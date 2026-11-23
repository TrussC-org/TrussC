"""Offline per-project version checks. Set TRUSSCLI to a CLI built from this tree.

Run through tcbuild.sh: one check configures and builds a tiny fixture app.
All Git writes are confined to temporary fake repositories, never this checkout.
"""
import hashlib
import platform
import io
import tarfile
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

DELEGATE_STUB = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        if (getenv("TRUSSC_DELEGATED_ROOT")) return 98;
        const char* probe = getenv("TRUSSC_STUB_PROBE");
        if (probe && strcmp(probe, "sleep") == 0) {
#ifdef _WIN32
            Sleep(60000);
#else
            sleep(60);
#endif
        }
        return probe && strcmp(probe, "fail") == 0 ? 127 : 0;
    }
    const char* output = getenv("TRUSSC_STUB_OUTPUT");
    FILE* file = fopen(output, "w");
    if (!file) return 99;
    fprintf(file, "%s\n", getenv("TRUSSC_DELEGATED_ROOT"));
    for (int i = 1; i < argc; ++i) fprintf(file, "%s\n", argv[i]);
    fclose(file);
    puts("delegate stdout");
    fputs("delegate stderr\n", stderr);
    const char* code = getenv("TRUSSC_STUB_EXIT");
    return code ? atoi(code) : 0;
}
"""


@unittest.skipUnless(CLI.is_file(), "build this tree's CLI and set TRUSSCLI")
class VersionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.release_scratch = tempfile.TemporaryDirectory(prefix="trusscli-release-stub-")
        cls.addClassCleanup(cls.release_scratch.cleanup)
        source = Path(cls.release_scratch.name).resolve() / "stub.c"
        source.write_text(DELEGATE_STUB, encoding="utf-8")
        cls.release_stub = source.with_name("stub.exe" if sys.platform == "win32" else "stub")
        subprocess.run([os.environ.get("CC", "cc"), str(source), "-o", str(cls.release_stub)], check=True)

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="trusscli-versions-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name).resolve()
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

    def publish_delegate(self, broken=False):
        tools = self.source / "tools"
        tools.mkdir(exist_ok=True)
        (tools / "trusscli-protocol").write_text("1\n", encoding="utf-8")
        (tools / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(stub C)\n'
            'add_executable(trusscli stub.c)\n'
            'if(APPLE)\nset_target_properties(trusscli PROPERTIES MACOSX_BUNDLE TRUE)\nendif()\n',
            encoding="utf-8")
        (tools / "stub.c").write_text("invalid C\n" if broken else DELEGATE_STUB, encoding="utf-8")
        self.advance()
        self.git("-C", self.source, "push", self.base / "remotes/TrussC-org/TrussC.git", "main")
        self.env["TRUSSC_STUB_OUTPUT"] = str(self.base / "delegate argv")

    def delegate_binary(self, root=None):
        relative = {"darwin": "trusscli.app/Contents/MacOS/trusscli",
                    "win32": "trusscli.exe"}.get(sys.platform, "trusscli")
        return (root or self.root()) / "tools/bin" / relative

    def release(self, ref="vRelease", owner="TrussC-org", unsafe=False, dotted=False, locked=False, stub=None):
        base = Path(self.env["TRUSSC_GITHUB_BASE_URL"].removeprefix("file://"))
        folder = base / owner / "TrussC/releases/download" / ref
        folder.mkdir(parents=True, exist_ok=True)
        arch = {"amd64": "x86_64", "aarch64": "arm64"}.get(platform.machine().lower(), platform.machine().lower())
        asset = folder / f"trusscli-{ref}-{NATIVE}-{arch}.tar.gz"
        with tarfile.open(asset, "w:gz") as archive:
            if locked:
                directory = tarfile.TarInfo("locked")
                directory.type = tarfile.DIRTYPE
                directory.mode = 0
                archive.addfile(directory)
            name = "../outside" if unsafe else str(self.delegate_binary(Path("root")).relative_to("root/tools/bin"))
            if dotted:
                root = tarfile.TarInfo("./")
                root.type = tarfile.DIRTYPE
                root.mode = 0o755
                archive.addfile(root)
                name = "./" + name
            info = tarfile.TarInfo(name)
            data = (stub or self.release_stub).read_bytes()
            info.size = len(data)
            info.mode = 0o644  # Acquisition must add Unix execute permissions.
            archive.addfile(info, io.BytesIO(data))
        sums = folder / f"trusscli-{ref}-SHA256SUMS.txt"
        sums.write_text(hashlib.sha256(asset.read_bytes()).hexdigest() + "  " + asset.name + "\n", encoding="utf-8")
        return asset, sums

    def release_source(self):
        self.publish_delegate()
        self.git("-C", self.source, "tag", "vRelease")
        for owner in ("TrussC-org", "tettou771"):
            self.git("-C", self.source, "push", self.base / f"remotes/{owner}/TrussC.git", "main", "vRelease")

    def test_release_tag_and_fork_download_without_build(self):
        self.release_source()
        for owner in ("TrussC-org", "tettou771"):
            with self.subTest(owner=owner):
                self.release(owner=owner, dotted=owner == "tettou771")
                output = self.cli("use", f"{owner}/TrussC@vRelease")
                self.assertNotIn("first time takes", output)
                self.assertTrue(self.delegate_binary().is_file())
                self.assertFalse((self.root() / "tools/trusscli-build.log").exists())
                self.assertFalse((self.root() / "tools/build-trusscli").exists())
                self.assertIn("delegate stdout", self.cli("build", "--dry-run"))
                self.assertEqual(self.delegate_binary().read_bytes(), self.release_stub.read_bytes())
                self.assertEqual(list((self.root() / "tools").glob(".trusscli-release-*")), [])

    def test_packaged_release_round_trip(self):
        self.release_source()
        arch = {"amd64": "x86_64", "aarch64": "arm64"}.get(platform.machine().lower(), platform.machine().lower())
        folder = self.base / "remotes/TrussC-org/TrussC/releases/download/vRelease"
        bin_dir = self.base / "release-bin"
        binary = self.delegate_binary(Path("root")).relative_to("root/tools/bin")
        (bin_dir / binary).parent.mkdir(parents=True)
        shutil.copy2(self.release_stub, bin_dir / binary)
        script = ROOT / "tools/package_trusscli_release.py"
        subprocess.run([sys.executable, "-I", str(script), "package", "--tag", "vRelease",
                        "--os", NATIVE, "--arch", arch, "--bin-dir", str(bin_dir), "--out", str(folder)], check=True)
        subprocess.run([sys.executable, "-I", str(script), "checksums", "--tag", "vRelease", "--dir", str(folder)], check=True)
        self.assertNotIn("first time takes", self.cli("use", "vRelease"))
        self.assertEqual(self.delegate_binary().read_bytes(), self.release_stub.read_bytes())
        self.assertFalse((self.root() / "tools/trusscli-build.log").exists())
        self.assertIn("delegate stdout", self.cli("build", "--dry-run"))

    def test_release_probe_failures_build_local_delegate(self):
        self.release_source()
        source = self.base / "release-only.c"
        source.write_text(DELEGATE_STUB.replace('"delegate stdout"', '"release-only delegate stdout"'), encoding="utf-8")
        stub = source.with_suffix(".exe" if sys.platform == "win32" else ".bin")
        subprocess.run([os.environ.get("CC", "cc"), str(source), "-o", str(stub)], check=True)
        for probe, reason in (("fail", "exit 127"), ("sleep", "timeout")):
            with self.subTest(probe=probe):
                self.env["TRUSSC_CACHE_DIR"] = str(self.cache / probe)
                self.env["TRUSSC_STUB_PROBE"] = probe
                self.release(stub=stub)
                output = self.cli("use", "vRelease")
                self.assertEqual(output.count("building it from source"), 1, output)
                self.assertIn("does not run here (" + reason + ")", output)
                self.assertTrue((self.root() / "tools/trusscli-build.log").is_file())
                self.assertTrue(self.delegate_binary().is_file())
                self.assertNotEqual(self.delegate_binary().read_bytes(), stub.read_bytes())
                self.assertEqual(list((self.root() / "tools").glob(".trusscli-release-*")), [])
                self.assertIn("delegate stdout", self.cli("build", "--dry-run"))
        self.env.pop("TRUSSC_STUB_PROBE")

    def test_release_probe_spawn_failure_builds_local_delegate(self):
        self.release_source()
        stub = self.base / "invalid-binary"
        stub.write_bytes(b"This is not an executable format.\n")
        stub.chmod(0o755)
        self.release(stub=stub)
        output = self.cli("use", "vRelease")
        reasons = [line for line in output.splitlines() if "building it from source" in line]
        self.assertEqual(len(reasons), 1, output)
        self.assertIn("does not run here (cannot start)", reasons[0])
        self.assertNotIn("failed to spawn", output)
        self.assertNotIn(".trusscli-release-", output)
        self.assertTrue((self.root() / "tools/trusscli-build.log").is_file())
        self.assertTrue(self.delegate_binary().is_file())
        self.assertNotEqual(self.delegate_binary().read_bytes(), stub.read_bytes())
        self.assertEqual(list((self.root() / "tools").glob(".trusscli-release-*")), [])
        self.assertIn("delegate stdout", self.cli("build", "--dry-run"))

    def test_release_failures_fall_back_once(self):
        self.release_source()
        for failure, reason in (("mismatch", "SHA-256 mismatch"), ("asset", "asset"),
                                ("checksum", "checksum download"), ("release", "asset"),
                                ("unsafe", "unsafe TrussC archive path"),
                                ("permissions", "unsafe trusscli archive directory permissions"),
                                ("large-asset", "asset"), ("large-checksum", "checksum download")):
            with self.subTest(failure=failure):
                self.env["TRUSSC_CACHE_DIR"] = str(self.cache / failure)
                asset, sums = self.release(unsafe=failure == "unsafe", locked=failure == "permissions")
                if failure == "large-asset":
                    with asset.open("r+b") as file:
                        file.truncate(64 * 1024 * 1024 + 1)
                elif failure == "large-checksum":
                    sums.write_bytes(b"x" * (64 * 1024 + 1))
                elif failure == "mismatch":
                    sums.write_text("0" * 64 + "  " + asset.name + "\n", encoding="utf-8")
                elif failure == "asset":
                    asset.rename(asset.with_name("other-platform.tar.gz"))
                elif failure == "checksum":
                    sums.unlink()
                elif failure == "release":
                    shutil.rmtree(asset.parent)
                output = self.cli("use", "vRelease")
                lines = [line for line in output.splitlines() if "building it from source" in line]
                self.assertEqual(len(lines), 1, output)
                self.assertIn(reason, lines[0])
                self.assertEqual(output.count("first time takes a few minutes"), 1)
                self.assertTrue((self.root() / "tools/trusscli-build.log").is_file())
                self.assertTrue(self.delegate_binary().is_file())
                self.assertEqual(list((self.root() / "tools").glob(".trusscli-release-*")), [])
                self.assertFalse((self.root() / "tools/outside").exists())

    def test_branch_and_commit_ignore_release_and_vendor_is_unchanged(self):
        self.release_source()
        commit = self.git("-C", self.source, "rev-parse", "HEAD").strip()
        for ref in ("main", commit):
            with self.subTest(ref=ref):
                self.release(ref)
                output = self.cli("use", ref)
                self.assertNotIn("release asset", output)
                self.assertTrue((self.root() / "tools/trusscli-build.log").is_file())
                # Preserve the serialized vendor object, including localPath and addon marks.
                presets = self.project / "CMakePresets.json"
                text = presets.read_text(encoding="utf-8")
                vendor = '{"trussc": {"addonLocalPaths": {"tcxFixture": "fixture addon"}, "addonPaths": {"tcxFixture": "resolved fixture"}, "addonOrder": ["tcxFixture"]}, "fixture": [1, 2]}'
                data = json.loads(text)
                # Write deliberately noncanonical whitespace to catch any rewrite.
                data.pop("vendor")
                text = json.dumps(data, indent=2)[:-1] + ', "vendor": ' + vendor + '}\n'
                presets.write_text(text, encoding="utf-8")
                before = presets.read_bytes()
                self.cli("build", "--dry-run")
                self.assertEqual(presets.read_bytes(), before)
                # The real delegate's build path also preserves local selection marks.
                vendor = '{"trussc": {"localPath": ' + json.dumps(str(self.source)) + ', "addonLocalPaths": {"tcxFixture": "fixture addon"}, "addonPaths": {"tcxFixture": "resolved fixture"}, "addonOrder": ["tcxFixture"]}}'
                data["vendor"] = json.loads(vendor)
                text = json.dumps(data, indent=2)
                presets.write_text(text, encoding="utf-8")
                before = presets.read_bytes()
                self.env["TRUSSC_DELEGATED_ROOT"] = str(self.root())
                try:
                    self.cli("build", "--dry-run")
                    self.assertEqual(presets.read_bytes(), before)
                finally:
                    self.env.pop("TRUSSC_DELEGATED_ROOT")

    def test_delegate_acquisition_arguments_streams_exit_and_missing_binary(self):
        self.publish_delegate()
        output = self.cli("use", "main")
        self.assertEqual(output.count("first time takes a few minutes"), 1)
        self.assertTrue(self.delegate_binary().is_file())
        self.assertFalse((self.root() / "tools/build-trusscli").exists())
        self.assertNotIn("first time takes a few minutes", self.cli("update"))
        self.assertIn("Delegate trusscli", self.cli("doctor"))
        args = ["build", "--dry-run", "-p", str(self.project), "--release", "-j", "2", "--future-build-option"]
        self.env["TRUSSC_STUB_EXIT"] = "23"
        result = subprocess.run([str(CLI), *args], cwd=self.project, env=self.env,
                                capture_output=True, text=True, encoding="utf-8", timeout=20)
        self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
        self.assertIn("delegate stdout", result.stdout)
        self.assertIn("delegate stderr", result.stderr)
        received = (self.base / "delegate argv").read_text(encoding="utf-8").splitlines()
        self.assertEqual(received, [str(self.root()), *args])
        self.delegate_binary().unlink()
        (self.base / "delegate argv").unlink()
        self.assertIn("run `trusscli update`", self.cli("build", success=False))
        self.assertFalse((self.base / "delegate argv").exists())
        self.assertFalse((self.root() / "tools/build-trusscli").exists())
        self.assertEqual(self.cli("update").count("first time takes a few minutes"), 1)

    def test_delegate_branch_refresh_rebuilds_and_failure_removes_old_binary(self):
        self.publish_delegate()
        self.cli("use", "main")
        self.advance()
        self.git("-C", self.source, "push", self.base / "remotes/TrussC-org/TrussC.git", "main")
        self.assertEqual(self.cli("update").count("first time takes a few minutes"), 1)
        self.publish_delegate(broken=True)
        output = self.cli("update", success=False)
        self.assertIn("delegate trusscli build failed; log:", output)
        self.assertTrue((self.root() / "tools/trusscli-build.log").is_file())
        self.assertFalse(self.delegate_binary().exists())
        self.assertIn("run `trusscli update`", self.cli("build", success=False))

    def test_delegate_first_use_build_failure_and_recursion_guard(self):
        self.publish_delegate(broken=True)
        self.assertIn("delegate trusscli build failed; log:", self.cli("use", "main", success=False))
        # No presets have been written yet; inspect the selected cache directly.
        root = self.cache / "versions/TrussC-org/TrussC/main"
        self.assertFalse(self.delegate_binary(root).exists())
        self.publish_delegate()
        self.cli("update")
        # The delegate must use the supplied root, even with an unavailable pin.
        (self.project / ".trussc-version").write_text("missing\n", encoding="utf-8")
        self.env["TRUSSC_DELEGATED_ROOT"] = str(root)
        output = self.cli("build", "--dry-run")
        self.assertIn("-DTRUSSC_DIR=" + str(root / "core"), output)
        self.assertNotIn("delegate stdout", output)
        self.assertFalse((self.base / "delegate argv").exists())

    def test_no_marker_notice_repeats_and_local_selection_does_not_delegate(self):
        self.cli("use", "v1")
        for _ in range(2):
            self.assertIn("v1 has no trusscli delegation support; building with this trusscli",
                          self.cli("build", "--dry-run"))
        self.publish_delegate()
        self.cli("use-local", self.source)
        self.assertNotIn("first time takes a few minutes", self.cli("update"))
        output = self.cli("build", "--dry-run")
        self.assertIn("-DTRUSSC_DIR=" + str(self.source / "core"), output)
        self.assertNotIn("delegation support", output)
        self.assertFalse((self.base / "delegate argv").exists())

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

    @unittest.skipIf(sys.platform == "win32", "fixture uses /dev/zero")
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


@unittest.skipUnless(CLI.is_file(), "build this tree's CLI and set TRUSSCLI")
class TarballTests(unittest.TestCase):
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

    publish_delegate = VersionTests.publish_delegate
    delegate_binary = VersionTests.delegate_binary

    def test_archive_delegate_is_built_once_and_refreshed_with_source(self):
        self.publish_delegate()
        self.archive("main")
        self.assertEqual(self.cli("use", "main").count("first time takes a few minutes"), 1)
        self.assertNotIn("first time takes a few minutes", self.cli("update"))
        self.assertIn("delegate stdout", self.cli("build", "--dry-run"))
        self.advance()
        self.archive("main")
        self.assertEqual(self.cli("update").count("first time takes a few minutes"), 1)
        self.assertTrue(self.delegate_binary().is_file())

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
                self.assertIn("-DTRUSSC_DIR=" + str(root / "core"), output)

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
        for ref in ("missing", "truncated"):
            with self.subTest(ref=ref):
                if ref == "truncated":
                    data = self.archive_path("v1").read_bytes()
                    self.archive_path(ref).write_bytes(data[:len(data) // 2])
                self.cli("use", ref, success=False)
                root = self.cache / "versions/TrussC-org/TrussC" / ref
                self.assertFalse(root.exists())
                self.assertEqual(list(root.parent.iterdir()), [])
                self.assertIn("run `trusscli update`", self.cli("build", "--dry-run", success=False))

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

    @unittest.skipIf(sys.platform == "win32", "fixture needs directory symlinks")
    def test_internal_symlink_is_supported(self):
        (self.source / "alias").symlink_to("core", target_is_directory=True)
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
        (versions / "TrussC-org").symlink_to(outside, target_is_directory=True)
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
            (path / tool).symlink_to(shutil.which(tool))
        self.env.pop("TRUSSC_FETCH")
        self.env["PATH"] = str(path)
        self.assertNotIn("Error:", self.cli("use", "v1"))
        self.assertFalse((self.cache / "repos").exists())
        self.assertIn("(v1)", self.cli("build", "--dry-run"))


@unittest.skipUnless(CLI.is_file(), "build this tree's CLI and set TRUSSCLI")
class ArchiveReleaseTests(unittest.TestCase):
    setUpClass = classmethod(VersionTests.setUpClass.__func__)
    setUp = TarballTests.setUp
    git = VersionTests.git
    advance = VersionTests.advance
    cli = VersionTests.cli
    root = VersionTests.root
    publish_delegate = VersionTests.publish_delegate
    delegate_binary = VersionTests.delegate_binary
    archive_path = TarballTests.archive_path
    archive = TarballTests.archive
    release = VersionTests.release

    def test_qualified_tag_downloads_but_short_branch_archive_builds(self):
        self.publish_delegate()
        self.git("-C", self.source, "tag", "vRelease")
        self.archive("vRelease", path="refs/tags/vRelease")
        self.release()
        output = self.cli("use", "vRelease")
        self.assertNotIn("first time takes", output)
        self.assertFalse((self.root() / "tools/trusscli-build.log").exists())
        self.assertIn("delegate stdout", self.cli("build", "--dry-run"))
        self.archive("main")
        self.release("main")
        self.assertIn("Building this version's trusscli", self.cli("use", "main"))
        self.assertTrue((self.root() / "tools/trusscli-build.log").exists())


if __name__ == "__main__":
    unittest.main()
