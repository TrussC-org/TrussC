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
| **earcut.hpp** | commit `365be263bfed498f9d3d3bf065bcb39b0d0e4ba6` (untagged; 16 commits after v2.2.4 and 16 before v3.2.3) | `core/include/earcut/earcut.hpp` | https://github.com/mapbox/earcut.hpp | ISC | Mapbox |
| **LuaJIT** | 2.1 (rolling), commit not recorded | `addons/tcxLua/LuaJIT/` | https://github.com/LuaJIT/LuaJIT | MIT | Mike Pall |
| **branchlib** | branch `x` (not pinned) | `branchlib.h` | https://github.com/owner/branchlib | MIT | Someone |

### Build Tools

| Library | Version | Pinned / vendored in | Upstream | License | Author/Organization | SHA-256 |
|---------|---------|----------------------|----------|---------|---------------------|---------|
| **sokol-shdc (sokol-tools-bin)** | commit `2428a421bb4c2d765c43cb77167b6e1d6e0d498f` (2026-11-10T18:18:51Z) | `core/cmake/trussc_shaders.cmake` | https://github.com/floooh/sokol-tools-bin | zlib License | Andre Weissflog | linux: `f1a5b91e55696d4e4f615b5d1cf524ac2636fc9a052dac6d568d448e2af86b39` |

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
                # The CA bundle comes from curl.se, not a GitHub repository.
                self.assertTrue(r["upstream"].startswith("https://"))
                self.assertTrue(r["paths"])
                self.assertTrue(r["version"] or r["commit"] or r["branch"] or r["not_recorded"])
                for p in r["paths"]:
                    self.assertTrue((ROOT / p).exists(), p)

    def test_parse_list_values(self):
        by_name = {r["name"]: r for r in check.parse_list(FIXTURE_LIST)}
        self.assertEqual(set(by_name), {"earcut.hpp", "LuaJIT", "branchlib",
                                        "sokol-shdc (sokol-tools-bin)", "mbedTLS", "libcurl",
                                        "tinyobjloader", "HAP"})
        self.assertFalse(by_name["earcut.hpp"]["not_recorded"])
        self.assertIsNone(by_name["earcut.hpp"]["version"])
        self.assertEqual(by_name["earcut.hpp"]["commit"], "365be263bfed498f9d3d3bf065bcb39b0d0e4ba6")
        self.assertEqual(by_name["LuaJIT"]["version"], "2.1")
        self.assertTrue(by_name["LuaJIT"]["not_recorded"])
        self.assertEqual(by_name["branchlib"]["branch"], "x")
        self.assertFalse(by_name["branchlib"]["not_recorded"])
        self.assertEqual(by_name["sokol-shdc (sokol-tools-bin)"]["hashes"],
                         {"linux": "f1a5b91e55696d4e4f615b5d1cf524ac2636fc9a052dac6d568d448e2af86b39"})
        self.assertEqual(by_name["mbedTLS"]["version"], "3.6.7")
        self.assertEqual(by_name["mbedTLS"]["slug"], "mbed-tls/mbedtls")
        self.assertEqual(by_name["libcurl"]["version"], "8.12.1")
        self.assertEqual(by_name["tinyobjloader"]["commit"], "966edce")
        self.assertIsNone(by_name["tinyobjloader"]["version"])
        self.assertEqual(by_name["HAP"]["commit"], "d847f6bbd3be88575dd4ef33a877243780e3be76")
        self.assertEqual(by_name["sokol-shdc (sokol-tools-bin)"]["commit"],
                         "2428a421bb4c2d765c43cb77167b6e1d6e0d498f")
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

    def test_box2d_v2_is_frozen_while_v3_tracks_releases(self):
        tags = ["v2.4.2", "v2.4.3", "v3.2.0", "v3.2.1"]
        with patch.object(check, "ls_remote", return_value=[("sha", "refs/tags/" + t) for t in tags]):
            self.assertEqual(check.latest_release(row(name="Box2D", version="2.4.2"), check.UPSTREAM["Box2D"]),
                             "2.4.3 (tag `v2.4.3`)")
            self.assertIsNone(check.latest_release(row(name="Box2D", version="2.4.3"), check.UPSTREAM["Box2D"]))
            self.assertEqual(check.latest_release(row(name="Box2D v3", version="3.2.0"), check.UPSTREAM.get("Box2D v3", {})),
                             "3.2.1 (tag `v3.2.1`)")

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
        cfg = {"file": "library.h", "macros": ["LIB_VERSION_MAJOR", "LIB_VERSION_MINOR"]}
        with patch.dict(check.UPSTREAM, {"library": cfg}), \
                patch.object(check, "raw", return_value=b"#define LIB_VERSION_MAJOR 1\n#define LIB_VERSION_MINOR 3\n"):
            self.assertEqual(check.check_row(row(version="1.2"))[0], "newer")
        with patch.object(check, "ls_remote", side_effect=subprocess.TimeoutExpired("git", 60)):
            self.assertEqual(check.check_row(row())[0], "error")
        with patch.object(check, "ls_remote", return_value=[]):
            self.assertEqual(check.check_row(row())[0], "error")
        self.assertEqual(check.check_row(row(name="dr_mp3"))[0], "skip")

    def report(self, rows, states, osv=None):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            body = Path(directory) / "report.md"
            key = Path(directory) / "report.key"
            args = ["check", "--root", str(ROOT), "--body", str(body), "--fingerprint", str(key)]
            with patch.object(sys, "argv", args), patch.object(check, "parse_list", return_value=rows), \
                    patch.object(check, "check_row", side_effect=states), \
                    patch.object(check, "check_osv", return_value=osv if osv is not None else []), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(check.main(), 0)
            return body.read_text(encoding="utf-8"), key.read_text(encoding="utf-8").strip()

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


    def test_osv_inventory_has_explicit_mapping_for_every_entry(self):
        rows = check.parse_list((ROOT / check.LIST_FILE).read_text(encoding="utf-8"))
        self.assertEqual(set(check.OSV), {r["name"] for r in rows})

    def test_osv_one_bounded_batch_and_safe_report(self):
        current = row("earcut.hpp", version=None, commit="a" * 40)
        fixture = ROOT / "tools/tests/fixtures/osv/hit.json"
        response = io.BytesIO(fixture.read_bytes())
        with patch.object(check.urllib.request, "urlopen", return_value=response) as call:
            results = check.check_osv([current, row("dr_mp3")])
        call.assert_called_once()
        request = call.call_args.args[0]
        self.assertEqual(request.full_url, check.OSV_URL)
        self.assertEqual(request.method, "POST")
        self.assertEqual(json.loads(request.data), {"queries": [{"commit": "a" * 40}]})
        self.assertEqual(call.call_args.kwargs["timeout"], check.OSV_TIMEOUT)
        report = "\n".join(check.osv_report(results))
        self.assertIn("[TEST-OSV-1](https://osv.dev/vulnerability/TEST-OSV-1)", report)
        self.assertIn("dr_mp3: not checkable (", report)
        self.assertNotIn("PRIVATE_FIXTURE_TEXT", report)

    def test_osv_no_hit_and_unmatchable(self):
        current = row("earcut.hpp", version=None, commit="a" * 40)
        response = io.BytesIO((ROOT / "tools/tests/fixtures/osv/no-hit.json").read_bytes())
        with patch.object(check.urllib.request, "urlopen", return_value=response):
            results = check.check_osv([current])
        self.assertEqual(results, [(current, "checked", [])])
        self.assertIn("No OSV matches", "\n".join(check.osv_report(results)))
        with patch.object(check.urllib.request, "urlopen", side_effect=AssertionError("must stay offline")):
            results = check.check_osv([row("unknown"), row("LuaJIT")])
        self.assertTrue(all(state == "skip" for _, state, _ in results))
        self.assertIn("unknown: not checkable (no OSV mapping", "\n".join(check.osv_report(results)))

    def test_osv_errors_never_claim_clean_results(self):
        current = row("earcut.hpp", version=None, commit="a" * 40)
        fixture = ROOT / "tools/tests/fixtures/osv/error.json"
        responses = [TimeoutError("PRIVATE_FIXTURE_TEXT"), fixture.read_bytes(),
                     b'{"results": []}', b'{"results": [{"next_page_token": "more"}]}',
                     b'{"results": [{"vulns": [{"id": "bad|id"}]}]}', b'not json']
        for value in responses:
            with self.subTest(value=value):
                kwargs = {"side_effect": value} if isinstance(value, Exception) else {"return_value": io.BytesIO(value)}
                with patch.object(check.urllib.request, "urlopen", **kwargs):
                    results = check.check_osv([current])
                self.assertEqual(results[0][1], "error")
                report, key = self.report([current], [("current", None)], osv=results)
                self.assertEqual(key, "incomplete")
                self.assertIn("OSV check failed", report)
                self.assertNotIn("No OSV matches", report)
                self.assertNotIn("PRIVATE_FIXTURE_TEXT", report)

    def test_osv_resolves_tags_and_short_commits_to_full_sha(self):
        for current, ref in [(row("libcurl", version="8.23.0"), "curl-8_23_0"),
                             (row("stb_image", commit="abcdef0"), "abcdef0")]:
            with self.subTest(ref=ref), patch.object(check.urllib.request, "urlopen",
                    return_value=io.BytesIO(json.dumps({"sha": "b" * 40}).encode())) as call:
                self.assertEqual(check.osv_commit(current, check.OSV[current["name"]]), "b" * 40)
                self.assertEqual(call.call_args.args[0].full_url,
                                 "https://api.github.com/repos/owner/library/commits/" + ref)
                self.assertEqual(call.call_args.kwargs["timeout"], check.OSV_TIMEOUT)
        with patch.object(check.urllib.request, "urlopen", side_effect=TimeoutError):
            results = check.check_osv([row("libcurl")])
        self.assertEqual(results[0][1], "error")

    def test_osv_fingerprint_tracks_advisories_and_version(self):
        current = row("earcut.hpp", version=None, commit="a" * 40)
        def report(ids, pin=current):
            return self.report([pin], [("current", None)], [(pin, "checked", ids)])[1]
        key = report(["TEST-OSV-1"])
        self.assertNotEqual(key, "none")
        self.assertEqual(key, report(["TEST-OSV-1"]))
        self.assertNotEqual(key, report(["TEST-OSV-2"]))
        self.assertNotEqual(key, report(["TEST-OSV-1"], dict(current, commit="b" * 40)))
        self.assertEqual(report([]), "none")

    def test_earcut_checks_only_vendored_upstream_file(self):
        current = next(r for r in check.parse_list(FIXTURE_LIST) if r["name"] == "earcut.hpp")
        with patch.object(check, "ls_remote", return_value=[("a" * 40, "refs/heads/master")]), \
                patch.object(check, "raw", side_effect=[b"old", b"new"]) as raw:
            self.assertEqual(check.check_row(current)[0], "newer")
        self.assertEqual([c.args[2] for c in raw.call_args_list], ["include/mapbox/earcut.hpp"] * 2)

    def test_release_osv_only_does_not_read_upstreams(self):
        current = row("earcut.hpp", version=None, commit="a" * 40)
        with patch.object(sys, "argv", ["check", "--osv-only"]), \
                patch.object(check, "parse_list", return_value=[current]), \
                patch.object(check, "check_row", side_effect=AssertionError("no upstream version reads")), \
                patch.object(check, "check_osv", return_value=[(current, "checked", [])]), \
                contextlib.redirect_stdout(io.StringIO()) as output, \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(check.main(), 0)
        self.assertIn("OSV release check", output.getvalue())
        self.assertIn("No OSV matches", output.getvalue())
        self.assertNotIn("no newer upstream", output.getvalue())

    def test_release_check_runs_same_osv_tool_and_records_summary(self):
        import yaml
        workflow = yaml.load((ROOT / ".github/workflows/release-check.yml").read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
        steps = workflow["jobs"]["osv-check"]["steps"]
        self.assertIn("--osv-only --body osv.md", steps[2]["run"])
        self.assertIn('cat osv.md >> "$GITHUB_STEP_SUMMARY"', steps[3]["run"])


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
            body.write_text(self.issue(key="incomplete")["body"], encoding="utf-8")
            key.write_text("incomplete\n", encoding="utf-8")
            env = dict(GITHUB_REPOSITORY="owner/repo", GITHUB_STEP_SUMMARY=str(summary),
                       RUN_URL="https://example.invalid/run/1")
            args = ["publish", "--body", str(body), "--fingerprint", str(key)]
            with patch.object(sys, "argv", args), patch.dict(os.environ, env), \
                    patch.object(publish, "gh", side_effect=AssertionError("no remote calls")), \
                    contextlib.redirect_stdout(io.StringIO()):
                publish.main()
                self.assertIn(env["RUN_URL"], summary.read_text(encoding="utf-8"))
                key.write_text(self.KEY, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "disagree"):
                    publish.main()

    @unittest.skipIf(sys.platform == "win32", "fake gh requires POSIX shebang execution")
    def test_publisher_cli_with_fake_gh(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            directory = Path(directory)
            fake = directory / "gh"
            fake.write_text("#!/usr/bin/env python3\n"
                            "import json, os, sys\n"
                            "with open(os.environ['FAKE_GH_LOG'], 'a', encoding='utf-8') as f:\n"
                            "    f.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                            "if sys.argv[1] == 'api':\n"
                            "    data = '[]' if '/comments' in sys.argv[3] else os.environ['FAKE_GH_ISSUES']\n"
                            "    for item in json.loads(data):\n"
                            "        print(json.dumps(item))\n", encoding="utf-8")
            fake.chmod(0o755)
            body, key, summary, log = [directory / n for n in ["body.md", "key", "summary.md", "gh.log"]]
            body.write_text(self.issue()["body"], encoding="utf-8")
            key.write_text(self.KEY + "\n", encoding="utf-8")
            env = dict(os.environ, PATH=str(directory) + os.pathsep + os.environ["PATH"],
                       GITHUB_REPOSITORY="owner/repo", GITHUB_STEP_SUMMARY=str(summary),
                       RUN_URL="https://example.invalid/run/1", FAKE_GH_LOG=str(log),
                       FAKE_GH_ISSUES=json.dumps([self.issue(key="none")]))
            result = subprocess.run([sys.executable, str(ROOT / "tools/update_upstream_issue.py"),
                                     "--body", str(body), "--fingerprint", str(key)],
                                    env=env, capture_output=True, text=True, check=True, encoding="utf-8", errors="replace")
            self.assertEqual(result.returncode, 0)
            calls = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(calls[-1][:3], ["issue", "comment", "12"])
            self.assertEqual(calls[-1][-2:], ["--body-file", str(body)])
            self.assertIn(env["RUN_URL"], summary.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
