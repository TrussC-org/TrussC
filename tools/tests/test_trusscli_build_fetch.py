"""Build/run confirmation against local file:// repositories; no network required.

Run through tcbuild.sh, with TRUSSCLI pointing to this tree's compiled CLI.
TRUSSCLI_BEFORE selects cached-output comparisons with an older binary.
"""
import errno
import json
import os
import select
import signal
import subprocess
import time
import unittest

from test_trusscli_versions import VersionTests, CLI, NATIVE
from test_trusscli_addon_versions import AddonVersionTests


class FetchCases:
    def cli(self, *args, success=True):
        result = subprocess.run([str(CLI), *map(str, args)], cwd=self.project, env=self.env,
                                input="", capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=60)
        self.last_result = result
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
            self.assertIn("Error:", output)
        return output

    def terminal(self, command="build", answer=b"\n"):
        import pty
        pid, terminal = pty.fork()
        if pid == 0:
            os.chdir(self.project)
            os.execve(str(CLI), [str(CLI), command, "-j1"], self.env)
        output = b""
        ended = 0
        status = None
        sent = False
        closed = False
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                if not closed and select.select([terminal], [], [], 0.05)[0]:
                    try:
                        data = os.read(terminal, 4096)
                        output += data
                        closed = not data
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        closed = True
                if b"Download now? [Y/n]" in output and not sent:
                    os.write(terminal, answer)
                    sent = True
                if not ended:
                    ended, status = os.waitpid(pid, os.WNOHANG)
                if ended and closed:
                    return os.waitstatus_to_exitcode(status), output.decode("utf-8", errors="replace")
            self.fail("PTY command did not complete: " + repr(output))
        finally:
            os.close(terminal)
            if not ended:
                os.killpg(pid, signal.SIGKILL)
                os.waitpid(pid, 0)

    def assert_fetched(self, output):
        self.assertIn("Not in cache:\n  " + self.identity, output.replace("\r\n", "\n"))
        self.assertTrue(self.cached.is_dir(), output)

    def test_fetch_terminal_return(self):
        code, output = self.terminal()
        self.assertEqual(code, 0, output)
        self.assertEqual(output.count("Download now? [Y/n]"), 1, output)
        self.assert_fetched(output)
        self.assertTrue(self.built.is_file(), output)

    def test_fetch_terminal_no(self):
        code, output = self.terminal(answer=b"n\n")
        self.assertEqual(code, 1, output)
        self.assertIn("version is unavailable; run `trusscli update`", output)
        self.assertFalse(self.cached.exists())

    def test_fetch_nonterminal_requires_yes(self):
        output = self.cli("build", success=False)
        self.assertIn("--yes", output)
        self.assertIn("version is unavailable; run `trusscli update`", output)
        self.assertNotIn("Download now?", output)
        self.assertFalse(self.cached.exists())

    def test_fetch_nonterminal_yes(self):
        output = self.cli("build", "--yes", "-j1")
        self.assert_fetched(output)
        self.assertNotIn("Download now?", output)
        self.assertTrue(self.built.is_file(), output)

    def test_fetch_cached_has_no_prompt_and_same_output(self):
        self.cli("update")
        plain = self.cli("build", "--dry-run", "-j1")
        automated = self.cli("build", "--dry-run", "-y", "-j1")
        self.assertEqual(plain, automated)
        self.assertNotIn("Not in cache", plain)
        self.assertNotIn("Download now?", plain)
        before = os.environ.get("TRUSSCLI_BEFORE")
        if before:
            result = subprocess.run([before, "build", "--dry-run", "-j1"], cwd=self.project, env=self.env,
                                    input="", capture_output=True, text=True, encoding="utf-8", timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(plain, result.stdout + result.stderr)


@unittest.skipUnless(os.name == "posix", "PTY checks require POSIX")
class VersionFetchTests(FetchCases, VersionTests):
    def setUp(self):
        super().setUp()
        recipe = self.source / "core/cmake/trussc_app.cmake"
        recipe.write_text('macro(trussc_app)\nadd_executable(fixture src/main.cpp)\n'
                          'set_target_properties(fixture PROPERTIES OUTPUT_NAME project '
                          'RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")\n'
                          'if(APPLE)\nset_target_properties(fixture PROPERTIES MACOSX_BUNDLE TRUE)\nendif()\nendmacro()\n', encoding="utf-8")
        self.git("-C", self.source, "add", ".")
        self.git("-C", self.source, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-m", "runnable fixture")
        remote = self.base / "remotes/TrussC-org/TrussC.git"
        self.git("-C", remote, "fetch", str(self.source), "+refs/heads/main:refs/heads/main")
        (self.project / ".trussc-version").write_text("main\n", encoding="utf-8")
        self.identity = "TrussC-org/TrussC@main"
        self.cached = self.cache / "versions/TrussC-org/TrussC/main"
        self.built = self.project / ("bin/project.app/Contents/MacOS/project"
                                     if NATIVE == "macos" else "bin/project")

    def test_fetch_dry_run_never_changes_project_or_cache(self):
        build = self.project / f"build-{NATIVE}"
        build.mkdir()
        (build / "keep").write_text("keep", encoding="utf-8")
        before = {p.relative_to(self.project): p.read_bytes()
                  for p in self.project.rglob("*") if p.is_file()}
        for flags in ((), ("--yes",)):
            with self.subTest(flags=flags):
                output = self.cli("build", "--dry-run", "--clean", *flags, success=False)
                self.assertIn(self.identity, output)
                self.assertNotIn("Download now?", output)
                self.assertNotIn("Project updated:", output)
                self.assertFalse(self.cached.exists())
                self.assertEqual(before, {p.relative_to(self.project): p.read_bytes()
                                         for p in self.project.rglob("*") if p.is_file()})

    def test_fetch_addon_selection_has_size_bound(self):
        (self.project / "addons.make").write_bytes(b"#" + b"x" * (16 * 1024 * 1024))
        output = self.cli("build", "--yes", success=False)
        self.assertIn("addons.make exceeds the 16777216-byte limit", output)
        self.assertNotIn("Download now?", output)
        self.assertFalse(self.cached.exists())

    def test_fetch_run_asks_once_then_launches(self):
        (self.project / "src/main.cpp").write_text('#include <cstdio>\nint main() { puts("FETCH_APP_RAN"); }\n', encoding="utf-8")
        code, output = self.terminal("run")
        self.assertEqual(code, 0, output)
        self.assertEqual(output.count("Download now? [Y/n]"), 1, output)
        self.assertIn("FETCH_APP_RAN", output)

    def test_fetch_run_nonterminal_yes(self):
        output = self.cli("run", "--yes", "-j1")
        self.assert_fetched(output)
        self.assertNotIn("Download now?", output)
        self.assertIn("Launching project", output)

    def test_fetch_short_version_pin_uses_update(self):
        short = self.initial[:7]
        (self.project / ".trussc-version").write_text(short + "\n", encoding="utf-8")
        self.cli("build", "-y", "-j1")
        self.assertEqual((self.project / ".trussc-version").read_text(encoding="utf-8"), self.initial + "\n")


@unittest.skipUnless(os.name == "posix", "PTY checks require POSIX")
class AddonFetchTests(FetchCases, AddonVersionTests):
    def setUp(self):
        super().setUp()
        (self.project / "addons.make").write_text("alice/tcxFixture@v1\n", encoding="utf-8")
        self.identity = "alice/tcxFixture@v1"
        self.cached = self.cache / "addons/alice/tcxFixture/v1"
        self.built = self.project / f"build-{NATIVE}/fixture"

    def test_fetch_missing_framework_defers_cached_addon_dependencies(self):
        self.cli("update")
        manifest = self.cached / "addon.json"
        manifest.write_text(json.dumps({"dependencies": ["tcxBundled"]}), encoding="utf-8")
        (self.project / ".trussc-version").write_text("main\n", encoding="utf-8")
        code, output = self.terminal(answer=b"n\n")
        self.assertEqual(code, 1, output)
        self.assertIn("  TrussC-org/TrussC@main", output)
        self.assertEqual(output.count("Download now? [Y/n]"), 1, output)
        self.assertNotIn("unknown owner", output)
        self.assertNotIn("  tcxBundled", output)

    def test_fetch_ownerless_selection_with_fresh_registry(self):
        body = self.registry_path().read_text(encoding="utf-8")
        self.registry_path().unlink()
        commands = self.base / "commands"
        commands.mkdir()
        marker = self.base / "registry-requested"
        curl = commands / "curl"
        curl.write_text("#!/usr/bin/env python3\nfrom pathlib import Path\nPath(" + repr(str(marker)) +
                        ").touch()\nprint(" + repr(body) + ")\n", encoding="utf-8")
        curl.chmod(0o755)
        self.env["PATH"] = str(commands) + os.pathsep + self.env["PATH"]
        (self.project / "addons.make").write_text("tcxFixture@v1\n", encoding="utf-8")
        output = self.cli("build", success=False)
        self.assertIn("  tcxFixture@v1", output)
        self.assertIn("--yes", output)
        self.assertFalse(marker.exists())
        self.assertFalse(self.cached.exists())
        output = self.cli("build", "--yes", "-j1")
        self.assertIn("  tcxFixture@v1", output)
        self.assertTrue(marker.exists())
        self.assertTrue(self.cached.is_dir())
        self.assertTrue(self.built.is_file())

    def test_fetch_legacy_project_keeps_inherited_framework_lookup(self):
        (self.project / ".trussc-version").unlink()
        (self.project / "addons.make").write_text("tcxBundled\n", encoding="utf-8")
        presets = self.project / "CMakePresets.json"
        data = json.loads(presets.read_text(encoding="utf-8"))
        data["vendor"]["trussc"].pop("localPath", None)
        native = next(p for p in data["configurePresets"] if p["name"] == NATIVE)
        variables = native.pop("cacheVariables")
        variables["TRUSSC_DIR"] = str(self.source / "core")
        data["configurePresets"].append({"name": "framework-base", "hidden": True,
                                         "cacheVariables": variables})
        native["inherits"] = "framework-base"
        presets.write_text(json.dumps(data), encoding="utf-8")
        self.env.pop("TRUSSC_DIR", None)
        before = presets.read_bytes()
        output = self.cli("build", "-j1")
        self.assertNotIn("Not in cache", output)
        self.assertTrue(self.built.is_file())
        self.assertEqual(presets.read_bytes(), before)

    def test_fetch_short_addon_pin_uses_update(self):
        (self.project / "addons.make").write_text("alice/tcxFixture@" + self.addon_initial[:7] + "\n", encoding="utf-8")
        self.cli("build", "-y", "-j1")
        self.assertIn("alice/tcxFixture@" + self.addon_initial + "\n", (self.project / "addons.make").read_text(encoding="utf-8"))

    def test_fetch_dist_keeps_its_existing_offline_behavior(self):
        output = self.cli("dist", success=False)
        self.assertIn("version is unavailable; run `trusscli update`", output)
        self.assertNotIn("Not in cache", output)
        self.assertNotIn("Download now?", output)
        self.assertFalse(self.cached.exists())

    def test_fetch_local_root_updates_once(self):
        output = self.cli("build", "--tc-root", self.source, "--yes", "-j1")
        self.assertEqual(output.count("Project updated:"), 1, output)
        self.assert_fetched(output)

    def test_fetch_lists_all_missing_selections_once(self):
        (self.project / ".trussc-version").write_text("main\n", encoding="utf-8")
        code, output = self.terminal(answer=b"N\n")
        self.assertEqual(code, 1, output)
        self.assertIn("  TrussC-org/TrussC@main", output)
        self.assertIn("  alice/tcxFixture@v1", output)
        self.assertEqual(output.count("Download now? [Y/n]"), 1, output)
        self.assertFalse(self.cached.exists())



def load_tests(loader, tests, pattern):
    return unittest.TestSuite(cls(name) for cls in (VersionFetchTests, AddonFetchTests)
                              for name in loader.getTestCaseNames(cls) if name.startswith("test_fetch_")
                              and (not os.environ.get("TRUSSCLI_BEFORE")
                                   or name == "test_fetch_cached_has_no_prompt_and_same_output"))


if __name__ == "__main__":
    unittest.main()
