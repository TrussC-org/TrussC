#!/usr/bin/env python3
"""Report third-party libraries that have something newer upstream.

Run weekly by .github/workflows/upstream-check.yml (#407). Reads the one
third-party list, docs/LICENSE.md "Third-Party Libraries" (through
tools/check_dependencies.py), and asks each upstream:

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
one tracking issue when the set of entries with something newer changes;
--fingerprint writes the key it compares.

Usage: python3 tools/check_upstream_versions.py [--body FILE] [--fingerprint FILE]
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
import urllib.request

sys.dont_write_bytecode = True  # no tools/__pycache__ in the checkout
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_dependencies as deps  # noqa: E402

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
    "dr_wav": {"skip": "bundled in miniaudio.h; updated together with miniaudio"},
    "dr_mp3": {"skip": "bundled in miniaudio.h; updated together with miniaudio"},
    "dr_flac": {"skip": "bundled in miniaudio.h; updated together with miniaudio"},
    # docs/SECURITY.md: tcxTls tracks the mbedTLS 3.6 LTS branch.
    "mbedTLS": {"series": "3.6"},
    # addons/tcxLua/README.md: sol2 does not support Lua 5.5 yet.
    "Lua": {"series": "5.4"},
    "libcurl": {"tag_prefix": "curl-"},
    "bcdec": {"file": "bcdec.h", "macros": ["BCDEC_VERSION_MAJOR", "BCDEC_VERSION_MINOR"]},
}

TIMEOUT = 60


def version_key(v):
    m = re.match(r"^(\d+(?:\.\d+)*)([a-z]?)$", v)
    if not m:
        return None
    return tuple(int(x) for x in m.group(1).split(".")), m.group(2)


def ls_remote(url, *patterns, tags=False):
    cmd = ["git", "ls-remote"] + (["--tags", "--refs"] if tags else []) + [url] + list(patterns)
    out = subprocess.run(cmd, check=True, capture_output=True,
                         text=True, timeout=TIMEOUT).stdout
    return [line.split("\t") for line in out.splitlines() if "\t" in line]


def raw(slug, ref, path):
    url = "https://raw.githubusercontent.com/%s/%s/%s" % (slug, ref, path)
    with urllib.request.urlopen(url, timeout=TIMEOUT) as r:
        return r.read()


def latest_release(row, cfg):
    url = "https://github.com/%s.git" % row["slug"]
    best = None
    tag_rx = re.compile("^" + cfg.get("tag_prefix", "v?") + r"(\d+(?:[._]\d+)*)([a-z]?)$")
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
    v = deps._macros(*cfg["macros"])(text)
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
    ap.add_argument("--fingerprint", help="write the key of the set of entries with something newer here")
    a = ap.parse_args()
    rows = deps.load(os.path.abspath(a.root))

    newer, skipped, failed = [], [], []
    for row in rows:
        state, text = check_row(row)
        if state == "newer":
            newer.append((row, text))
        elif state == "skip":
            skipped.append((row, text))
        elif state == "error":
            failed.append((row, text))
        print("%-16s %-8s %s" % (row["name"], state, text or ""), file=sys.stderr)

    key_src = "\n".join(sorted("%s|%s|%s" % (r["name"], current_text(r), t) for r, t in newer))
    key = hashlib.sha256(key_src.encode()).hexdigest()[:16] if newer else "none"

    list_url = "%s/%s/blob/main/%s#third-party-libraries" % (
        os.environ.get("GITHUB_SERVER_URL", "https://github.com"),
        os.environ.get("GITHUB_REPOSITORY", "TrussC-org/TrussC"), deps.LIST_FILE)
    out = []
    if newer:
        out.append(("Upstream has something newer for these entries of the third-party list "
                    "([docs/LICENSE.md](%s)). "
                    "Read the upstream release notes before updating; a pull request that updates "
                    "one changes its row in the list too (`tools/check_dependencies.py`).\n") % list_url)
        out.append("| Library | In TrussC | Upstream | Pinned / vendored in |")
        out.append("|---|---|---|---|")
        for r, t in newer:
            out.append("| %s | %s | %s | %s |" % (r["name"], current_text(r), t,
                                                ", ".join("`%s`" % p for p in r["paths"])))
    else:
        out.append(("Every checked entry of the third-party list "
                    "([docs/LICENSE.md](%s)) "
                    "is at its newest upstream release.") % list_url)
    if failed:
        out.append("\n**Could not check** (retried next week):\n")
        out += ["- %s: %s" % (r["name"], t) for r, t in failed]
    if skipped:
        out.append("\n<details><summary>Not checked</summary>\n")
        out += ["- %s: %s" % (r["name"], t) for r, t in skipped]
        out.append("\n</details>")
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
