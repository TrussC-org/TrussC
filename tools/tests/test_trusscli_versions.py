"""Offline per-project version checks. Set TRUSSCLI to a CLI built from this tree.

Run through tcbuild.sh: one check configures and builds a tiny fixture app.
All Git writes are confined to temporary fake repositories, never this checkout.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()
NATIVE = {"linux": "linux", "darwin": "macos", "win32": "windows"}[sys.platform]


@unittest.skipUnless(CLI.is_file(), "build this tree's CLI and set TRUSSCLI")
class VersionTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="trusscli-versions-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name)
        self.project = self.base / "project"
        (self.project / "src").mkdir(parents=True)
        (self.project / "src/main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        (self.project / "CMakeLists.txt").write_text("# TRUSSC_DIR\n", encoding="utf-8")
        (self.project / "addons.make").write_text("", encoding="utf-8")
        (self.project / "CMakePresets.json").write_text(json.dumps({
            "version": 6, "vendor": {"trussc": {"ide": "cmake"}, "fixture": {"keep": True}}}), encoding="utf-8")
        self.source = self.base / "source"
        core = self.source / "core/cmake"
        core.mkdir(parents=True)
        (core / "trussc_app.cmake").write_text(
            'macro(trussc_app)\nadd_executable(fixture src/main.cpp)\nendmacro()\n', encoding="utf-8")
        template = self.source / "examples/templates/emptyExample/src"
        template.mkdir(parents=True)
        (template / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        self.git("init", "-b", "main", self.source)
        self.git("-C", self.source, "add", ".")
        self.git("-C", self.source, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-m", "initial")
        self.initial = self.git("-C", self.source, "rev-parse", "HEAD").strip()
        self.git("-C", self.source, "tag", "v1")
        self.git("-C", self.source, "tag", "shared")
        self.advance()
        self.moved = self.git("-C", self.source, "rev-parse", "HEAD").strip()
        self.git("-C", self.source, "branch", "shared")
        self.git("-C", self.source, "branch", "feature/version+test")
        self.git("-C", self.source, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "tag", "-a", "annotated", self.initial, "-m", "annotated release")
        for owner in ("TrussC-org", "tettou771"):
            remote = self.base / "remotes" / owner / "TrussC.git"
            remote.parent.mkdir(parents=True, exist_ok=True)
            self.git("clone", "--bare", self.source, remote)
        self.cache = self.base / "cache with spaces"
        self.env = dict(os.environ, HOME=str(self.base / "home"), USERPROFILE=str(self.base / "home"),
                        TRUSSC_DIR=str(self.source), TRUSSC_CACHE_DIR=str(self.cache),
                        TRUSSC_GITHUB_BASE_URL=(self.base / "remotes").as_uri(),
                        GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                        GIT_ALLOW_PROTOCOL="file", GIT_TERMINAL_PROMPT="0")

    def git(self, *args):
        result = subprocess.run(["git", *map(str, args)], capture_output=True, text=True, encoding="utf-8",
                                env=dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                                         GIT_ALLOW_PROTOCOL="file", GIT_TERMINAL_PROMPT="0"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def advance(self):
        marker = self.source / "revision"
        marker.write_text(marker.read_text(encoding="utf-8") + "next\n" if marker.exists() else "next\n", encoding="utf-8")
        self.git("-C", self.source, "add", ".")
        self.git("-C", self.source, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-m", "advance")

    def cli(self, *args, success=True):
        result = subprocess.run([str(CLI), *map(str, args)], cwd=self.project, env=self.env,
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=20)
        self.last_result = result
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
            self.assertIn("Error:", output)
        return output

    def root(self):
        presets = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))
        native = next(p for p in presets["configurePresets"] if p["name"] == NATIVE)
        return Path(native["cacheVariables"]["TRUSSC_DIR"]).parent

    def local(self):
        return json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))["vendor"]["trussc"].get("localPath")

    def test_tags_branches_commits_forks_and_tag_priority(self):
        for spec, commit, owner, ref in (("v1", self.initial, "TrussC-org", "v1"),
                ("main", self.moved, "TrussC-org", "main"),
                (self.initial, self.initial, "TrussC-org", self.initial),
                ("tettou771/TrussC@v1", self.initial, "tettou771", "v1"),
                ("shared", self.initial, "TrussC-org", "shared"),
                ("annotated", self.initial, "TrussC-org", "annotated"),
                ("feature/version+test", self.moved, "TrussC-org", "feature/version+test")):
            with self.subTest(spec=spec):
                self.cli("use", spec)
                expected = self.cache / "versions" / owner / "TrussC" / ref
                self.assertEqual(self.root(), expected)
                self.assertEqual(self.git("-C", expected, "rev-parse", "HEAD").strip(), commit)
                self.assertTrue((self.cache / "repos" / owner / "TrussC.git").is_dir())
                self.assertEqual((self.project / ".trussc-version").read_text(encoding="utf-8").strip(), spec)

    def test_official_and_fork_same_tag_keep_separate_worktrees(self):
        fork_remote = self.base / "remotes/tettou771/TrussC.git"
        self.git("--git-dir=" + str(fork_remote), "update-ref", "refs/tags/v1", self.moved)
        official = self.cache / "versions/TrussC-org/TrussC/v1"
        fork = self.cache / "versions/tettou771/TrussC/v1"
        acquired = {}
        for spec, root, commit in (("v1", official, self.initial),
                                   ("tettou771/TrussC@v1", fork, self.moved),
                                   ("v1", official, self.initial)):
            with self.subTest(spec=spec):
                self.cli("use", spec)
                self.assertEqual(self.root(), root)
                acquired[root] = commit
                for worktree, expected in acquired.items():
                    self.assertEqual(self.git("-C", worktree, "rev-parse", "HEAD").strip(), expected)
                output = self.cli("build", "--dry-run")
                self.assertIn("-DTRUSSC_DIR=" + str(root / "core"), output)
                self.assertIn(commit, output)

    def test_colliding_ref_paths_do_not_reuse_other_worktrees(self):
        self.git("-C", self.source, "branch", "v1/core")
        self.git("-C", self.source, "push", self.base / "remotes/TrussC-org/TrussC.git", "refs/heads/v1/core")
        for first, second, commit in (("v1", "v1/core", self.initial),
                                      ("v1/core", "v1", self.moved)):
            with self.subTest(first=first):
                self.env["TRUSSC_CACHE_DIR"] = str(self.cache / first.replace("/", "-"))
                self.cli("use", first)
                root = self.root()
                output = self.cli("use", second, success=False)
                self.assertIn("exact registered worktree root", output)
                self.assertIn("'v1'", output)
                self.assertIn("'v1/core'", output)
                self.assertEqual(self.git("-C", root, "rev-parse", "HEAD").strip(), commit)

    def test_commit_id_wins_over_hex_named_tag(self):
        self.git("-C", self.source, "tag", self.initial, self.moved)
        self.git("-C", self.source, "push", self.base / "remotes/TrussC-org/TrussC.git",
                 "refs/tags/" + self.initial)
        self.cli("use", self.initial)
        self.assertEqual(self.git("-C", self.root(), "rev-parse", "HEAD").strip(), self.initial)

    @unittest.skipIf(sys.platform == "win32", "fixture needs directory symlinks")
    def test_ref_path_cannot_follow_worktree_symlink_outside_cache(self):
        outside = self.base / "outside"
        outside.mkdir()
        (self.source / "escape").symlink_to(outside, target_is_directory=True)
        self.advance()
        self.git("-C", self.source, "tag", "parent")
        self.git("-C", self.source, "branch", "parent/escape/child")
        self.git("-C", self.source, "push", self.base / "remotes/TrussC-org/TrussC.git",
                 "refs/tags/parent", "refs/heads/parent/escape/child")
        self.cli("use", "parent")
        self.assertTrue((self.root() / "escape").is_symlink())
        output = self.cli("use", "parent/escape/child", success=False)
        self.assertIn("only real directories", output)
        self.assertEqual(list(outside.iterdir()), [])
        # Also reject an existing destination reached through the symlink.
        output = self.cli("use", "parent/escape", success=False)
        self.assertIn("only real directories", output)
        self.assertEqual(list(outside.iterdir()), [])

    def test_version_manifest_is_bounded_before_parsing(self):
        version = self.project / ".trussc-version"
        version.write_bytes(b"v1" + b" " * 4095)
        output = self.cli("build", "--dry-run", success=False)
        self.assertIn("4096-byte limit", output)
        self.assertFalse(self.cache.exists())

    @unittest.skipUnless(sys.platform == "linux", "fixture uses /dev/zero")
    def test_version_manifest_rejects_special_file_symlink(self):
        (self.project / ".trussc-version").symlink_to("/dev/zero")
        output = self.cli("build", "--dry-run", success=False)
        self.assertIn("must be a regular file", output)
        self.assertFalse(self.cache.exists())

    def test_branch_refreshes_only_on_update_and_build_uses_commit(self):
        self.cli("use", "main")
        self.advance()
        latest = self.git("-C", self.source, "rev-parse", "HEAD").strip()
        self.git("-C", self.source, "push", self.base / "remotes/TrussC-org/TrussC.git", "main")
        out = self.cli("build", "--dry-run")
        self.assertIn(self.moved, out)
        self.assertNotIn(latest, out)
        self.cli("update")
        self.assertEqual(self.git("-C", self.root(), "rev-parse", "HEAD").strip(), latest)
        self.assertIn(latest, self.cli("build"))

    def test_build_missing_cache_never_fetches_even_without_presets(self):
        (self.project / ".trussc-version").write_text("v1\n", encoding="utf-8")
        for remove_presets in (False, True):
            if remove_presets:
                (self.project / "CMakePresets.json").unlink()
            out = self.cli("build", success=False)
            self.assertIn("run `trusscli update`", out)
            self.assertFalse(self.cache.exists())
        self.cli("update", "--ide", "cmake")
        self.assertEqual(self.root(), self.cache / "versions/TrussC-org/TrussC/v1")
        shutil.rmtree(self.root())
        self.cli("build", success=False)
        self.cli("update")
        self.assertTrue(self.root().is_dir())

    def test_last_selection_wins_and_clear_order(self):
        self.cli("use", "v1")
        self.cli("use-local", self.source)
        self.assertEqual(self.root(), self.source)
        self.assertEqual(self.local(), self.source.as_posix())
        # Simulate a pull changing the shared file while the local marker stays.
        (self.project / ".trussc-version").write_text("main\n", encoding="utf-8")
        self.cli("update")
        self.assertEqual(self.root(), self.source)
        self.assertFalse((self.cache / "versions/TrussC-org/TrussC/main").exists())
        self.cli("use-local", "--clear")
        self.assertEqual(self.root(), self.cache / "versions/TrussC-org/TrussC/main")
        self.assertIsNone(self.local())
        self.cli("use-local", self.source)
        self.cli("use", "v1")
        self.assertIsNone(self.local())
        self.assertEqual(self.root(), self.cache / "versions/TrussC-org/TrussC/v1")
        self.cli("update", "--tc-root", self.source)
        self.assertEqual(self.local(), self.source.as_posix())
        self.cli("use", "--clear")
        self.assertIsNone(self.local())
        self.assertFalse((self.project / ".trussc-version").exists())
        self.assertEqual(self.root(), self.source)
        vendor = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))["vendor"]
        self.assertEqual(vendor["fixture"], {"keep": True})

    def test_build_follows_shared_file_changes_without_fetch(self):
        self.cli("use", "main")
        self.cli("use", "v1")
        (self.project / ".trussc-version").write_text("main\n", encoding="utf-8")
        out = self.cli("build", "--dry-run")
        self.assertIn(self.moved, out)
        self.assertIn("-DTRUSSC_DIR=" + str(self.cache / "versions/TrussC-org/TrussC/main/core"), out)
        self.cli("use-local", self.source)
        (self.project / ".trussc-version").write_text("unavailable\n", encoding="utf-8")
        self.assertIn(str(self.source), self.cli("build", "--dry-run"))

    def test_read_only_commands_follow_selection_and_root_alias_is_persistent(self):
        self.cli("use", "v1")
        out = self.cli("info", "project", "--json")
        self.assertIn(str(self.root()), out)
        self.cli("info", "project", "--json", "--tc-root", self.source)
        json.loads(self.last_result.stdout)
        self.assertEqual(self.local(), self.source.as_posix())
        self.assertEqual(self.root(), self.source)
        self.cli("update")
        self.assertEqual(self.root(), self.source)

    def test_malformed_specs_fail_before_writes_or_fetch(self):
        before = (self.project / "CMakePresets.json").read_bytes()
        for spec in ("", "..", "../main", "a/../b", "/main", "main/", "a//b", "@main",
                     "a/@v1", "a/b@", "a/b/c@v1", "a/b@../v1", "a/b@x@y", "a b", "x.lock", "x:bad", "a/b@x[1]", "a/b@x\\y"):
            with self.subTest(spec=spec):
                self.cli("use", spec, success=False)
                self.assertFalse((self.project / ".trussc-version").exists())
                self.assertFalse(self.cache.exists())
                self.assertEqual((self.project / "CMakePresets.json").read_bytes(), before)

    def test_unavailable_ref_stops_without_fallback(self):
        self.cli("use", "missing", success=False)
        self.assertEqual((self.project / ".trussc-version").read_text(encoding="utf-8").strip(), "missing")
        self.assertIn("run `trusscli update`", self.cli("build", success=False))

    def test_opt_in_and_local_path_validation(self):
        self.cli("update")
        self.assertEqual(self.root(), self.source)
        self.assertFalse((self.project / ".trussc-version").exists())
        self.cli("use-local", self.base / "missing", success=False)
        self.assertIsNone(self.local())
        (self.project / "CMakePresets.json").write_text(
            json.dumps({"version": 6, "vendor": {"trussc": "invalid"}}), encoding="utf-8")
        self.cli("update", "--ide", "cmake")
        self.assertIsNone(self.local())
        self.cli("new", self.base / "new-app", "--ide", "cmake")
        self.assertFalse((self.base / "new-app/.trussc-version").exists())
        self.cli("new", self.base / "local-app", "--ide", "cmake", "--tc-root", self.source)
        data = json.loads((self.base / "local-app/CMakePresets.json").read_text(encoding="utf-8"))
        self.assertEqual(data["vendor"]["trussc"]["localPath"], self.source.as_posix())


if __name__ == "__main__":
    unittest.main()
