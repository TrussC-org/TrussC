#!/usr/bin/env python3
"""Check the third-party list in docs/LICENSE.md against the build.

docs/LICENSE.md, "Third-Party Libraries", is the one list of third-party code
in TrussC and the version the build uses (#407). This script compares every
row with what the repo actually fetches or vendors:

  - every `FetchContent_Declare` in a tracked or unignored CMakeLists.txt / *.cmake has a
    row whose "Pinned / vendored in" cell names that file and whose upstream
    is the fetched repository;
  - that row's version equals the declaration's `GIT_TAG`, or the tag in its
    `URL` (a leading `v` and a `curl-` style prefix are ignored, `8_12_1`
    reads as `8.12.1`), and a `branch ... (not pinned)` row names the
    `GIT_TAG` branch;
  - a row's version equals the version line or macros of its vendored copy,
    where the copy states one (VENDORED_VERSIONS below);
  - a row's commit matches the commit in its provenance file (PROVENANCE_
    COMMITS below);
  - every path in "Pinned / vendored in" exists, and a row that names a
    CMakeLists.txt has a matching `FetchContent_Declare` in it;
  - every row is checked against something: a declaration, a vendored
    version, a provenance commit, or `not recorded` (NO_SOURCE lists the
    rows whose version is recorded only in the list itself).

It only reads files: no build, no network. It first runs its own self-test
on a copy of the parsed list with one version changed and one row removed.
The weekly upstream check (tools/check_upstream_versions.py) reads the list
through this module too.

Usage: python3 tools/check_dependencies.py [--root DIR]
Exit status: 0 when the list matches, 1 otherwise.
"""

import argparse
import copy
import os
import re
import subprocess
import sys

LIST_FILE = "docs/LICENSE.md"

# Vendored copies that state their version. Each entry is a list of
# (path, extractor); every listed copy must match the row.
def _line(pattern):
    """Version from the first match of `pattern` (one group)."""
    rx = re.compile(pattern, re.M)

    def get(text):
        m = rx.search(text)
        return m.group(1) if m else None
    return get


def _macros(*names):
    """Version joined from numeric (or quoted) #define values, with dots."""
    def get(text):
        parts = []
        for n in names:
            m = re.search(r'^\s*#\s*define\s+' + re.escape(n) + r'\s+"?(\w+)"?', text, re.M)
            if not m:
                return None
            parts.append(m.group(1))
        return ".".join(parts)
    return get


VENDORED_VERSIONS = {
    "stb_image": [("core/include/stb/stb_image.h", _line(r"stb_image - v(\S+)"))],
    "stb_image_write": [("core/include/stb/stb_image_write.h", _line(r"stb_image_write - v(\S+)"))],
    "stb_perlin": [("core/include/stb/stb_perlin.h", _line(r"stb_perlin\.h - v(\S+)"))],
    "stb_truetype": [("core/include/stb/stb_truetype.h", _line(r"stb_truetype\.h - v(\S+)"))],
    "stb_vorbis": [("core/include/stb_vorbis.c", _line(r"Ogg Vorbis audio decoder - v(\S+)"))],
    "miniaudio": [
        ("core/include/miniaudio.h", _line(r"^miniaudio - v(\S+)")),
        ("core/include/miniaudio.h", _macros("MA_VERSION_MAJOR", "MA_VERSION_MINOR", "MA_VERSION_REVISION")),
    ],
    "dr_wav": [("core/include/miniaudio.h", _macros("MA_DR_WAV_VERSION_MAJOR", "MA_DR_WAV_VERSION_MINOR", "MA_DR_WAV_VERSION_REVISION"))],
    "dr_mp3": [("core/include/miniaudio.h", _macros("MA_DR_MP3_VERSION_MAJOR", "MA_DR_MP3_VERSION_MINOR", "MA_DR_MP3_VERSION_REVISION"))],
    "dr_flac": [("core/include/miniaudio.h", _macros("MA_DR_FLAC_VERSION_MAJOR", "MA_DR_FLAC_VERSION_MINOR", "MA_DR_FLAC_VERSION_REVISION"))],
    "nlohmann/json": [("core/include/nlohmann/json.hpp", _macros("NLOHMANN_JSON_VERSION_MAJOR", "NLOHMANN_JSON_VERSION_MINOR", "NLOHMANN_JSON_VERSION_PATCH"))],
    "pugixml": [("core/include/pugixml/pugixml.hpp", _line(r"define PUGIXML_VERSION \d+ // (\S+)"))],
    "cpp-httplib": [("core/include/impl/httplib.h", _line(r'define CPPHTTPLIB_VERSION "([^"]+)"'))],
    "LZ4": [("core/include/lz4/lz4.h", _macros("LZ4_VERSION_MAJOR", "LZ4_VERSION_MINOR", "LZ4_VERSION_RELEASE"))],
    "Dear ImGui": [
        ("addons/tcxImGui/src/imgui/imgui.h", _line(r'define IMGUI_VERSION\s+"([^"]+)"')),
        ("addons/tcxImGui/src/imgui/TRUSSC_MODIFICATIONS.md", _line(r"\*\*Upstream base:\*\*.*?tag `v?([^`]+)`")),
    ],
    "bcdec": [("addons/tcxHap/src/impl/bcdec.h", _macros("BCDEC_VERSION_MAJOR", "BCDEC_VERSION_MINOR"))],
    "Lua": [
        ("addons/tcxLua/lua/include/lua.h", _macros("LUA_VERSION_MAJOR", "LUA_VERSION_MINOR", "LUA_VERSION_RELEASE")),
        ("addons/tcxLua/lua/src/lua.h", _macros("LUA_VERSION_MAJOR", "LUA_VERSION_MINOR", "LUA_VERSION_RELEASE")),
    ],
    "sol2": [
        ("addons/tcxLua/include/sol/sol.hpp", _line(r'define SOL_VERSION_STRING "([^"]+)"')),
        ("addons/tcxLua/include/sol/forward.hpp", _line(r'define SOL_VERSION_STRING "([^"]+)"')),
    ],
    "LuaJIT": [("addons/tcxLua/LuaJIT/src/luajit_rolling.h", _line(r'define LUAJIT_VERSION\s+"LuaJIT (\d+\.\d+)'))],
}


def _table_commit(file_name):
    """Commit column of the provenance table row for `file_name`."""
    return _line(r"^\| `" + re.escape(file_name) + r"` \|.*?\| `([0-9a-f]{7,40})`")


# Provenance files that record the upstream commit of a vendored copy.
PROVENANCE_COMMITS = {
    "sokol": ("core/include/sokol/TRUSSC_MODIFICATIONS.md", _line(r"\*\*Upstream base:\*\*.*?commit `([0-9a-f]{7,40})`")),
    "stb_image": ("core/include/stb/README.md", _table_commit("stb_image.h")),
    "stb_image_write": ("core/include/stb/README.md", _table_commit("stb_image_write.h")),
    "stb_perlin": ("core/include/stb/README.md", _table_commit("stb_perlin.h")),
    "stb_truetype": ("core/include/stb/README.md", _table_commit("stb_truetype.h")),
    "stb_vorbis": ("core/include/stb/README.md", _table_commit("../stb_vorbis.c")),
    "tinyobjloader": ("addons/tcxObj/PROVENANCE.md", _table_commit("src/tiny_obj_loader.h")),
}

# Rows whose version is recorded only in the list itself.
NO_SOURCE = {
    "luajit-cmake": "vendored CMake scripts without a version line; the commit is recorded only in the list",
}

# Copies without a version extractor still need a row and the correct path.
# Provenance files live outside some listed paths, so name the actual copy.
VENDORED_PATHS = {
    "sokol": "core/include/sokol/",
    "tinyobjloader": "addons/tcxObj/src/tiny_obj_loader.h",
    "earcut.hpp": "core/include/earcut/earcut.hpp",
    "sokol_imgui.h": "addons/tcxImGui/src/sokol_imgui.h",
    "luajit-cmake": "addons/tcxLua/luajit-cmake/",
}


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------

VERSION_RX = re.compile(r"^\d+(?:\.\d+)*[a-z]?$")


def parse_version_cell(cell):
    """Split a Version cell into version / commit / branch / not_recorded."""
    out = {"version": None, "commit": None, "branch": None, "not_recorded": False}
    m = re.match(r"\s*(\d+(?:\.\d+)*[a-z]?)\b", cell)
    if m:
        out["version"] = m.group(1)
    m = re.search(r"commit `([0-9a-f]{7,40})`", cell)
    if m:
        out["commit"] = m.group(1)
    m = re.search(r"branch `([^`]+)` \(not pinned\)", cell)
    if m:
        out["branch"] = m.group(1)
    if cell.strip().startswith("not recorded") or "commit not recorded" in cell:
        out["not_recorded"] = True
    return out


def parse_list(text):
    """Rows of the tables under "## Third-Party Libraries" in LICENSE.md."""
    start = text.find("\n## Third-Party Libraries")
    if start < 0:
        raise ValueError(LIST_FILE + ': no "## Third-Party Libraries" section')
    end = text.find("\n## ", start + 1)
    section = text[start:end if end >= 0 else len(text)]
    lineno0 = text[:start].count("\n") + 1
    rows = []
    header = None
    for i, line in enumerate(section.split("\n")):
        if not line.startswith("|"):
            header = None
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if header is None:
            header = [c.lower() for c in cells]
            continue
        if set(line.replace("|", "").strip()) <= set("-: "):
            continue
        col = dict(zip(header, cells))
        name = col.get("library", "").replace("**", "").strip()
        where = col.get("pinned / vendored in", "")
        upstream = col.get("upstream", "")
        row = {
            "name": name,
            "line": lineno0 + i,
            "version_cell": col.get("version", ""),
            "paths": re.findall(r"`([^`]+)`", where),
            "upstream": upstream,
            "slug": github_slug(upstream),
        }
        row.update(parse_version_cell(row["version_cell"]))
        rows.append(row)
    return rows


def github_slug(url):
    m = re.search(r"github\.com/([^/\s)]+)/([^/\s)#?]+)", url or "")
    if not m:
        return None
    repo = m.group(2)
    if repo.endswith(".git"):
        repo = repo[:-4]
    return (m.group(1) + "/" + repo).lower()


def normalize_tag(tag):
    """`v3.6.7` -> `3.6.7`, `curl-8_12_1` -> `8.12.1`, `1.2.1` stays."""
    t = re.sub(r"^[A-Za-z][A-Za-z-]*?[-_]?(?=\d)", "", tag.strip())
    if "." not in t and "_" in t:
        t = t.replace("_", ".")
    return t


def tag_from_url(url):
    m = re.search(r"/refs/tags/(.+?)\.(?:tar\.gz|tar\.xz|tar\.bz2|tgz|zip)$", url)
    if m:
        return m.group(1)
    m = re.search(r"/releases/download/([^/]+)/", url)
    if m:
        return m.group(1)
    m = re.search(r"/archive/(.+?)\.(?:tar\.gz|tar\.xz|tgz|zip)$", url)
    if m:
        return m.group(1)
    return None


def tracked_cmake_files(root):
    try:
        out = subprocess.run(
            ["git", "-C", root, "ls-files", "--cached", "--others", "--exclude-standard",
             "-z", "--", "*CMakeLists.txt", "*.cmake"],
            check=True, capture_output=True, text=True).stdout.split("\0")
        files = [f for f in out if f]
        if files:
            return sorted(set(files))
    except (OSError, subprocess.CalledProcessError):
        pass
    files = []
    for d, dirs, names in os.walk(root):
        dirs[:] = [x for x in dirs if not x.startswith(".") and not x.startswith("build") and x != "node_modules"]
        for n in names:
            if n == "CMakeLists.txt" or n.endswith(".cmake"):
                files.append(os.path.relpath(os.path.join(d, n), root).replace(os.sep, "/"))
    return files


def parse_fetch_declares(text):
    """(name, {KEY: value}, line) for every FetchContent_Declare( ... )."""
    # Tokenize before looking for commands: comments and quoted/bracket
    # arguments can contain command names, '#' and parentheses literally.
    rx = re.compile(
        r'(?P<bracket>#?\[(?P<eq>=*)\[.*?\](?P=eq)\])'
        r'|(?P<comment>#[^\n]*)|(?P<quoted>"(?:\\.|[^"\\])*")'
        r'|(?P<word>[^\s()"#]+)|(?P<paren>[()])', re.S)
    tokens = []
    for m in rx.finditer(text):
        value, kind = m.group(), m.lastgroup
        if kind == "comment" or (kind == "bracket" and value.startswith("#")):
            continue
        if kind == "quoted":
            value = value[1:-1]
        elif kind == "bracket":
            width = len(m.group("eq")) + 2
            value = value[width:-width]
        tokens.append((value, kind, m.start()))
    out = []
    i = 0
    while i + 1 < len(tokens):
        value, kind, position = tokens[i]
        if kind != "word" or value.lower() != "fetchcontent_declare" or tokens[i + 1][:2] != ("(", "paren"):
            i += 1
            continue
        line = text[:position].count("\n") + 1
        depth, i, body = 1, i + 2, []
        while i < len(tokens) and depth:
            value, kind, _ = tokens[i]
            if (value, kind) == ("(", "paren"):
                depth += 1
            elif (value, kind) == (")", "paren"):
                depth -= 1
            if depth:
                body.append(value)
            i += 1
        if depth or not body:
            raise ValueError("line %d: incomplete FetchContent_Declare" % line)
        args = {}
        keywords = [v.upper() for v in body]
        for k in ("GIT_REPOSITORY", "GIT_TAG", "URL"):
            if k in keywords:
                j = keywords.index(k)
                if j + 1 < len(body):
                    args[k] = body[j + 1]
        out.append((body[0], args, line))
    return out


def same_upstream(url, row):
    slug = github_slug(url)
    if slug:
        return slug == row["slug"]
    # Non-GitHub sources must match too; an unknown host is not a wildcard.
    upstream = row["upstream"].rstrip("/").removesuffix(".git")
    source = url.rstrip("/").removesuffix(".git")
    return bool(upstream) and (source == upstream or source.startswith(upstream + "/"))


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


# --------------------------------------------------------------------------
# The check
# --------------------------------------------------------------------------

def check(root, rows):
    errors = []
    by_name = {r["name"]: r for r in rows}

    def err(row, msg):
        errors.append("%s:%d: %s: %s" % (LIST_FILE, row["line"], row["name"], msg))

    checked = set()

    # Paths exist.
    for r in rows:
        if not r["paths"]:
            err(r, 'no path in "Pinned / vendored in"')
        for p in r["paths"]:
            if not os.path.exists(os.path.join(root, p.rstrip("/"))):
                err(r, "path %s does not exist" % p)

    # A real but unrelated path must not satisfy a known vendored row.
    vendored_paths = dict(VENDORED_PATHS)
    vendored_paths.update({name: sources[0][0] for name, sources in VENDORED_VERSIONS.items()})
    for name, path in vendored_paths.items():
        r = by_name.get(name)
        if r is None:
            errors.append("%s: no row for %s (vendored in %s)" % (LIST_FILE, name, path))
        elif not any(path.rstrip("/") == p.rstrip("/") or
                     (os.path.isdir(os.path.join(root, p)) and path.startswith(p.rstrip("/") + "/"))
                     for p in r["paths"]):
            err(r, '"Pinned / vendored in" does not include %s' % path)

    # FetchContent declarations.
    fetched_rows = set()
    for f in tracked_cmake_files(root):
        try:
            text = read(root, f)
        except OSError:
            continue
        try:
            declarations = parse_fetch_declares(text)
        except ValueError as e:
            errors.append("%s: %s" % (f, e))
            continue
        for name, args, line in declarations:
            url = args.get("GIT_REPOSITORY") or args.get("URL") or ""
            match = [r for r in rows if f in r["paths"] and same_upstream(url, r)]
            where = "%s:%d: FetchContent_Declare(%s)" % (f, line, name)
            if not match:
                errors.append("%s has no row in %s (a row naming `%s` with upstream %s)"
                              % (where, LIST_FILE, f, url or "?"))
                continue
            if len(match) > 1:
                errors.append("%s matches multiple rows in %s" % (where, LIST_FILE))
                continue
            r = match[0]
            fetched_rows.add((r["name"], f))
            checked.add(r["name"])
            if "GIT_TAG" in args:
                pinned = args["GIT_TAG"]
            elif "URL" in args:
                pinned = tag_from_url(args["URL"])
            else:
                pinned = None
            if pinned is None:
                err(r, "%s: no GIT_TAG and no tag in URL" % where)
            elif r["branch"]:
                if pinned != r["branch"]:
                    err(r, "list says branch `%s`, %s fetches `%s`" % (r["branch"], where, pinned))
            elif r["commit"] and not r["version"]:
                if not re.fullmatch(r"[0-9a-f]{7,40}", pinned) or not (
                        pinned.startswith(r["commit"]) or r["commit"].startswith(pinned)):
                    err(r, "list says commit %s, %s fetches %s" % (r["commit"], where, pinned))
            elif r["version"]:
                if normalize_tag(pinned) != r["version"]:
                    err(r, "list says %s, %s fetches %s" % (r["version"], where, pinned))
            else:
                err(r, "%s: the row has no version, commit or branch" % where)

    # A row naming a CMakeLists.txt has a declaration in it.
    for r in rows:
        for p in r["paths"]:
            if (p.endswith("CMakeLists.txt") or p.endswith(".cmake")) and (r["name"], p) not in fetched_rows \
                    and os.path.exists(os.path.join(root, p)):
                err(r, "%s has no matching FetchContent_Declare" % p)

    # Vendored version lines.
    for name, sources in VENDORED_VERSIONS.items():
        r = by_name.get(name)
        if r is None:
            errors.append("%s: no row for %s (vendored in %s)" % (LIST_FILE, name, sources[0][0]))
            continue
        checked.add(name)
        for path, get in sources:
            try:
                got = get(read(root, path))
            except OSError:
                err(r, "cannot read %s" % path)
                continue
            if got is None:
                err(r, "no version found in %s" % path)
            elif got.lstrip("v") != r["version"]:
                err(r, "list says %s, %s says %s" % (r["version"], path, got))

    # Provenance commits.
    for name, (path, get) in PROVENANCE_COMMITS.items():
        r = by_name.get(name)
        if r is None:
            errors.append("%s: no row for %s (provenance in %s)" % (LIST_FILE, name, path))
            continue
        checked.add(name)
        try:
            got = get(read(root, path))
        except OSError:
            err(r, "cannot read %s" % path)
            continue
        if got is None:
            err(r, "no commit found in %s" % path)
        elif not r["commit"]:
            err(r, "list has no commit, %s says %s" % (path, got[:7]))
        elif not got.startswith(r["commit"]):
            err(r, "list says commit %s, %s says %s" % (r["commit"], path, got[:7]))

    # Every row is checked against something.
    for r in rows:
        if r["name"] in checked or r["not_recorded"] or r["name"] in NO_SOURCE:
            continue
        err(r, "nothing to check this row against: add it to VENDORED_VERSIONS / "
               "PROVENANCE_COMMITS / NO_SOURCE in tools/check_dependencies.py")
    names = [r["name"] for r in rows]
    for n in sorted(set(x for x in names if names.count(x) > 1)):
        errors.append("%s: %s has more than one row" % (LIST_FILE, n))
    return list(dict.fromkeys(errors))


def selftest(root, rows):
    """The check must catch a changed version and a removed row."""
    problems = []
    assert normalize_tag("v3.6.7") == "3.6.7"
    assert normalize_tag("curl-8_12_1") == "8.12.1"
    assert normalize_tag("1.2.1") == "1.2.1"
    assert tag_from_url("https://github.com/celtera/libremidi/archive/refs/tags/v5.4.3.tar.gz") == "v5.4.3"
    assert parse_version_cell("commit `966edce` (x)")["commit"] == "966edce"
    assert parse_version_cell("branch `master` (not pinned)")["branch"] == "master"
    fetched = [r for r in rows if any(p.endswith("CMakeLists.txt") for p in r["paths"]) and r["version"]]
    vendored = [r for r in rows if r["name"] in VENDORED_VERSIONS]
    for victim in fetched[:1] + vendored[:1]:
        bad = copy.deepcopy(rows)
        for r in bad:
            if r["name"] == victim["name"]:
                r["version"] = "0.0.0-selftest"
        if not any(victim["name"] in e and "0.0.0-selftest" in e for e in check(root, bad)):
            problems.append("a changed version of %s was not reported" % victim["name"])
    for victim in fetched[:1]:
        bad = [r for r in rows if r["name"] != victim["name"]]
        if not any("has no row" in e for e in check(root, bad)):
            problems.append("a removed row (%s) was not reported" % victim["name"])
    return problems


def load(root):
    return parse_list(read(root, LIST_FILE))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    rows = load(root)
    problems = selftest(root, rows)
    if problems:
        for p in problems:
            print("check_dependencies self-test: " + p)
        return 1
    errors = check(root, rows)
    for e in errors:
        print(e)
    if errors:
        print("\n%d problem(s). %s is the one list of third-party versions: update the row "
              "(or the build), so the two agree." % (len(errors), LIST_FILE))
        return 1
    print("check_dependencies: %d third-party entries in %s match the build." % (len(rows), LIST_FILE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
