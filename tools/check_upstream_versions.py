#!/usr/bin/env python3
"""Report third-party libraries that have something newer upstream.

Run weekly by .github/workflows/upstream-check.yml (#407). Reads the one
third-party list, docs/LICENSE.md "Third-Party Libraries", and asks each upstream:

  - a row with a release version: the newest release tag
    (`git ls-remote --tags`: `v1.2.3`, `1.2.3`, or the prefix UPSTREAM
    gives), compared as a version number; pre-release tags (rc, beta, ...)
    are ignored, and UPSTREAM can limit a row to one series;
  - a row pinned to a commit: whether the vendored files changed on the
    upstream branch since that commit (the file at the pinned commit and at
    the branch tip, from raw.githubusercontent.com), or, with no files
    listed, whether the branch tip moved (`git ls-remote`);
  - a row whose vendored copy has no release tags: the version macros of the
    same file at the upstream default branch.

It needs no token: only public git and raw file reads. It never changes a
version and never fails because of an upstream: an upstream that cannot be
reached is listed under "Could not check". The workflow posts the report to
one tracking issue when the set of library names with something newer or the
OSV results change;
incomplete checks are summarized without replacing the last complete report;
--fingerprint writes the key it compares.

OSV.dev is queried in one timeout-bounded batch of immutable upstream commits.
Short pins and release tags are first resolved through GitHub. Unmatchable
entries and failed checks are explicit; advisory output is limited to IDs/links.
--osv-only runs this part for the release checklist without upstream reads.

Usage: python3 tools/check_upstream_versions.py [--body FILE] [--fingerprint FILE]
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.request
import urllib.parse

sys.dont_write_bytecode = True  # the shared inventory parser only reads files
from check_dependencies import LIST_FILE, parse_list


# How to ask each upstream, by row name. Rows not listed here use the
# default for their Version cell: release tags for a version, the branch tip
# for a commit. "skip" rows are listed under "Not checked" with the reason.
UPSTREAM = {
    "sokol": {"branch": "master",
              "files": ["sokol_app.h", "sokol_gfx.h", "sokol_glue.h", "sokol_log.h", "util/sokol_gl.h"]},
    "stb_image": {"branch": "nv/all-fixes", "files": ["stb_image.h"]},
    "stb_image_write": {"branch": "master", "files": ["stb_image_write.h"]},
    "stb_perlin": {"branch": "master", "files": ["stb_perlin.h"]},
    "stb_truetype": {"branch": "master", "files": ["stb_truetype.h"]},
    "stb_vorbis": {"branch": "stb_vorbis-sezero", "files": ["stb_vorbis.c"]},
    "tinyobjloader": {"branch": "release", "files": ["tiny_obj_loader.h"]},
    "luajit-cmake": {"branch": "master"},
    "earcut.hpp": {"branch": "master", "files": ["include/mapbox/earcut.hpp"]},
    "dr_wav": {"skip": "bundled in miniaudio.h; updated together with miniaudio"},
    "dr_mp3": {"skip": "bundled in miniaudio.h; updated together with miniaudio"},
    "dr_flac": {"skip": "bundled in miniaudio.h; updated together with miniaudio"},
    # docs/architecture/SECURITY_NOTES.md: tcxTls tracks the mbedTLS 3.6 LTS branch.
    "mbedTLS": {"series": "3.6"},
    # addons/tcxLua/README.md: sol2 does not support Lua 5.5 yet.
    "Lua": {"series": "5.4"},
    # tcxBox2d is frozen on v2; tcxBox2d3 tracks v3 separately.
    "Box2D": {"series": "2.4"},
    "libcurl": {"tag_prefix": "curl-"},
}

# OSV's commit query accepts a full git SHA (no repository field). Keep the
# repository here to resolve short pins and release tags to that SHA. This
# avoids guessing package ecosystem aliases or treating fork versions as releases.
# An empty config uses the inventory commit; tag configs apply only without one.
OSV = {
    "sokol": {}, "sokol_imgui.h": {},
    "stb_image": {}, "stb_image_write": {}, "stb_perlin": {},
    "stb_truetype": {}, "stb_vorbis": {},
    "miniaudio": {"tag": "{version}"},
    "nlohmann/json": {"tag": "v{version}"},
    "pugixml": {"tag": "v{version}"},
    "cpp-httplib": {"tag": "v{version}"},
    "LZ4": {"tag": "v{version}"}, "Highway": {}, "earcut.hpp": {},
    "sokol-shdc (sokol-tools-bin)": {},
    "Dear ImGui": {"tag": "v{version}"},
    "mbedTLS": {"tag": "v{version}"},
    "libcurl": {"tag": "curl-{version_underscored}"},
    "libremidi": {"tag": "v{version}"}, "Box2D": {"tag": "v{version}"},
    "Box2D v3": {"tag": "v{version}"},
    "cgltf": {"tag": "v{version}"}, "tinyobjloader": {},
    "Snappy": {"tag": "{version}"}, "HAP": {},
    "Lua": {"tag": "v{version}"}, "sol2": {"tag": "v{version}"},
    "luajit-cmake": {},
    "dr_wav": {"skip": "bundled component has no recorded standalone git commit"},
    "dr_mp3": {"skip": "bundled component has no recorded standalone git commit"},
    "dr_flac": {"skip": "bundled component has no recorded standalone git commit"},
    "LuaJIT": {"skip": "rolling version without a recorded git commit"},
    "Bitmap ASCII glyph data": {"skip": "source version/commit not recorded"},
    "ofxMidi helper ports": {"skip": "source version/commit not recorded"},
    "Mozilla/curl CA bundle": {"skip": "data snapshot without a source commit or OSV package mapping"},
}
OSV_URL = "https://api.osv.dev/v1/querybatch"
OSV_TIMEOUT = 20


def osv_commit(row, cfg):
    """Resolve the inventory's upstream repository + pin, never a branch tip."""
    if row["branch"] or row["not_recorded"]:
        return None
    ref = row["commit"]
    if ref and re.fullmatch(r"[0-9a-f]{40}", ref):
        return ref
    if not ref and row["version"] and "tag" in cfg:
        ref = cfg["tag"].format(version=row["version"],
                                version_underscored=row["version"].replace(".", "_"))
    if not ref or not row["slug"]:
        return None
    # GitHub resolves abbreviated pins without fetching/cloning a repository.
    url = "https://api.github.com/repos/%s/commits/%s" % (
        row["slug"], urllib.parse.quote(ref, safe=""))
    headers = {"Accept": "application/vnd.github+json"}
    token = os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = "Bearer " + token
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=OSV_TIMEOUT) as response:
        sha = json.load(response).get("sha", "")
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("invalid resolved commit")
    return sha


def check_osv(rows):
    """Return (row, state, IDs/reason) for every entry. Never copy advisory text.

    State is checked, skip, or error. Resolve each immutable pin, then make one
    bounded querybatch request for this small inventory. Errors are incomplete
    checks, never clean results. No exception/response text is published.
    """
    results, pending, queries = [], [], []
    for row in rows:
        cfg = OSV.get(row["name"], {"skip": "no OSV mapping configured"})
        if "skip" in cfg:
            results.append((row, "skip", cfg["skip"]))
            continue
        try:
            commit = osv_commit(row, cfg)
            if commit is None:
                results.append((row, "skip", "no immutable version/commit usable by the OSV mapping"))
                continue
        except Exception:
            results.append((row, "error", "could not resolve upstream pin for OSV"))
            continue
        pending.append(row)
        queries.append({"commit": commit})
    if not queries:
        return results
    try:
        request = urllib.request.Request(OSV_URL, data=json.dumps({"queries": queries}).encode(),
                                         headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(request, timeout=OSV_TIMEOUT) as response:
            data = json.load(response)
        batch = data["results"]
        if not isinstance(batch, list) or len(batch) != len(pending):
            raise ValueError("invalid OSV batch length")
        checked = []
        for row, result in zip(pending, batch):
            if not isinstance(result, dict) or set(result) - {"vulns"}:
                raise ValueError("invalid or incomplete OSV result")
            vulns = result.get("vulns", [])
            if not isinstance(vulns, list):
                raise ValueError("invalid OSV vulnerabilities")
            ids = []
            for advisory in vulns:
                advisory_id = advisory.get("id")
                if not isinstance(advisory_id, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", advisory_id):
                    raise ValueError("invalid OSV advisory ID")
                ids.append(advisory_id)
            checked.append((row, "checked", sorted(set(ids))))
        results.extend(checked)
    except Exception:
        results.extend((row, "error", "OSV query failed; check could not run") for row in pending)
    return results


def osv_report(results):
    out = ["\n### OSV vulnerability check\n"]
    failed = any(state == "error" for _, state, _ in results)
    hits = [(r, ids) for r, state, ids in results if state == "checked" and ids]
    if failed:
        out.append("**OSV check failed or is incomplete; the check could not run for all mapped entries.**")
    if hits:
        out += ["| Library | In TrussC | Advisory ID |", "|---|---|---|"]
        for r, ids in hits:
            for advisory_id in ids:
                out.append("| %s | %s | [%s](https://osv.dev/vulnerability/%s) |" % (
                    r["name"], current_text(r), advisory_id, advisory_id))
    elif not failed:
        if any(state == "checked" for _, state, _ in results):
            out.append("No OSV matches for the checked commits. This does not cover entries listed as not checkable.")
        else:
            out.append("No entries could be checked with OSV.")
    for r, state, detail in results:
        if state == "skip":
            out.append("- %s: not checkable (%s)" % (r["name"], detail))
        elif state == "error":
            out.append("- %s: check failed (%s)" % (r["name"], detail))
    return out


TIMEOUT = 60


def version_key(v):
    m = re.match(r"^(\d+(?:\.\d+)*)([a-z]?)$", v)
    if not m:
        return None
    return tuple(int(x) for x in m.group(1).split(".")), m.group(2)


def ls_remote(url, *patterns, tags=False):
    cmd = ["git", "ls-remote"] + (["--tags", "--refs"] if tags else []) + [url] + list(patterns)
    out = subprocess.run(cmd, check=True, capture_output=True,
                         text=True, timeout=TIMEOUT, encoding="utf-8").stdout
    return [line.split("\t") for line in out.splitlines() if "\t" in line]


def raw(slug, ref, path):
    url = "https://raw.githubusercontent.com/%s/%s/%s" % (slug, ref, path)
    with urllib.request.urlopen(url, timeout=TIMEOUT) as r:
        return r.read()


def latest_release(row, cfg):
    url = "https://github.com/%s.git" % row["slug"]
    best = None
    tag_rx = re.compile("^" + (re.escape(cfg["tag_prefix"]) if "tag_prefix" in cfg else "v?") + r"(\d+(?:[._]\d+)*)([a-z]?)$")
    for _sha, ref in ls_remote(url, tags=True):
        tag = ref[len("refs/tags/"):]
        m = tag_rx.match(tag)
        if not m:
            continue
        v = m.group(1).replace("_", ".") + m.group(2)
        if cfg.get("series") and not (v + ".").startswith(cfg["series"] + "."):
            continue
        k = version_key(v)
        if k and (best is None or k > best[0]):
            best = (k, v, tag)
    if best is None:
        raise RuntimeError("no release tags")
    cur = version_key(row["version"])
    if cur is not None and best[0] > cur:
        return "%s (tag `%s`)" % (best[1], best[2])
    return None


def commit_changes(row, cfg):
    branch = cfg.get("branch", "master")
    files = cfg.get("files")
    url = "https://github.com/%s.git" % row["slug"]
    heads = ls_remote(url, "refs/heads/" + branch)
    if not heads:
        raise RuntimeError("no branch " + branch)
    tip = heads[0][0]
    if tip.startswith(row["commit"]):
        return None
    if not files:
        return "`%s` is at `%s` (pinned `%s`)" % (branch, tip[:7], row["commit"])
    changed = [f for f in files if raw(row["slug"], row["commit"], f) != raw(row["slug"], tip, f)]
    if changed:
        return "%s changed on `%s` since `%s` (tip `%s`)" % (
            ", ".join("`%s`" % f for f in changed), branch, row["commit"], tip[:7])
    return None


def file_version(row, cfg):
    text = raw(row["slug"], "HEAD", cfg["file"]).decode("utf-8", "replace")
    parts = [re.search(r"^\s*#\s*define\s+" + re.escape(m) + r"\s+(\d+)", text, re.M)
             for m in cfg["macros"]]
    v = ".".join(m.group(1) for m in parts) if all(parts) else None
    if v is None:
        raise RuntimeError("no version macros in upstream " + cfg["file"])
    if version_key(v) and version_key(row["version"]) and version_key(v) > version_key(row["version"]):
        return "%s (`%s` on the default branch)" % (v, cfg["file"])
    return None


def check_row(row):
    """('newer', text) | ('current', None) | ('skip', reason) | ('error', text)."""
    cfg = UPSTREAM.get(row["name"], {})
    if "skip" in cfg:
        return "skip", cfg["skip"]
    if row["branch"]:
        return "skip", "fetches the tip of `%s` (not pinned)" % row["branch"]
    if row["not_recorded"]:
        return "skip", "version not recorded"
    if not row["slug"]:
        return "skip", "upstream is not on GitHub"
    try:
        if "file" in cfg:
            newer = file_version(row, cfg)
        elif row["commit"]:
            newer = commit_changes(row, cfg)
        elif row["version"]:
            newer = latest_release(row, cfg)
        else:
            return "skip", "no version, commit or branch in the list"
    except Exception as e:  # network, missing tags, ...: report, never fail
        return "error", "%s: %s" % (type(e).__name__, e)
    return ("newer", newer) if newer else ("current", None)


def current_text(row):
    if row["commit"] and row["version"]:
        return "%s, commit `%s`" % (row["version"], row["commit"])
    if row["commit"]:
        return "commit `%s`" % row["commit"]
    return row["version"] or ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap.add_argument("--body", help="write the Markdown report here (default: stdout)")
    ap.add_argument("--fingerprint", help="write the upstream/OSV report fingerprint here")
    ap.add_argument("--osv-only", action="store_true", help="only run OSV (release checklist)")
    a = ap.parse_args()
    with open(os.path.join(os.path.abspath(a.root), LIST_FILE), encoding="utf-8") as f:
        rows = parse_list(f.read())
    if not rows or len({r["name"] for r in rows}) != len(rows):
        raise ValueError("third-party inventory is empty or has duplicate names")

    newer, skipped, failed = [], [], []
    for row in rows:
        state, text = ("current", None) if a.osv_only else check_row(row)
        if state == "newer":
            newer.append((row, text))
        elif state == "skip":
            skipped.append((row, text))
        elif state == "error":
            failed.append((row, text))
        print("%-16s %-8s %s" % (row["name"], state, text or ""), file=sys.stderr)

    osv = check_osv(rows)
    # Keep upstream's set-only behavior, but fingerprint the full OSV outcome,
    # including coverage and the shipped pin. Sort so inventory/API order is immaterial.
    osv_hits = any(state == "checked" and ids for _, state, ids in osv)
    osv_failed = any(state == "error" for _, state, _ in osv)
    key_src = json.dumps([sorted(r["name"] for r, _ in newer), sorted(
        (r["name"], current_text(r), state, detail) for r, state, detail in osv)], sort_keys=True)
    key = "incomplete" if failed or osv_failed else (
        hashlib.sha256(key_src.encode()).hexdigest()[:16] if newer or osv_hits else "none")

    list_url = "%s/%s/blob/main/%s#third-party-libraries" % (
        os.environ.get("GITHUB_SERVER_URL", "https://github.com"),
        os.environ.get("GITHUB_REPOSITORY", "TrussC-org/TrussC"), LIST_FILE)
    out = []
    if a.osv_only:
        out.append("Third-party OSV release check ([docs/LICENSE.md](%s))." % list_url)
    elif newer:
        out.append(("Upstream has something newer for these entries of the third-party list "
                    "([docs/LICENSE.md](%s)). "
                    "Read the upstream release notes before updating; a pull request that updates "
                    "one changes its row in the list too.\n") % list_url)
        out.append("| Library | In TrussC | Upstream | Pinned / vendored in |")
        out.append("|---|---|---|---|")
        for r, t in newer:
            out.append("| %s | %s | %s | %s |" % (r["name"], current_text(r), t,
                                                ", ".join("`%s`" % p for p in r["paths"])))
    elif failed:
        out.append("No newer versions were found among reachable upstreams. The check is incomplete.")
    else:
        out.append(("Every checked entry of the third-party list "
                    "([docs/LICENSE.md](%s)) "
                    "has no newer upstream release or changes to its tracked files.") % list_url)
    if failed:
        out.append("\n**Could not check** (retried next week):\n")
        out += ["- %s: %s" % (r["name"], t) for r, t in failed]
    if skipped:
        out.append("\n<details><summary>Not checked</summary>\n")
        out += ["- %s: %s" % (r["name"], t) for r, t in skipped]
        out.append("\n</details>")
    out.extend(osv_report(osv))
    out.append("\n<!-- upstream-check: %s -->" % key)
    body = "\n".join(out) + "\n"

    if a.body:
        with open(a.body, "w", encoding="utf-8") as f:
            f.write(body)
    else:
        sys.stdout.write(body)
    if a.fingerprint:
        with open(a.fingerprint, "w", encoding="utf-8") as f:
            f.write(key + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
