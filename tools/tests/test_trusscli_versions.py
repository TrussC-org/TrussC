"""Offline per-project version checks. Set TRUSSCLI to a CLI built from this tree.

Run through tcbuild.sh: one check configures and builds a tiny fixture app.
All Git writes are confined to temporary fake repositories, never this checkout.
"""
import io
import tarfile
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()
NATIVE = {"linux": "linux", "darwin": "macos", "win32": "windows"}[sys.platform]


def fixture_rmtree(path):
    """Remove fixture repositories, including Windows read-only Git objects."""
    def make_writable_and_retry(function, path, exception):
        os.chmod(path, stat.S_IWRITE)
        function(path)

    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=make_writable_and_retry)
    else:
        shutil.rmtree(path, onerror=make_writable_and_retry)


@unittest.skipUnless(CLI.is_file(), "build this tree's CLI and set TRUSSCLI")
class VersionTests(unittest.TestCase):
    def setUp(self):
        self.base = Path(tempfile.mkdtemp(prefix="trusscli-versions-")).resolve()
        self.addCleanup(fixture_rmtree, self.base)
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
        (self.source / ".git_archival.txt").write_text(
            "describe: $Format:%(describe:tags,match=v[0-9]*)$\n", encoding="utf-8")
        (self.source / ".gitattributes").write_text(".git_archival.txt export-subst\n", encoding="utf-8")
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
                        LOCALAPPDATA=str(self.base / "home/AppData/Local"),
                        APPDATA=str(self.base / "home/AppData/Roaming"),
                        XDG_CACHE_HOME=str(self.base / "home/.cache"),
                        XDG_CONFIG_HOME=str(self.base / "home/.config"),
                        XDG_DATA_HOME=str(self.base / "home/.local/share"),
                        TRUSSC_DIR=str(self.source), TRUSSC_CACHE_DIR=str(self.cache),
                        TRUSSC_GITHUB_BASE_URL=(self.base / "remotes").as_uri(),
                        GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                        GIT_ALLOW_PROTOCOL="file", GIT_TERMINAL_PROMPT="0")

    def registry_path(self):
        # Match registryCachePath(): macOS also uses XDG_CACHE_HOME, not Library.
        key = "LOCALAPPDATA" if sys.platform == "win32" else "XDG_CACHE_HOME"
        return Path(self.env[key]) / "trusscli/registry.json"

    def fixture_symlink(self, link, target, *, target_is_directory=False):
        try:
            link.symlink_to(target, target_is_directory=target_is_directory)
        except OSError:
            self.skipTest("fixture needs directory symlinks")

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
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
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

    def old_layout(self):
        fixture_rmtree(self.source / "core")
        (self.source / "trussc/cmake").mkdir(parents=True)
        (self.source / "trussc/cmake/trussc_app.cmake").write_text("# old layout\n", encoding="utf-8")
        self.advance()
        commit = self.git("-C", self.source, "rev-parse", "HEAD").strip()
        self.git("-C", self.source, "tag", "v0.4.4")
        for owner in ("TrussC-org", "tettou771"):
            self.git("-C", self.source, "push", self.base / "remotes" / owner / "TrussC.git", "main", "v0.4.4")
        if self.env.get("TRUSSC_FETCH") == "tarball":
            for ref in ("main", "v0.4.4", commit):
                self.archive(ref)
            self.archive("v0.4.4", owner="tettou771")
        return commit

    def test_old_layout_use_preserves_selection_and_leaves_no_cache_entry(self):
        commit = self.old_layout()
        for existing in (False, True):
            if existing:
                self.cli("use", "v1")
                self.cli("use-local", self.cache / "versions/TrussC-org/TrussC/v1")
            if existing:
                for name in (".trussc-version", "CMakePresets.json"):
                    path = self.project / name
                    path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            before = {name: (self.project / name).read_bytes() if (self.project / name).exists() else None
                      for name in (".trussc-version", "CMakePresets.json")}
            for ref in ("v0.4.4", "main", commit, "tettou771/TrussC@v0.4.4"):
                with self.subTest(existing=existing, ref=ref):
                    output = self.cli("use", ref, success=False)
                    self.assertIn("predates the core/ layout; trusscli supports v0.5.0 and later.", output)
                    self.assertIn("Run `trusscli use v0.5.0` (or newer).", output)
                    for name, contents in before.items():
                        path = self.project / name
                        self.assertEqual(path.read_bytes() if path.exists() else None, contents)
                    owner, version = (ref.split("/", 1)[0], "v0.4.4") if "@" in ref else ("TrussC-org", ref)
                    self.assertFalse((self.cache / "versions" / owner / "TrussC" / version).exists())
            self.assertEqual(list(self.cache.rglob(".trussc-download-*")), [])

    def test_old_layout_update_preserves_existing_cached_tree(self):
        self.cli("use", "main")
        root = self.root()
        before = (root / "core/cmake/trussc_app.cmake").read_bytes()
        self.old_layout()
        self.assertIn("predates the core/ layout", self.cli("update", success=False))
        self.assertEqual((root / "core/cmake/trussc_app.cmake").read_bytes(), before)
        self.assertEqual((self.project / ".trussc-version").read_text(encoding="utf-8"), "main\n")
        self.cli("build", "--dry-run")

    def test_directory_layout_marker_rejected_before_cache_publication(self):
        self.cli("use", "main")
        root = self.root()
        before = (root / "core/cmake/trussc_app.cmake").read_bytes()
        old_commit = self.git("-C", root, "rev-parse", "HEAD")
        (self.source / "core/cmake/trussc_app.cmake").unlink()
        (self.source / "core/cmake/trussc_app.cmake").mkdir()
        (self.source / "core/cmake/trussc_app.cmake/entry").write_text("# directory marker\n", encoding="utf-8")
        (self.source / "trussc").mkdir()
        (self.source / "trussc/entry").write_text("# old layout\n", encoding="utf-8")
        self.advance()
        self.git("-C", self.source, "tag", "directory-marker")
        self.git("-C", self.source, "push", self.base / "remotes/TrussC-org/TrussC.git", "main", "directory-marker")
        selection = {name: (self.project / name).read_bytes()
                     for name in (".trussc-version", "CMakePresets.json")}
        self.assertIn("predates the core/ layout", self.cli("use", "directory-marker", success=False))
        self.assertFalse((root.parent / "directory-marker").exists())
        self.assertIn("predates the core/ layout", self.cli("update", success=False))
        self.assertEqual(self.git("-C", root, "rev-parse", "HEAD"), old_commit)
        self.assertEqual((root / "core/cmake/trussc_app.cmake").read_bytes(), before)
        for name, contents in selection.items():
            self.assertEqual((self.project / name).read_bytes(), contents)
        self.cli("build", "--dry-run")

    def test_use_replaces_oversized_previous_pin(self):
        (self.project / ".trussc-version").write_bytes(b"v1" + b" " * 4095)
        self.cli("use", "v1")
        self.assertEqual((self.project / ".trussc-version").read_bytes(), b"v1\n")
        self.assertEqual(self.root(), self.cache / "versions/TrussC-org/TrussC/v1")

    def test_installed_cli_builds_sources_without_version_tools(self):
        self.cli("use", "v1")
        self.assertIn(self.root().as_posix(), self.cli("build", "--dry-run"))
        self.assertFalse((self.root() / "tools").exists())
        presets = self.project / "CMakePresets.json"
        data = json.loads(presets.read_text(encoding="utf-8"))
        data["vendor"]["fixture"] = {"keep": [1, 2]}
        presets.write_text(json.dumps(data, indent=3), encoding="utf-8")
        before = presets.read_bytes()
        self.cli("build", "--dry-run")
        self.assertEqual(presets.read_bytes(), before)
        # The selected sources may have tools of their own; builds use the installed CLI.
        tools = self.root() / "tools"
        tools.mkdir()
        (tools / "CMakeLists.txt").write_text('message(FATAL_ERROR "unexpected tools build")\n', encoding="utf-8")
        self.cli("build", "--dry-run")

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
                self.assertIn("-DTRUSSC_DIR=" + (root / "core").as_posix(), output)
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
        self.fixture_symlink(self.source / "escape", outside, target_is_directory=True)
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

    @unittest.skipIf(sys.platform == "win32", "fixture uses /dev/zero")
    def test_version_manifest_rejects_special_file_symlink(self):
        self.fixture_symlink(self.project / ".trussc-version", "/dev/zero")
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
        fixture_rmtree(self.root())
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
        self.assertIn("-DTRUSSC_DIR=" + (self.cache / "versions/TrussC-org/TrussC/main/core").as_posix(), out)
        self.cli("use-local", self.source)
        (self.project / ".trussc-version").write_text("unavailable\n", encoding="utf-8")
        self.assertIn(self.source.as_posix(), self.cli("build", "--dry-run"))

    def test_read_only_commands_follow_selection_and_root_alias_is_persistent(self):
        self.cli("use", "v1")
        self.cli("info", "project", "--json")
        self.assertEqual(Path(json.loads(self.last_result.stdout)["tc_root"]), self.root())
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

    def test_unavailable_ref_preserves_previous_selection(self):
        self.cli("use", "v1")
        before = {name: (self.project / name).read_bytes()
                  for name in (".trussc-version", "CMakePresets.json")}
        cached = {p.relative_to(self.cache): p.read_bytes() if p.is_file() else None
                  for p in self.cache.rglob("*")}
        output = self.cli("use", "missing", success=False)
        self.assertEqual(self.last_result.returncode, 1)
        self.assertEqual(next(line for line in self.last_result.stderr.splitlines() if line.startswith("Error:")),
                         "Error: Ref 'missing' was not found in TrussC-org/TrussC.")
        self.assertIn("Check the tag or branch name", output)
        for name, contents in before.items():
            self.assertEqual((self.project / name).read_bytes(), contents)
        self.assertEqual({p.relative_to(self.cache): p.read_bytes() if p.is_file() else None
                          for p in self.cache.rglob("*")}, cached)
        self.cli("build", "--dry-run")

    def test_missing_commit_and_repository_failure_are_distinct(self):
        self.cli("use", "v1")
        ref = "0" * 40
        self.cli("use", ref, success=False)
        self.assertEqual(self.last_result.returncode, 1)
        self.assertEqual(next(line for line in self.last_result.stderr.splitlines() if line.startswith("Error:")),
                         f"Error: Ref '{ref}' was not found in TrussC-org/TrussC.")
        (self.base / "remotes/TrussC-org/TrussC.git").rename(self.base / "unavailable.git")
        output = self.cli("use", "missing", success=False)
        self.assertIn("Error: TrussC version Git command failed:", output)
        self.assertNotIn("was not found", output)

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


@unittest.skipUnless(CLI.is_file(), "build this tree's CLI and set TRUSSCLI")
class TarballTests(unittest.TestCase):
    fixture_symlink = VersionTests.fixture_symlink
    git = VersionTests.git
    advance = VersionTests.advance
    cli = VersionTests.cli
    root = VersionTests.root

    def setUp(self):
        VersionTests.setUp(self)
        self.env["TRUSSC_FETCH"] = "tarball"
        self.env["TRUSSC_GITHUB_BASE_URL"] = (self.base / "archives").as_uri()
        self.archives = self.base / "archives"
        for owner in ("TrussC-org", "tettou771"):
            self.archive("v1", owner=owner)
        for ref in ("main", self.initial, "feature/version+test"):
            self.archive(ref)
        self.archive("shared", revision="refs/tags/shared", path="refs/tags/shared")
        # A short URL pointing at the branch must not override the qualified tag.
        self.archive("shared", revision="refs/heads/shared")

    test_use_replaces_oversized_previous_pin = VersionTests.test_use_replaces_oversized_previous_pin
    old_layout = VersionTests.old_layout
    test_old_layout_use_preserves_selection_and_leaves_no_cache_entry = VersionTests.test_old_layout_use_preserves_selection_and_leaves_no_cache_entry
    test_old_layout_update_preserves_existing_cached_tree = VersionTests.test_old_layout_update_preserves_existing_cached_tree
    test_installed_cli_builds_sources_without_version_tools = VersionTests.test_installed_cli_builds_sources_without_version_tools

    def archive_path(self, ref, owner="TrussC-org"):
        return self.archives / owner / "TrussC/archive" / (ref + ".tar.gz")

    def archive(self, ref, owner="TrussC-org", revision=None, path=None):
        target = self.archive_path(path or ref, owner)
        target.parent.mkdir(parents=True, exist_ok=True)
        prefix = "TrussC-" + ref.replace("/", "-") + "/"
        self.git("-C", self.source, "archive", "--format=tar.gz", "--prefix=" + prefix,
                 "--output=" + str(target), revision or ref)
        with tarfile.open(target) as archive:
            return archive.extractfile(prefix + ".git_archival.txt").read().decode().strip()[10:]

    @unittest.skipIf(sys.platform == "win32", "fixture needs an executable curl script")
    def test_unavailable_ref_preserves_previous_selection(self):
        self.cli("use", "v1")
        # Keep acquisition offline: model curl's HTTP result on the file:// fixture path.
        tools = self.base / "http-status-tools"
        tools.mkdir()
        curl = tools / "curl"
        real_curl = shutil.which("curl")
        self.env["PATH"] = str(tools) + os.pathsep + self.env["PATH"]
        for status, code in ((404, 22), (401, 22), (403, 22), (500, 22), (0, 7)):
            with self.subTest(status=status):
                curl.write_text(
                    "#!" + sys.executable + "\nimport subprocess, sys\n"
                    "if 'missing' not in sys.argv[-1]:\n"
                    "    sys.exit(subprocess.call([" + repr(real_curl) + "] + sys.argv[1:]))\n"
                    "assert '--write-out' in sys.argv\n"
                    f"sys.stderr.write('fixture download failed\\n')\n"
                    f"sys.stdout.write('\\n{status:03d}')\nsys.exit({code})\n", encoding="utf-8")
                curl.chmod(0o755)
                if status == 404:
                    VersionTests.test_unavailable_ref_preserves_previous_selection(self)
                else:
                    output = self.cli("use", "missing", success=False)
                    self.assertEqual(self.last_result.returncode, 1)
                    self.assertIn("Error: TrussC archive download failed: fixture download failed", output)
                    self.assertNotIn("was not found", output)

    def test_tags_branches_commits_forks_and_tag_priority(self):
        for spec, owner, ref, revision in (
                ("v1", "TrussC-org", "v1", "refs/tags/v1"),
                ("main", "TrussC-org", "main", "refs/heads/main"),
                (self.initial, "TrussC-org", self.initial, self.initial),
                ("tettou771/TrussC@v1", "tettou771", "v1", "refs/tags/v1"),
                ("shared", "TrussC-org", "shared", "refs/tags/shared"),
                ("feature/version+test", "TrussC-org", "feature/version+test", "refs/heads/feature/version+test")):
            with self.subTest(spec=spec):
                expected = self.git("-C", self.source, "describe", "--tags", "--match=v[0-9]*", revision).strip()
                self.cli("use", spec)
                root = self.cache / "versions" / owner / "TrussC" / ref
                self.assertEqual(self.root(), root)
                self.assertFalse((root / ".git").exists())
                self.assertFalse((self.cache / "repos").exists())
                output = self.cli("build", "--dry-run")
                self.assertIn("(" + expected + ")", output)
                self.assertIn("-DTRUSSC_DIR=" + (root / "core").as_posix(), output)

    def test_branch_refresh_and_failed_update_preserves_previous_tree(self):
        original = self.archive("main")
        self.cli("use", "main")
        self.advance()
        latest = self.archive("main")
        self.assertNotEqual(original, latest)
        self.assertIn("(" + original + ")", self.cli("build", "--dry-run"))
        self.cli("update")
        self.assertIn("(" + latest + ")", self.cli("build"))
        root = self.root()
        before = (root / "revision").read_bytes()
        self.archive_path("main").write_bytes(b"truncated")
        self.cli("update", success=False)
        self.assertEqual((root / "revision").read_bytes(), before)
        self.assertIn("(" + latest + ")", self.cli("build", "--dry-run"))
        self.assertEqual(sorted(p.name for p in root.parent.iterdir()), ["main"])

    def test_missing_and_truncated_archives_leave_no_version(self):
        before = (self.project / "CMakePresets.json").read_bytes()
        for ref in ("missing", "truncated"):
            with self.subTest(ref=ref):
                if ref == "truncated":
                    data = self.archive_path("v1").read_bytes()
                    self.archive_path(ref).write_bytes(data[:len(data) // 2])
                self.cli("use", ref, success=False)
                root = self.cache / "versions/TrussC-org/TrussC" / ref
                self.assertFalse(root.exists())
                self.assertEqual(list(root.parent.iterdir()), [])
                self.assertFalse((self.project / ".trussc-version").exists())
                self.assertEqual((self.project / "CMakePresets.json").read_bytes(), before)

    def test_unsafe_archive_entries_are_rejected_before_extraction(self):
        for entry, target in (("TrussC-fixture/../outside", None), ("/absolute", None),
                              ("TrussC-fixture/link", "../../outside"),
                              ("TrussC-fixture/link", "/absolute"),
                              ("TrussC-fixture/link", "C:/outside"),
                              ("TrussC-fixture/link", "..\\outside")):
            with self.subTest(entry=entry, target=target):
                path = self.archive_path("unsafe")
                with tarfile.open(path, "w:gz") as archive:
                    info = tarfile.TarInfo(entry)
                    if target:
                        info.type = tarfile.SYMTYPE
                        info.linkname = target
                        archive.addfile(info)
                    else:
                        info.size = 1
                        archive.addfile(info, io.BytesIO(b"x"))
                self.cli("use", "unsafe", success=False)
                root = self.cache / "versions/TrussC-org/TrussC"
                self.assertEqual(list(root.iterdir()), [])
                self.assertFalse((self.base / "outside").exists())

    def large_zero_archive(self, ref, members, owner="", group=""):
        # PAX sparse 0.1 stores a hole ending at size, with no payload to compress.
        # tarfile writes the headers; both GNU tar and BSD tar read this format.
        archive = self.archive_path(ref)
        with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as file:
            for name, size in members:
                info = tarfile.TarInfo("TrussC-large/" + name)
                info.uname, info.gname = owner, group
                info.pax_headers = {"GNU.sparse.size": str(size),
                                    "GNU.sparse.numblocks": "1",
                                    "GNU.sparse.map": f"{size},0"}
                file.addfile(info)
        self.assertLess(archive.stat().st_size, 4 * 1024 ** 2)

    def test_expanded_member_total_is_bounded_before_extraction(self):
        # Each member fits the limit; their sum exceeds it.
        self.large_zero_archive("large", [("first", 1024 ** 3 + 1), ("second", 1024 ** 3 + 1)])
        self.assertIn("expanded size exceeds 2 GiB", self.cli("use", "large", success=False))
        self.assertEqual(list((self.cache / "versions/TrussC-org/TrussC").iterdir()), [])

    def test_owner_names_with_spaces_do_not_hide_member_size(self):
        # A group named "x 5" once made the listing parser read 5 as the size.
        for owner, group in (("a", "x 5"), ("a b", "x 5")):
            with self.subTest(owner=owner, group=group):
                self.large_zero_archive("owner", [("large", 2 * 1024 ** 3 + 1)], owner, group)
                self.assertIn("expanded size exceeds 2 GiB", self.cli("use", "owner", success=False))
                self.assertEqual(list((self.cache / "versions/TrussC-org/TrussC").iterdir()), [])

    def test_oversized_archival_metadata_reports_unknown(self):
        self.cli("use", "v1")
        with (self.root() / ".git_archival.txt").open("wb") as file:
            file.seek(64 * 1024 * 1024)
            file.write(b"\ndescribe: v1\n")
        output = self.cli("build", "--dry-run")
        self.assertIn("(unknown)", output)
        self.assertLess(len(output), 16384)

    @unittest.skipIf(sys.platform == "win32", "fixture needs an executable script and POSIX process checks")
    def test_listing_output_limit_terminates_child(self):
        tools = self.base / "listing-tools"
        tools.mkdir()
        pidfile = self.base / "tar.pid"
        tool = tools / "tar"
        tool.write_text(
            "#!" + sys.executable + "\nimport os, time\n"
            "from pathlib import Path\n"
            "Path(" + repr(str(pidfile)) + ").write_text(str(os.getpid()), encoding='utf-8')\n"
            "os.write(1, b'x' * (17 * 1024 * 1024))\ntime.sleep(60)\n", encoding="utf-8")
        tool.chmod(0o755)
        self.env["PATH"] = str(tools) + os.pathsep + self.env["PATH"]
        try:
            self.assertIn("listing is too large", self.cli("use", "v1", success=False))
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text(encoding="utf-8")), 0)
            self.assertEqual(list((self.cache / "versions/TrussC-org/TrussC").iterdir()), [])
        finally:
            if pidfile.exists():
                try:
                    os.kill(int(pidfile.read_text(encoding="utf-8")), 9)
                except ProcessLookupError:
                    pass

    @unittest.skipIf(sys.platform == "win32", "fixture needs Windows symlink creation and tar extraction privileges")
    def test_internal_symlink_is_supported(self):
        self.fixture_symlink(self.source / "alias", "core", target_is_directory=True)
        self.advance()
        self.archive("main")
        self.cli("use", "main")
        self.assertTrue((self.root() / "alias/cmake/trussc_app.cmake").is_file())

    def test_colliding_ref_paths_preserve_cached_tree(self):
        self.archive("v1/core", revision="main")
        for first, second in (("v1", "v1/core"), ("v1/core", "v1")):
            with self.subTest(first=first):
                self.env["TRUSSC_CACHE_DIR"] = str(self.cache / first.replace("/", "-"))
                self.cli("use", first)
                root = self.root()
                before = (root / ".git_archival.txt").read_bytes()
                self.assertIn("conflicts", self.cli("use", second, success=False))
                self.assertEqual((root / ".git_archival.txt").read_bytes(), before)

    @unittest.skipIf(sys.platform == "win32", "fixture needs directory symlinks")
    def test_destination_containment_check_rejects_symlink(self):
        outside = self.base / "outside"
        outside.mkdir()
        versions = self.cache / "versions"
        versions.mkdir(parents=True)
        self.fixture_symlink(versions / "TrussC-org", outside, target_is_directory=True)
        self.assertIn("only real directories", self.cli("use", "v1", success=False))
        self.assertEqual(list(outside.iterdir()), [])

    def test_build_missing_cache_never_fetches(self):
        (self.project / ".trussc-version").write_text("v1\n", encoding="utf-8")
        self.assertIn("run `trusscli update`", self.cli("build", "--dry-run", success=False))
        self.assertFalse(self.cache.exists())
        self.cli("update", "--ide", "cmake")
        self.assertEqual(self.root(), self.cache / "versions/TrussC-org/TrussC/v1")

    @unittest.skipIf(sys.platform == "win32", "fixture uses executable symlinks in PATH")
    def test_git_unavailable_selects_tarball_automatically(self):
        path = self.base / "no-git-bin"
        path.mkdir()
        for tool in ("curl", "tar", "gzip"):
            self.fixture_symlink(path / tool, shutil.which(tool))
        self.env.pop("TRUSSC_FETCH")
        self.env["PATH"] = str(path)
        self.assertNotIn("Error:", self.cli("use", "v1"))
        self.assertFalse((self.cache / "repos").exists())
        self.assertIn("(v1)", self.cli("build", "--dry-run"))


@unittest.skipUnless(CLI.is_file(), "build this tree's CLI and set TRUSSCLI")
class WindowsTarListingTests(unittest.TestCase):
    """Use native tar on Windows; simulate CRLF and localized BSD listings on POSIX."""
    fixture_symlink = VersionTests.fixture_symlink
    git = VersionTests.git
    advance = VersionTests.advance
    cli = VersionTests.cli
    root = VersionTests.root
    archive = TarballTests.archive
    archive_path = TarballTests.archive_path
    old_layout = VersionTests.old_layout
    large_zero_archive = TarballTests.large_zero_archive
    test_use_replaces_oversized_previous_pin = TarballTests.test_use_replaces_oversized_previous_pin
    test_internal_symlink_is_supported = TarballTests.test_internal_symlink_is_supported
    test_expanded_member_total_is_bounded_before_extraction = TarballTests.test_expanded_member_total_is_bounded_before_extraction
    test_owner_names_with_spaces_do_not_hide_member_size = TarballTests.test_owner_names_with_spaces_do_not_hide_member_size
    test_old_layout_use_preserves_selection_and_leaves_no_cache_entry = TarballTests.test_old_layout_use_preserves_selection_and_leaves_no_cache_entry
    test_old_layout_update_preserves_existing_cached_tree = TarballTests.test_old_layout_update_preserves_existing_cached_tree

    def setUp(self):
        TarballTests.setUp(self)
        # runCaptured uses CreateProcess directly, so PATHEXT tar.cmd wrappers
        # cannot intercept tar. Exercise the real Windows tar instead.
        if sys.platform == "win32":
            return
        tools = self.base / "windows-tar"
        tools.mkdir()
        tool = tools / "tar"
        tool.write_text(
            "#!" + sys.executable + "\n"
            "import subprocess, sys\n"
            "result = subprocess.run([" + repr(shutil.which("tar")) + "] + sys.argv[1:], capture_output=True)\n"
            "output = result.stdout\n"
            "if '-tzf' in sys.argv or '-tvzf' in sys.argv:\n"
            "    lines = output.splitlines()\n"
            "    if '-tvzf' in sys.argv:\n"
            "        assert '--numeric-owner' in sys.argv\n"
            "        names = subprocess.check_output([" + repr(shutil.which("tar")) + ", '-P', '-tzf', sys.argv[-1]]).splitlines()\n"
            "        for i, line in enumerate(lines):\n"
            "            mode, owner, rest = line.split(None, 2)\n"
            "            if b'/' in owner:\n"
            "                uid, gid = owner.split(b'/')\n"
            "                size, rest = rest.split(None, 1)\n"
            "            else:\n"
            "                uid, gid, size, rest = rest.split(None, 3)\n"
            "            name = rest[rest.index(names[i]):]\n"
            "            date = ['11 24 14:28', '11月 24 14:28', '11 月 24 14:28'][i % 3].encode()\n"
            "            lines[i] = b' '.join([mode, b'0', uid, gid, size, date, name])\n"
            "    output = b'\\r\\n'.join(lines) + (b'\\r\\n' if lines else b'')\n"
            "sys.stdout.buffer.write(output)\n"
            "sys.stderr.buffer.write(result.stderr)\n"
            "sys.exit(result.returncode)\n", encoding="utf-8")
        tool.chmod(0o755)
        self.env["PATH"] = str(tools) + os.pathsep + self.env["PATH"]


class FixtureHelperTests(unittest.TestCase):
    def test_symlink_permission_error_skips_fixture(self):
        case = VersionTests()
        with mock.patch('os.symlink', side_effect=OSError(1314, 'privilege not held')):
            with self.assertRaisesRegex(unittest.SkipTest, 'fixture needs directory symlinks'):
                case.fixture_symlink(Path('unused-link'), Path('unused-target'),
                                     target_is_directory=True)

    def test_rmtree_removes_read_only_tree(self):
        tree = Path(tempfile.mkdtemp(prefix='trusscli-cleanup-'))
        self.addCleanup(lambda: fixture_rmtree(tree) if tree.exists() else None)
        objects = tree / 'objects'
        objects.mkdir()
        (objects / 'object').write_bytes(b'fixture')
        (objects / 'object').chmod(stat.S_IREAD)
        fixture_rmtree(tree)
        self.assertFalse(tree.exists())

    def test_rmtree_retries_both_python_callback_contracts(self):
        for version, keyword in (((3, 11), 'onerror'), ((3, 12), 'onexc')):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as scratch:
                path = Path(scratch) / 'object'
                path.write_bytes(b'fixture')
                path.chmod(stat.S_IREAD)
                failure = PermissionError('read-only Git object')

                def remove(tree, **callbacks):
                    self.assertEqual(Path(tree), Path(scratch))
                    self.assertEqual(list(callbacks), [keyword])

                    def retry(filename):
                        self.assertTrue(Path(filename).stat().st_mode & stat.S_IWRITE)
                        os.unlink(filename)

                    exception = failure if keyword == 'onexc' else (PermissionError, failure, None)
                    callbacks[keyword](retry, str(path), exception)

                with mock.patch.object(sys, 'version_info', version), \
                     mock.patch.object(shutil, 'rmtree', side_effect=remove):
                    fixture_rmtree(scratch)
                self.assertFalse(path.exists())


if __name__ == "__main__":
    unittest.main()
