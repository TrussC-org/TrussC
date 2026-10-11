"""Offline regression tests for the weekly workflow; never contact GitHub."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    with patch.object(sys, "path", [str(ROOT / "tools")] + sys.path):
        spec.loader.exec_module(result)
    return result


check = module("check_upstream_versions")
publish = module("update_upstream_issue")


def row(name="library", version="1.2.3", commit=None):
    return dict(name=name, version=version, commit=commit, branch=None,
                not_recorded=False, slug="owner/library", paths=["library.h"])


# A fixed copy of docs/LICENSE.md rows, so the parser's exact values are
# checked without pinning the real inventory (which changes with every bump).
FIXTURE_LIST = """# License

## Third-Party Libraries

### Core

| Library | Version | Pinned / vendored in | Upstream | License | Author/Organization |
|---------|---------|----------------------|----------|---------|---------------------|
| **earcut.hpp** | not recorded | `core/include/earcut/earcut.hpp` | https://github.com/mapbox/earcut.hpp | ISC | Mapbox |
| **LuaJIT** | 2.1 (rolling), commit not recorded | `addons/tcxLua/LuaJIT/` | https://github.com/LuaJIT/LuaJIT | MIT | Mike Pall |
| **branchlib** | branch `x` (not pinned) | `branchlib.h` | https://github.com/owner/branchlib | MIT | Someone |

### Build Tools

| Library | Version | Pinned / vendored in | Upstream | License | Author/Organization | SHA-256 |
|---------|---------|----------------------|----------|---------|---------------------|---------|
| **sokol-shdc (sokol-tools-bin)** | commit `11d0cf678105d614d675e6d9bd2aaf3eeff12f8c` (2026-08-29T14:15:01Z) | `core/cmake/trussc_shaders.cmake` | https://github.com/floooh/sokol-tools-bin | zlib License | Andre Weissflog | linux: `ed35e89ef381d521a499096ed4ada85e4d135d8011e151cca6b7d893c43b21df` |

### Addons

| Library | Version | Pinned / vendored in | Upstream | License | Author/Organization | Addon |
|---------|---------|----------------------|----------|---------|---------------------|-------|
| **mbedTLS** | 3.6.7 | `addons/tcxTls/CMakeLists.txt` | https://github.com/Mbed-TLS/mbedtls | Apache-2.0 or GPL-2.0-or-later (dual-licensed) | Arm Limited | tcxTls |
| **libcurl** | 8.12.1 (Windows, when no system libcurl is found) | `addons/tcxCurl/CMakeLists.txt` | https://github.com/curl/curl | curl License (MIT-style) | Daniel Stenberg and contributors | tcxCurl |
| **tinyobjloader** | commit `966edce` (branch `release`, + 2 TrussC patches) | `addons/tcxObj/src/tiny_obj_loader.h` | https://github.com/tinyobjloader/tinyobjloader | MIT | Syoyo Fujita and contributors | tcxObj |
| **HAP** | commit `d847f6bbd3be88575dd4ef33a877243780e3be76` (2024-07-25) | `addons/tcxHap/CMakeLists.txt` | https://github.com/Vidvox/hap | BSD 2-Clause | Tom Butterworth, Vidvox LLC | tcxHap |

## Next Section
"""


class UpstreamTests(unittest.TestCase):
    def test_inventory_shape(self):
        # Shape only: the weekly workflow runs these tests before the report,
        # so a pinned value that went stale would stop the report itself.
        rows = check.parse_list((ROOT / check.LIST_FILE).read_text(encoding="utf-8"))
        self.assertTrue(rows)
        for r in rows:
            with self.subTest(library=r["name"]):
                self.assertTrue(r["slug"])
                self.assertTrue(r["paths"])
                self.assertTrue(r["version"] or r["commit"] or r["branch"] or r["not_recorded"])
                for p in r["paths"]:
                    self.assertTrue((ROOT / p).exists(), p)

    def test_parse_list_values(self):
        by_name = {r["name"]: r for r in check.parse_list(FIXTURE_LIST)}
        self.assertEqual(set(by_name), {"earcut.hpp", "LuaJIT", "branchlib",
                                        "sokol-shdc (sokol-tools-bin)", "mbedTLS", "libcurl",
                                        "tinyobjloader", "HAP"})
        self.assertTrue(by_name["earcut.hpp"]["not_recorded"])
        self.assertIsNone(by_name["earcut.hpp"]["version"])
        self.assertIsNone(by_name["earcut.hpp"]["commit"])
        self.assertEqual(by_name["LuaJIT"]["version"], "2.1")
        self.assertTrue(by_name["LuaJIT"]["not_recorded"])
        self.assertEqual(by_name["branchlib"]["branch"], "x")
        self.assertFalse(by_name["branchlib"]["not_recorded"])
        self.assertEqual(by_name["sokol-shdc (sokol-tools-bin)"]["hashes"],
                         {"linux": "ed35e89ef381d521a499096ed4ada85e4d135d8011e151cca6b7d893c43b21df"})
        self.assertEqual(by_name["mbedTLS"]["version"], "3.6.7")
        self.assertEqual(by_name["mbedTLS"]["slug"], "mbed-tls/mbedtls")
        self.assertEqual(by_name["libcurl"]["version"], "8.12.1")
        self.assertEqual(by_name["tinyobjloader"]["commit"], "966edce")
        self.assertIsNone(by_name["tinyobjloader"]["version"])
        self.assertEqual(by_name["HAP"]["commit"], "d847f6bbd3be88575dd4ef33a877243780e3be76")
        self.assertEqual(by_name["sokol-shdc (sokol-tools-bin)"]["commit"],
                         "11d0cf678105d614d675e6d9bd2aaf3eeff12f8c")
        self.assertEqual(by_name["HAP"]["paths"], ["addons/tcxHap/CMakeLists.txt"])

    def test_new_pins_use_commit_checks_in_weekly_report(self):
        rows = check.parse_list(FIXTURE_LIST)
        for current in rows:
            if current["name"] not in {"HAP", "sokol-shdc (sokol-tools-bin)"}:
                continue
            with self.subTest(library=current["name"]), \
                    patch.object(check, "ls_remote", return_value=[("0" * 40, "refs/heads/master")]):
                state, text = check.check_row(current)
                self.assertEqual(state, "newer")
                self.assertIn(current["commit"], text)

    def test_releases_ignore_prereleases_and_sort_numerically(self):
        tags = ["v1.9.0", "v1.10.0", "v2.0.0-rc1", "v2.0.0-beta", "unrelated"]
        with patch.object(check, "ls_remote", return_value=[("sha", "refs/tags/" + t) for t in tags]):
            self.assertEqual(check.latest_release(row(), {}), "1.10.0 (tag `v1.10.0`)")
            self.assertIsNone(check.latest_release(row(version="1.10.0"), {}))

    def test_series_and_curl_prefix(self):
        with patch.object(check, "ls_remote", return_value=[("sha", "refs/tags/" + t) for t in
                         ["v3.6.8", "v3.7.0", "v4.0.0"]]):
            self.assertEqual(check.latest_release(row(version="3.6.7"), {"series": "3.6"}),
                             "3.6.8 (tag `v3.6.8`)")
        with patch.object(check, "ls_remote", return_value=[("sha", "refs/tags/curl-8_13_0")]):
            self.assertEqual(check.latest_release(row(version="8.12.1"), {"tag_prefix": "curl-"}),
                             "8.13.0 (tag `curl-8_13_0`)")

    def test_imgui_letter_patch(self):
        with patch.object(check, "ls_remote", return_value=[("sha", "refs/tags/v1.92.9b")]):
            self.assertIsNone(check.latest_release(row(version="1.92.9b"), {}))
            self.assertIsNotNone(check.latest_release(row(version="1.92.9"), {}))

    def test_commit_file_changes_and_branch_only(self):
        pinned = row(commit="abcdef0")
        cfg = {"branch": "release", "files": ["library.h"]}
        with patch.object(check, "ls_remote", return_value=[("fedcba098765", "refs/heads/release")]):
            with patch.object(check, "raw", return_value=b"same"):
                self.assertIsNone(check.commit_changes(pinned, cfg))
            with patch.object(check, "raw", side_effect=[b"old", b"new"]):
                self.assertIn("library.h", check.commit_changes(pinned, cfg))
            self.assertIn("fedcba0", check.commit_changes(pinned, {"branch": "release"}))
        with patch.object(check, "ls_remote", return_value=[("abcdef012345", "refs/heads/release")]), \
                patch.object(check, "raw", side_effect=AssertionError("must not read matching commit")):
            self.assertIsNone(check.commit_changes(pinned, cfg))

    def test_macros_and_network_failure(self):
        with patch.object(check, "raw", return_value=b"#define BCDEC_VERSION_MAJOR 0\n#define BCDEC_VERSION_MINOR 99\n"):
            self.assertEqual(check.check_row(row(name="bcdec", version="0.98"))[0], "newer")
        with patch.object(check, "ls_remote", side_effect=subprocess.TimeoutExpired("git", 60)):
            self.assertEqual(check.check_row(row())[0], "error")
        with patch.object(check, "ls_remote", return_value=[]):
            self.assertEqual(check.check_row(row())[0], "error")
        self.assertEqual(check.check_row(row(name="dr_mp3"))[0], "skip")

    def report(self, rows, states):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            body = Path(directory) / "report.md"
            key = Path(directory) / "report.key"
            args = ["check", "--root", str(ROOT), "--body", str(body), "--fingerprint", str(key)]
            with patch.object(sys, "argv", args), patch.object(check, "parse_list", return_value=rows), \
                    patch.object(check, "check_row", side_effect=states), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(check.main(), 0)
            return body.read_text(), key.read_text().strip()

    def test_fingerprint_tracks_set_not_release_text_or_order(self):
        a, b = row("a"), row("b")
        _, key = self.report([a, b], [("newer", "release 2"), ("newer", "tip 123")])
        _, same = self.report([b, a], [("newer", "tip 456"), ("newer", "release 3")])
        _, changed = self.report([a, b], [("current", None), ("newer", "tip 456")])
        self.assertEqual(key, same)
        self.assertNotEqual(key, changed)

    def test_empty_and_partial_reports(self):
        body, key = self.report([row()], [("current", None)])
        self.assertEqual(key, "none")
        self.assertIn("no newer", body)
        body, key = self.report([row()], [("error", "offline")])
        self.assertEqual(key, "incomplete")
        self.assertIn("Could not check", body)
        self.assertNotIn("Every checked entry", body)
        _, key = self.report([row("a"), row("b")], [("newer", "release 2"), ("error", "offline")])
        self.assertEqual(key, "incomplete")


class PublishingTests(unittest.TestCase):
    KEY = "0123456789abcdef"

    def run_publish(self, issues=(), comments=(), key=KEY, failure=None):
        calls = []

        def fake_gh(*args):
            calls.append(args)
            if args[0] == "api":
                if failure:
                    raise failure
                values = comments if "/comments" in args[2] else issues
                return "\n".join(json.dumps(v) for v in values)
            return ""

        with patch.object(publish, "gh", side_effect=fake_gh), contextlib.redirect_stdout(io.StringIO()):
            publish.publish("owner/repo", Path("report.md"), key)
        return [c for c in calls if c[0] == "issue"], calls

    def issue(self, state="open", key=KEY, title=publish.TITLE, number=12):
        return dict(number=number, title=title, state=state,
                    body="<!-- upstream-check: %s -->" % key if key else "legacy body")

    def test_create_and_no_updates(self):
        mutations, _ = self.run_publish()
        self.assertEqual(mutations[0][:2], ("issue", "create"))
        self.assertEqual(self.run_publish(key="none")[0], [])

    def test_same_set_and_latest_comment(self):
        self.assertEqual(self.run_publish([self.issue()])[0], [])
        mutations, calls = self.run_publish([self.issue(key="none")],
                                           [dict(body="unrelated"), dict(body=self.issue()["body"])])
        self.assertEqual(mutations, [])
        self.assertTrue(all("--paginate" in c for c in calls))

    def test_changed_set_legacy_and_now_current(self):
        for old, new in [("none", self.KEY), (None, self.KEY), (self.KEY, "none")]:
            with self.subTest(old=old, new=new):
                mutations, _ = self.run_publish([self.issue(key=old)], key=new)
                self.assertEqual([m[1] for m in mutations], ["comment"])

    def test_closed_issue_is_reused(self):
        mutations, _ = self.run_publish([self.issue(state="closed")])
        self.assertEqual([m[1] for m in mutations], ["reopen"])
        mutations, _ = self.run_publish([self.issue(state="closed", key="none")])
        self.assertEqual([m[1] for m in mutations], ["reopen", "comment"])
        self.assertEqual(self.run_publish([self.issue(state="closed")], key="none")[0], [])

    def test_exact_title_and_oldest_issue(self):
        issues = [self.issue(title=publish.TITLE + " follow-up", number=1),
                  self.issue(key="none", number=15), self.issue(key="none", number=12)]
        mutations, _ = self.run_publish(issues)
        self.assertEqual(mutations[0][:3], ("issue", "comment", "12"))

    def test_incomplete_does_not_publish_or_query(self):
        self.assertEqual(self.run_publish(key="incomplete"), ([], []))

    def test_lookup_failure_propagates_without_creating(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_publish(failure=subprocess.CalledProcessError(1, "gh"))

    def test_workflow_entry_writes_summary_and_validates_key(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            body, key, summary = [Path(directory) / n for n in ["body.md", "key", "summary.md"]]
            body.write_text(self.issue(key="incomplete")["body"])
            key.write_text("incomplete\n")
            env = dict(GITHUB_REPOSITORY="owner/repo", GITHUB_STEP_SUMMARY=str(summary),
                       RUN_URL="https://example.invalid/run/1")
            args = ["publish", "--body", str(body), "--fingerprint", str(key)]
            with patch.object(sys, "argv", args), patch.dict(os.environ, env), \
                    patch.object(publish, "gh", side_effect=AssertionError("no remote calls")), \
                    contextlib.redirect_stdout(io.StringIO()):
                publish.main()
                self.assertIn(env["RUN_URL"], summary.read_text())
                key.write_text(self.KEY)
                with self.assertRaisesRegex(ValueError, "disagree"):
                    publish.main()

    def test_publisher_cli_with_fake_gh(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            directory = Path(directory)
            fake = directory / "gh"
            fake.write_text("#!/usr/bin/env python3\n"
                            "import json, os, sys\n"
                            "with open(os.environ['FAKE_GH_LOG'], 'a') as f:\n"
                            "    f.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                            "if sys.argv[1] == 'api':\n"
                            "    data = '[]' if '/comments' in sys.argv[3] else os.environ['FAKE_GH_ISSUES']\n"
                            "    for item in json.loads(data):\n"
                            "        print(json.dumps(item))\n")
            fake.chmod(0o755)
            body, key, summary, log = [directory / n for n in ["body.md", "key", "summary.md", "gh.log"]]
            body.write_text(self.issue()["body"])
            key.write_text(self.KEY + "\n")
            env = dict(os.environ, PATH=str(directory) + os.pathsep + os.environ["PATH"],
                       GITHUB_REPOSITORY="owner/repo", GITHUB_STEP_SUMMARY=str(summary),
                       RUN_URL="https://example.invalid/run/1", FAKE_GH_LOG=str(log),
                       FAKE_GH_ISSUES=json.dumps([self.issue(key="none")]))
            result = subprocess.run([sys.executable, str(ROOT / "tools/update_upstream_issue.py"),
                                     "--body", str(body), "--fingerprint", str(key)],
                                    env=env, capture_output=True, text=True, check=True)
            self.assertEqual(result.returncode, 0)
            calls = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(calls[-1][:3], ["issue", "comment", "12"])
            self.assertEqual(calls[-1][-2:], ["--body-file", str(body)])
            self.assertIn(env["RUN_URL"], summary.read_text())


if __name__ == "__main__":
    unittest.main()
