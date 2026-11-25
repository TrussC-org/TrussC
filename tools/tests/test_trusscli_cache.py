"""Offline cache commands; temporary file:// repositories and a private cache only."""
import datetime
import os
from pathlib import Path
import subprocess
import shutil
import unittest

from test_trusscli_addon_versions import AddonVersionTests
from test_trusscli_versions import CLI


class CacheTests(AddonVersionTests):
    def test_cache_remove_unique_prefix(self):
        for addon, commit in ((False, self.initial), (True, self.addon_initial)):
            spec = ("alice/tcxFixture@" if addon else "") + commit
            self.cli(*(("addon", "add", spec) if addon else ("use", spec)))
            root = self.cache / ("addons/alice/tcxFixture" if addon else "versions/TrussC-org/TrussC") / commit
            args = ("--addon", "alice/tcxFixture@" + commit[:7]) if addon else (commit[:7],)
            self.cli("cache", "remove", *args)
            self.assertFalse(root.exists())
            if not addon:
                self.cli("use", "v1.0.0")

    def test_cache_remove_shared_prefix_lists_candidates_and_keeps_trees(self):
        for addon in (False, True):
            parent = self.cache / ("addons/alice/tcxFixture" if addon else "versions/TrussC-org/TrussC")
            refs = ["123abcd" + "1" * 33, "123abcd" + "2" * 33]
            for ref in refs:
                root = parent / ref
                root.mkdir(parents=True)
                (root / ".git_archival.txt").write_text("describe: fixture\n", encoding="utf-8")
            prefix = "alice/tcxFixture@" if addon else ""
            args = ("--addon", prefix + "123abcd") if addon else ("123abcd",)
            output = self.cli("cache", "remove", *args, success=False)
            for ref in refs:
                self.assertIn(ref, output)
                self.assertTrue((parent / ref).is_dir())
            self.assertIn("longer prefix", output)

    def fixture_cache(self):
        self.cli("addon", "add", "tcxFixture@v1")
        self.worktree = self.cache / "versions/TrussC-org/TrussC/v1.0.0"
        self.addon_worktree = self.cache / "addons/alice/tcxFixture/v1"
        for source, name, ref in ((self.source, "TrussC-org/TrussC", "main"),
                                  (self.addon, "alice/tcxFixture", "feature/next")):
            archive = self.base / "remotes" / name / "archive" / (ref + ".tar.gz")
            archive.parent.mkdir(parents=True, exist_ok=True)
            self.git("-C", source, "archive", "--format=tar.gz", "--prefix=fixture/",
                     "--output=" + str(archive), "HEAD")
        self.env["TRUSSC_FETCH"] = "tarball"
        # Keep the worktree addon selection out of the tarball refresh.
        (self.project / "addons.make").write_text("", encoding="utf-8")
        self.cli("use", "main")
        self.cli("addon", "add", "alice/tcxFixture@feature/next")
        self.tarball = self.cache / "versions/TrussC-org/TrussC/main"
        self.addon_tarball = self.cache / "addons/alice/tcxFixture/feature/next"
        self.env.pop("TRUSSC_FETCH")
        self.versions = [self.worktree, self.tarball, self.addon_worktree, self.addon_tarball]

    def command(self, command, answer=None):
        if answer is None:
            result = subprocess.run([str(CLI), "cache", command], cwd=self.base, env=self.env,
                                    input="y\n", capture_output=True, text=True, encoding="utf-8", timeout=20)
        else:
            if not hasattr(os, "openpty"):
                self.skipTest("interactive stdin requires a POSIX pseudo-terminal")
            master, slave = os.openpty()
            try:
                os.write(master, (answer + "\n").encode())
                result = subprocess.run([str(CLI), "cache", command], cwd=self.base, env=self.env,
                                        stdin=slave, capture_output=True, text=True, encoding="utf-8", timeout=20)
            finally:
                os.close(master)
                os.close(slave)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def test_cache_empty_does_not_create_folders(self):
        empty = self.base / "empty-cache"
        self.env["TRUSSC_CACHE_DIR"] = str(empty)
        self.assertIn("Total: 0 B", self.command("list"))
        self.assertIn("Total: 0 B", self.command("autoremove"))
        self.assertIn("interactively in a terminal", self.command("clear"))
        self.assertFalse(empty.exists())

    def test_cache_list_four_kinds_sizes_and_dates(self):
        self.fixture_cache()
        (self.tarball / ".last-used").unlink()
        for root in self.versions:
            size = sum(p.stat().st_size for p in root.rglob("*")
                       if not p.is_symlink() and p.is_file())
            self.assertLessEqual(size, 1024**2)
            with (root / "padding").open("wb") as padding:
                padding.truncate(1024**2 - size)
        output = self.command("list")
        self.assertIn("TrussC TrussC-org/TrussC@v1.0.0 worktree", output)
        self.assertIn("TrussC TrussC-org/TrussC@main tarball", output)
        self.assertIn("addon alice/tcxFixture@v1 worktree", output)
        self.assertIn("addon alice/tcxFixture@feature/next tarball", output)
        self.assertIn(" 1.0 MB -\n", output)
        self.assertEqual(output.count(" 1.0 MB "), 4)
        self.assertIn("Total: 4.0 MB", output)
        self.assertEqual(len(output.splitlines()), 5)

    def test_cache_size_boundaries(self):
        self.env["TRUSSC_CACHE_DIR"] = str(self.base / "size-cache")
        root = self.base / "size-cache/versions/TrussC-org/TrussC/size-test"
        root.mkdir(parents=True)
        (root / ".git_archival.txt").touch()
        marker = root / ".last-used"
        marker.write_text("2000-01-01\n", encoding="utf-8")
        for size, expected in ((512, "512 B"), (1023, "1023 B"),
                               (1024, "1.0 KB"), (1536, "1.5 KB"),
                               (1024**2, "1.0 MB"), (1024**3, "1.0 GB")):
            with self.subTest(size=size):
                # Sparse files exercise GB output without allocating GB of storage.
                with (root / "payload").open("wb") as payload:
                    payload.truncate(size - marker.stat().st_size)
                for command in ("list", "autoremove", "clear"):
                    output = self.command(command)
                    self.assertIn(f"Total: {expected}\n" if command != "list" else
                                  f"Total: {expected} (versions; bare repos excluded)\n", output)
                    if command != "clear":
                        self.assertIn(f"tarball {expected} 2000-01-01\n", output)

    def test_cache_clear_answers_and_symlinks(self):
        self.fixture_cache()
        outside = self.base / "outside"
        outside.mkdir()
        sentinel = outside / "keep"
        sentinel.write_text("untouched", encoding="utf-8")
        self.fixture_symlink(self.worktree / "outside", outside, target_is_directory=True)
        self.fixture_symlink(self.cache / "outside", outside, target_is_directory=True)
        self.assertIn("interactively in a terminal", self.command("clear"))
        self.assertTrue(self.worktree.exists())
        for answer in ("n", ""):
            self.command("clear", answer)
            self.assertTrue(self.worktree.exists())
        self.command("clear", "y")
        self.assertTrue(self.cache.is_dir())
        self.assertEqual(list(self.cache.iterdir()), [])
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "untouched")

    def test_cache_autoremove_boundary_and_reacquisition(self):
        self.fixture_cache()
        today = datetime.date.today()
        for root, age in zip(self.versions, (90, 91, 89, None)):
            marker = root / ".last-used"
            if age is None:
                marker.unlink()
            else:
                marker.write_text(str(today - datetime.timedelta(days=age)) + "\n", encoding="utf-8")
        outside = self.base / "keep"
        outside.write_text("untouched", encoding="utf-8")
        self.fixture_symlink(self.worktree / "link", outside)
        for answer in (None, "n", ""):
            output = self.command("autoremove", answer)
            self.assertIn("@v1.0.0 worktree", output)
            self.assertIn("@main tarball", output)
            self.assertNotIn("alice/tcxFixture", output)
            self.assertTrue(all(p.exists() for p in self.versions))
        self.command("autoremove", "y")
        self.assertFalse(self.worktree.exists())
        self.assertFalse(self.tarball.exists())
        self.assertTrue(self.addon_worktree.exists())
        self.assertTrue(self.addon_tarball.exists())
        bare = self.cache / "repos/TrussC-org/TrussC.git"
        self.assertNotIn(self.worktree.as_posix(), self.git("--git-dir=" + str(bare), "worktree", "list"))
        self.assertEqual(outside.read_text(encoding="utf-8"), "untouched")
        (self.project / "addons.make").write_text("alice/tcxFixture@v1\n", encoding="utf-8")
        (self.project / ".trussc-version").write_text("v1.0.0\n", encoding="utf-8")
        self.cli("update")
        self.assertTrue(self.worktree.is_dir())

    def test_cache_autoremove_addons_and_invalid_dates(self):
        self.fixture_cache()
        for root in self.versions:
            (root / ".last-used").write_text("2026-02-30\n", encoding="utf-8")
        self.assertNotIn("@", self.command("autoremove", "y"))
        for root in (self.addon_worktree, self.addon_tarball):
            (root / ".last-used").write_text("2000-01-01\n", encoding="utf-8")
        self.command("autoremove", "y")
        self.assertFalse(self.addon_worktree.exists())
        self.assertFalse(self.addon_tarball.exists())
        bare = self.cache / "repos/alice/tcxFixture.git"
        self.assertNotIn(self.addon_worktree.as_posix(), self.git("--git-dir=" + str(bare), "worktree", "list"))
        (self.project / "addons.make").write_text("alice/tcxFixture@v1\n", encoding="utf-8")
        (self.project / ".trussc-version").write_text("v1.0.0\n", encoding="utf-8")
        self.cli("update")
        self.assertTrue(self.addon_worktree.is_dir())

    def test_cache_usage_acquisition_build_update_and_local(self):
        self.cli("addon", "add", "tcxFixture@v1")
        roots = [self.root(), Path(self.paths()["tcxFixture"])]
        today = str(datetime.date.today()) + "\n"
        for root in roots:
            self.assertEqual((root / ".last-used").read_text(encoding="utf-8"), today)
        for command in (("build", "--dry-run"), ("update",)):
            for root in roots:
                (root / ".last-used").write_text("2000-01-01\n", encoding="utf-8")
            self.cli(*command)
            for root in roots:
                self.assertEqual((root / ".last-used").read_text(encoding="utf-8"), today)
                os.utime(root / ".last-used", ns=(1000000000, 1000000000))
            self.cli(*command)
            for root in roots:
                self.assertEqual((root / ".last-used").stat().st_mtime_ns, 1000000000)
        self.cli("addon", "add-local", "tcxFixture", self.addon)
        self.cli("use-local", self.source)
        self.cli("build", "--dry-run")
        self.assertFalse((self.source / ".last-used").exists())
        self.assertFalse((self.addon / ".last-used").exists())
        self.assertFalse((roots[0] / "addons/tcxBundled/.last-used").exists())

    def test_cache_tarball_update_preserves_same_day_usage(self):
        self.fixture_cache()
        self.env["TRUSSC_FETCH"] = "tarball"
        for root in (self.tarball, self.addon_tarball):
            os.utime(root / ".last-used", ns=(1000000000, 1000000000))
        self.cli("update")
        for root in (self.tarball, self.addon_tarball):
            self.assertEqual((root / ".last-used").stat().st_mtime_ns, 1000000000)

    def test_cache_tarball_update_does_not_copy_through_metadata_symlink(self):
        self.fixture_cache()
        self.fixture_symlink(self.addon / ".last-used", "src/value.h")
        self.commit_addon()
        archive = self.base / "remotes/alice/tcxFixture/archive/feature/next.tar.gz"
        self.git("-C", self.addon, "archive", "--format=tar.gz", "--prefix=fixture/",
                 "--output=" + str(archive), "HEAD")
        self.env["TRUSSC_FETCH"] = "tarball"
        output = self.cli("update")
        self.assertIn("Warning: cannot record cache usage", output)
        self.assertTrue((self.addon_tarball / ".last-used").is_symlink())
        self.assertEqual((self.addon_tarball / "src/value.h").read_text(encoding="utf-8"),
                         (self.addon / "src/value.h").read_text(encoding="utf-8"))

    def test_cache_remove_each_kind_and_reacquire(self):
        self.fixture_cache()
        outside = self.base / "keep"
        outside.mkdir()
        (outside / "sentinel").write_text("untouched", encoding="utf-8")
        self.fixture_symlink(self.worktree / "outside", outside, target_is_directory=True)
        cases = [(self.worktree, ("v1.0.0",)),
                 (self.tarball, ("main",)),
                 (self.addon_worktree, ("--addon", "tcxFixture@v1")),
                 (self.addon_tarball, ("--addon", "alice/tcxFixture@feature/next"))]
        for index, (root, args) in enumerate(cases):
            with self.subTest(args=args):
                output = self.cli("cache", "remove", *args)
                self.assertNotIn("[y/N]", output)
                self.assertFalse(root.exists())
                self.assertTrue(all(p.exists() for p, _ in cases[index + 1:]))
        self.assertEqual((outside / "sentinel").read_text(encoding="utf-8"), "untouched")
        for name, root in (("TrussC-org/TrussC", self.worktree),
                           ("alice/tcxFixture", self.addon_worktree)):
            bare = self.cache / "repos" / (name + ".git")
            self.assertTrue(bare.is_dir())
            self.assertNotIn(root.as_posix(), self.git("--git-dir=" + str(bare), "worktree", "list"))
        (self.project / ".trussc-version").write_text("v1.0.0\n", encoding="utf-8")
        (self.project / "addons.make").write_text("alice/tcxFixture@v1\n", encoding="utf-8")
        self.cli("update")
        self.assertTrue(self.worktree.is_dir())
        self.assertTrue(self.addon_worktree.is_dir())
        self.assertFalse(self.tarball.exists())
        self.assertFalse(self.addon_tarball.exists())

    def test_cache_remove_same_spec_separate_namespaces(self):
        shutil.copytree(self.source / "core", self.addon / "core")
        self.commit_addon()
        self.git("-C", self.base / "remotes/alice/tcxFixture.git", "fetch", self.addon,
                 "+refs/heads/*:refs/heads/*")
        spec = "alice/tcxFixture@main"
        self.cli("use", spec)
        self.cli("addon", "add", spec)
        core = self.cache / "versions/alice/tcxFixture/main"
        addon = self.cache / "addons/alice/tcxFixture/main"
        self.cli("cache", "remove", spec)
        self.assertFalse(core.exists())
        self.assertTrue(addon.is_dir())
        self.cli("update")
        self.cli("cache", "remove", "--addon", spec)
        self.assertTrue(core.is_dir())
        self.assertFalse(addon.exists())

    def test_cache_remove_unknown_and_local_folders(self):
        self.fixture_cache()
        (self.project / "addons.make").write_text("", encoding="utf-8")
        self.cli("use-local", self.source)
        self.cli("addon", "add-local", "tcxFixture", self.addon)
        before = self.command("list")
        for args in (("missing",), ("--addon", "tcxFixture@missing"),
                     ("v1.0.0/core",), ("--addon", "tcxFixture@absent-feature"),
                     (str(self.source),), ("--addon", str(self.addon))):
            output = self.cli("cache", "remove", *args, success=False)
            # Local absolute paths are rejected by the existing parsers.
            if not os.path.isabs(args[-1]):
                self.assertIn(args[-1], output)
                self.assertIn("trusscli cache list", output)
            self.assertEqual(self.command("list"), before)
        self.assertTrue((self.source / "core/cmake/trussc_app.cmake").is_file())
        self.assertTrue((self.addon / "addon.json").is_file())

    def test_cache_remove_ambiguous_addon_needs_owner(self):
        self.cli("addon", "add", "alice/tcxFixture@v1")
        self.cli("addon", "add", "bob/tcxFixture@v1")
        before = self.command("list")
        output = self.cli("cache", "remove", "--addon", "tcxFixture@v1", success=False)
        self.assertIn("owner/repo@ref", output)
        self.assertEqual(self.command("list"), before)
        self.cli("cache", "remove", "--addon", "bob/tcxFixture@v1")
        self.assertTrue((self.cache / "addons/alice/tcxFixture/v1").is_dir())

    def test_cache_usage_warning_every_time_and_metadata_symlink(self):
        marker = self.root() / ".last-used"
        marker.unlink()
        target = self.base / "keep"
        target.write_text("untouched", encoding="utf-8")
        self.fixture_symlink(marker, target)
        for _ in range(2):
            self.assertIn("Warning: cannot record cache usage", self.cli("build", "--dry-run"))
        self.assertEqual(target.read_text(encoding="utf-8"), "untouched")
        marker.unlink()
        marker.mkdir()
        for _ in range(2):
            self.assertIn("Warning: cannot record cache usage", self.cli("build", "--dry-run"))

    def test_cache_unknown(self):
        self.assertIn("unknown cache command", self.cli("cache", "unknown", success=False))
        self.assertIn("cache list", self.cli("cache", "remove", "absent-version", success=False))

    def test_cache_symlinked_root_with_and_without_trailing_slash(self):
        target = self.base / "cache-target"
        target.mkdir()
        link = self.base / "cache-link"
        self.fixture_symlink(link, target, target_is_directory=True)
        for suffix in ("", "/"):
            with self.subTest(suffix=suffix):
                self.env["TRUSSC_CACHE_DIR"] = str(link) + suffix
                self.cli("update")
                root = target / "versions/TrussC-org/TrussC/v1.0.0"
                self.assertTrue((root / ".last-used").is_file())
                self.assertIn("TrussC-org/TrussC@v1.0.0 worktree", self.command("list"))
                self.cli("cache", "remove", "v1.0.0")
                self.assertFalse(root.exists())
                self.cli("update")
                (root / ".last-used").write_text("2000-01-01\n", encoding="utf-8")
                self.command("autoremove", "y")
                self.assertFalse(root.exists())
                self.cli("update")
                (target / "loose-file").write_text("cache data", encoding="utf-8")
                self.command("clear", "y")
                self.assertTrue(link.is_symlink())
                self.assertTrue(target.is_dir())
                self.assertEqual(list(target.iterdir()), [])


def load_tests(loader, tests, pattern):
    return unittest.TestSuite(CacheTests(name) for name in loader.getTestCaseNames(CacheTests)
                              if name.startswith("test_cache_"))


if __name__ == "__main__":
    unittest.main()
