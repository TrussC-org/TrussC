"""Offline tests for the per-PR dependency inventory gate."""

import copy
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("dependencies", ROOT / "tools/check_dependencies.py")
deps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deps)


class DependencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = deps.load(str(ROOT))
        cls.cmake = deps.tracked_cmake_files(str(ROOT))

    def errors(self, rows=None, changes=None):
        original = deps.read
        changes = changes or {}

        def read(root, path):
            return changes[path] if path in changes else original(root, path)

        with patch.object(deps, "read", side_effect=read), \
                patch.object(deps, "tracked_cmake_files", return_value=self.cmake):
            return deps.check(str(ROOT), self.rows if rows is None else rows)

    def change_row(self, name, **values):
        rows = copy.deepcopy(self.rows)
        next(r for r in rows if r["name"] == name).update(values)
        return rows

    def test_real_tree_and_selftest(self):
        self.assertEqual(len(self.rows), 31)
        self.assertEqual(deps.check(str(ROOT), self.rows), [])
        self.assertEqual(deps.selftest(str(ROOT), self.rows), [])

    def test_each_known_library_requires_a_row(self):
        for victim in self.rows:
            with self.subTest(library=victim["name"]):
                rows = [r for r in self.rows if r["name"] != victim["name"]]
                errors = self.errors(rows)
                self.assertTrue(any("no row" in e for e in errors), victim["name"])

    def test_release_tag_and_archive_url_mismatches(self):
        cases = [
            ("mbedTLS", "addons/tcxTls/CMakeLists.txt", "v3.6.7", "v3.6.8"),
            ("libcurl", "addons/tcxCurl/CMakeLists.txt", "8_22_0", "8_23_0"),
            ("libremidi", "addons/tcxMidi/CMakeLists.txt", "v5.4.3", "v5.4.4"),
        ]
        for name, path, old, new in cases:
            with self.subTest(library=name):
                changed = deps.read(str(ROOT), path).replace(old, new)
                self.assertTrue(any(name in e and "fetches" in e for e in self.errors(changes={path: changed})))

    def test_pinned_hap_and_shader_commits(self):
        for name, path in [("HAP", "addons/tcxHap/CMakeLists.txt"),
                           (deps.SHADER_COMPILER, deps.SHADER_CMAKE)]:
            current = next(r for r in self.rows if r["name"] == name)
            self.assertEqual(len(current["commit"]), 40)
            self.assertIsNone(current["branch"])
            for pin in ["0" * 40, "master"]:
                with self.subTest(library=name, pin=pin):
                    source = deps.read(str(ROOT), path).replace(current["commit"], pin)
                    self.assertTrue(any(name in e and "commit" in e for e in self.errors(changes={path: source})))
            self.assertTrue(any(name in e and "fetches" in e for e in
                                self.errors(self.change_row(name, commit="0" * 40))))

    def test_shader_host_hash_mismatches_missing_and_invalid(self):
        current = next(r for r in self.rows if r["name"] == deps.SHADER_COMPILER)
        source = deps.read(str(ROOT), deps.SHADER_CMAKE)
        for host, digest in current["hashes"].items():
            for replacement in ["0" * 64, "too-short"]:
                with self.subTest(host=host, replacement=replacement):
                    errors = self.errors(changes={deps.SHADER_CMAKE: source.replace(digest, replacement)})
                    self.assertTrue(any(host in e and "SHA-256" in e for e in errors))
            hashes = dict(current["hashes"], **{host: "0" * 64})
            self.assertTrue(any(host in e and "SHA-256" in e for e in
                                self.errors(self.change_row(deps.SHADER_COMPILER, hashes=hashes))))
            del hashes[host]
            self.assertTrue(any("all five hosts" in e for e in
                                self.errors(self.change_row(deps.SHADER_COMPILER, hashes=hashes))))
            declaration = 'set(_TC_SOKOL_SHDC_SHA256_%s "%s")' % (host, digest)
            self.assertIn(declaration, source)
            self.assertTrue(any(host in e and "missing" in e for e in
                                self.errors(changes={deps.SHADER_CMAKE: source.replace(declaration, "")})))

    def test_shader_upstream_pin_path_and_comments(self):
        source = deps.read(str(ROOT), deps.SHADER_CMAKE)
        for old, new in [("floooh/sokol-tools-bin/", "other/wrong/"),
                         ("${_TC_SOKOL_SHDC_COMMIT}/bin", "master/bin")]:
            self.assertIn(old, source)
            self.assertTrue(any("upstream URL" in e for e in
                                self.errors(changes={deps.SHADER_CMAKE: source.replace(old, new)})))
        self.assertTrue(any("does not include" in e for e in
                            self.errors(self.change_row(deps.SHADER_COMPILER, paths=["core/CMakeLists.txt"]))))
        commented = ('# set(_TC_SOKOL_SHDC_COMMIT "wrong")\n'
                     '#[=[set(_TC_SOKOL_SHDC_SHA256_linux "wrong")]=]\n'
                     'message([=[set(_TC_SOKOL_SHDC_COMMIT "wrong")]=])\n')
        self.assertEqual(self.errors(changes={deps.SHADER_CMAKE: commented + source}), [])
        self.assertTrue(any("incomplete set" in e for e in
                            self.errors(changes={deps.SHADER_CMAKE: source + '\nset(broken "value"'})))

    def test_url_hash_parsing_and_inventory_comparison(self):
        path = "addons/tcxMidi/CMakeLists.txt"
        source = deps.read(str(ROOT), path)
        args = deps.parse_fetch_declares(source)[0][1]
        digest = args["URL_HASH"].removeprefix("SHA256=")
        rows = self.change_row("libremidi", hashes={"archive": digest})
        self.assertEqual(self.errors(rows), [])
        for replacement in ["SHA256=" + "0" * 64, "SHA256=abc", "MD5=" + "0" * 32, ""]:
            with self.subTest(hash=replacement):
                source_change = source.replace("URL_HASH " + args["URL_HASH"],
                                               "URL_HASH " + replacement if replacement else "")
                errors = self.errors(rows, {path: source_change})
                self.assertTrue(any("libremidi" in e and "URL_HASH" in e for e in errors))
        text = '''\n## Third-Party Libraries\n
| Library | Version | Pinned / vendored in | Upstream | SHA-256 |
|---|---|---|---|---|
| archive | commit `abcdef0123456789abcdef0123456789abcdef01` | `CMakeLists.txt` | https://github.com/owner/archive | `%s` |
''' % digest
        self.assertEqual(deps.parse_list(text)[0]["hashes"], {"archive": digest})
        self.assertEqual(deps.tag_from_url("https://github.com/owner/archive/archive/abcdef0123456789abcdef0123456789abcdef01.tar.gz"),
                         "abcdef0123456789abcdef0123456789abcdef01")

    def test_commit_archive_with_url_hash(self):
        path = "addons/tcxHap/CMakeLists.txt"
        source = deps.read(str(ROOT), path)
        current = next(r for r in self.rows if r["name"] == "HAP")
        digest = "a" * 64
        source = source.replace("GIT_REPOSITORY https://github.com/Vidvox/hap.git",
                                "URL https://github.com/Vidvox/hap/archive/%s.tar.gz\n    URL_HASH SHA256=%s" %
                                (current["commit"], digest))
        source = source.replace("GIT_TAG " + current["commit"], "")
        rows = self.change_row("HAP", hashes={"archive": digest})
        self.assertEqual(self.errors(rows, {path: source}), [])
        for changed, expected in [(source.replace(current["commit"], "0" * 40), "fetches"),
                                  (source.replace(digest, "b" * 64), "URL_HASH"),
                                  (source.replace("URL_HASH SHA256=" + digest, "URL_HASH"), "URL_HASH")]:
            self.assertTrue(any("HAP" in e and expected in e for e in self.errors(rows, {path: changed})))

    def test_vendored_versions_and_provenance_mismatches(self):
        for name in deps.VENDORED_VERSIONS:
            with self.subTest(library=name):
                errors = self.errors(self.change_row(name, version="0.0.0"))
                self.assertTrue(any(name in e and "says" in e for e in errors))
        for name in deps.PROVENANCE_COMMITS:
            with self.subTest(provenance=name):
                errors = self.errors(self.change_row(name, commit="0000000"))
                self.assertTrue(any(name in e and "commit" in e and "says" in e for e in errors))

    def test_both_vendored_lua_copies_and_miniaudio_macros_are_checked(self):
        for path, old, new in [
            ("addons/tcxLua/lua/include/lua.h", '#define LUA_VERSION_RELEASE\t"8"', '#define LUA_VERSION_RELEASE\t"9"'),
            ("addons/tcxLua/lua/src/lua.h", '#define LUA_VERSION_RELEASE\t"8"', '#define LUA_VERSION_RELEASE\t"9"'),
            ("core/include/miniaudio.h", "#define MA_VERSION_REVISION 23", "#define MA_VERSION_REVISION 24"),
        ]:
            with self.subTest(path=path):
                source = deps.read(str(ROOT), path)
                self.assertIn(old, source)
                self.assertTrue(any(path in e and "says" in e for e in self.errors(changes={path: source.replace(old, new)})))

    def test_missing_and_unrelated_paths(self):
        for name, path, expected in [
            ("miniaudio", "missing/header.h", "does not exist"),
            ("miniaudio", "core/include/stb/stb_image.h", "does not include"),
            ("tinyobjloader", "core/include/stb/stb_image.h", "does not include"),
        ]:
            with self.subTest(library=name, path=path):
                self.assertTrue(any(name in e and expected in e for e in self.errors(self.change_row(name, paths=[path]))))

    def test_duplicate_and_ambiguous_rows(self):
        rows = copy.deepcopy(self.rows)
        rows.append(copy.deepcopy(next(r for r in rows if r["name"] == "mbedTLS")))
        errors = self.errors(rows)
        self.assertTrue(any("more than one row" in e for e in errors))
        self.assertTrue(any("matches multiple rows" in e for e in errors))

    def test_removed_declaration_in_shared_file_cannot_hide_behind_another(self):
        path = "addons/tcxHap/CMakeLists.txt"
        text = deps.read(str(ROOT), path)
        start = text.index("FetchContent_Declare(\n    hap_source")
        end = text.index(")", start) + 1
        errors = self.errors(changes={path: text[:start] + text[end:]})
        self.assertTrue(any("HAP" in e and "no matching FetchContent_Declare" in e for e in errors))

    def test_non_github_upstream_is_not_a_wildcard(self):
        path = "addons/tcxTls/CMakeLists.txt"
        text = deps.read(str(ROOT), path).replace("https://github.com/Mbed-TLS/mbedtls.git", "https://example.invalid/wrong/library.git")
        self.assertTrue(any("has no row" in e for e in self.errors(changes={path: text})))
        row = dict(upstream="https://example.invalid/owner/library", slug=None)
        self.assertTrue(deps.same_upstream("https://example.invalid/owner/library.git", row))
        self.assertFalse(deps.same_upstream("https://example.invalid/owner/library-other.git", row))

    def test_branch_mismatch_and_missing_pin(self):
        self.assertTrue(any("branch `wrong`" in e for e in self.errors(self.change_row("HAP", branch="wrong"))))
        path = "addons/tcxTls/CMakeLists.txt"
        text = deps.read(str(ROOT), path).replace("GIT_TAG v3.6.7", "")
        self.assertTrue(any("no GIT_TAG" in e for e in self.errors(changes={path: text})))

    def test_comments_case_and_literal_arguments(self):
        text = '''# FetchContent_Declare(commented GIT_TAG wrong)
#[=[
FetchContent_Declare(bracket_comment GIT_TAG wrong)
]=]
message("FetchContent_Declare(string GIT_TAG wrong)")
message([=[FetchContent_Declare(bracket_string GIT_TAG wrong)]=])
fetchcontent_declare(real
    GIT_REPOSITORY "https://github.com/owner/library.git"
    GIT_TAG [=[v1.2.3]=]
    PATCH_COMMAND "a # literal (parenthesis)"
)
'''
        self.assertEqual(deps.parse_fetch_declares(text), [
            ("real", {"GIT_REPOSITORY": "https://github.com/owner/library.git", "GIT_TAG": "v1.2.3"}, 7)])

    def test_malformed_declaration_fails(self):
        path = "addons/tcxTls/CMakeLists.txt"
        errors = self.errors(changes={path: "FetchContent_Declare(broken GIT_TAG v1.2.3"})
        self.assertTrue(any(path in e and "incomplete FetchContent_Declare" in e for e in errors))

    def test_short_commit_is_not_a_branch_prefix(self):
        path = "addons/tcxTls/CMakeLists.txt"
        text = deps.read(str(ROOT), path)
        for pin, should_fail in [("abcdef012345", False), ("abc", True), ("abcdef0-branch", True), ("1234567", True)]:
            with self.subTest(pin=pin):
                rows = self.change_row("mbedTLS", version=None, commit="abcdef0")
                errors = self.errors(rows, {path: text.replace("v3.6.7", pin)})
                self.assertEqual(any("fetches" in e for e in errors), should_fail)

    def test_untracked_cmake_files_are_scanned_without_staging(self):
        with tempfile.TemporaryDirectory(dir=ROOT, prefix="dependency-fixture-") as directory:
            path = Path(directory) / "CMakeLists.txt"
            path.write_text('fetchcontent_declare(new_library GIT_REPOSITORY https://github.com/new/library.git GIT_TAG v1.2.3)\n')
            relative = path.relative_to(ROOT).as_posix()
            self.assertIn(relative, deps.tracked_cmake_files(str(ROOT)))
            errors = deps.check(str(ROOT), self.rows)
            self.assertTrue(any(relative in e and "has no row" in e for e in errors))

    def test_cli_success_and_combined_mismatches_in_isolated_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {deps.LIST_FILE}
            for row in self.rows:
                for path in row["paths"]:
                    if (ROOT / path).is_dir():
                        (root / path).mkdir(parents=True, exist_ok=True)
                    else:
                        files.add(path)
            files.update(path for sources in deps.VENDORED_VERSIONS.values() for path, _ in sources)
            files.update(path for path, _ in deps.PROVENANCE_COMMITS.values())
            for path in files:
                destination = root / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / path, destination)

            def run():
                return subprocess.run([sys.executable, str(ROOT / "tools/check_dependencies.py"), "--root", directory],
                                      capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))

            good = run()
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            self.assertIn("31 third-party entries", good.stdout)
            license_path = root / deps.LIST_FILE
            inventory = license_path.read_text().replace("| 0.11.23 |", "| 0.11.21 |")
            shader_row = next(r for r in self.rows if r["name"] == deps.SHADER_COMPILER)
            inventory = inventory.replace(shader_row["commit"], "0" * 40)
            inventory = inventory.replace(shader_row["hashes"]["linux"], "0" * 64)
            inventory = "\n".join(line for line in inventory.splitlines() if not line.startswith("| **cgltf**")) + "\n"
            license_path.write_text(inventory)
            for path, old, new in [
                ("addons/tcxTls/CMakeLists.txt", "v3.6.7", "v3.6.8"),
                ("addons/tcxCurl/CMakeLists.txt", "8_22_0", "8_23_0"),
                ("core/include/stb/README.md", "6e9f34d", "0000000"),
            ]:
                target = root / path
                target.write_text(target.read_text().replace(old, new))
            bad = run()
            self.assertEqual(bad.returncode, 1, bad.stdout + bad.stderr)
            for expected in ["mbedTLS", "miniaudio", "FetchContent_Declare(cgltf)", "stb_truetype", "libcurl",
                             deps.SHADER_COMPILER, "SHA-256 for linux", "build fetches"]:
                self.assertIn(expected, bad.stdout)
            self.assertNotIn("match the build.", bad.stdout)

    def test_merge_preserves_all_header_job_checks(self):
        workflow = yaml.load((ROOT / ".github/workflows/build.yml").read_text(), Loader=yaml.BaseLoader)
        job = workflow["jobs"]["header-state-check"]
        commands = [step for step in job["steps"] if "run" in step]
        for command in ["python3 tools/check_header_state.py", "python3 tools/check_core_logging.py",
                        "python3 tools/test_core_test_runner.py", "python3 tools/tests/test_release_workflow.py -v",
                        "python3 -m unittest discover -s tools/tests -p test_dependencies.py -v",
                        "python3 tools/check_dependencies.py"]:
            matches = [step for step in commands if command in step["run"].splitlines()]
            self.assertEqual(len(matches), 1, command)
            if "check_header_state.py" not in command:
                self.assertEqual(matches[0]["if"], "always()")
        self.assertIn("header-state-check", workflow["jobs"]["ci-ok"]["needs"])


if __name__ == "__main__":
    unittest.main()
