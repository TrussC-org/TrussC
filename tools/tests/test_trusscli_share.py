"""Offline GitHub Pages publishing. Set TRUSSCLI to the branch's built binary."""

import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

if os.name != "nt":
    import resource

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).absolute()
# A valid one-pixel PNG; screenshot fixtures need no renderer or viewer.
PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+afo8AAAAASUVORK5CYII=")


@unittest.skipIf(os.name == "nt", "offline gh/cmake fixtures use POSIX scripts")
class ShareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="trusscli-share-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.project = self.base / "myApp"
        self.project.mkdir()
        (self.project / "src").mkdir()
        self.remote = self.base / "github.git"
        self.git("init", "--bare", str(self.remote), cwd=self.base)
        self.git("init", cwd=self.project)
        self.git("config", "user.name", "Test User")
        self.git("config", "user.email", "test@example.invalid")
        self.git("remote", "add", "origin", "git@github.com:author/myApp.git")
        self.web = self.base / "web"
        self.web.mkdir()
        (self.web / "myApp.html").write_text('<script src="myApp.js"></script>', encoding="utf-8")
        (self.web / "myApp.js").write_text('console.log("web");', encoding="utf-8")
        (self.web / "myApp.wasm").write_bytes(b"\0asm")
        (self.web / "myApp").write_text("native must stay private", encoding="utf-8")
        (self.web / "private.txt").write_text("not a web output", encoding="utf-8")
        (self.project / "screenshot.png").write_bytes(PNG)
        self.fakebin = self.base / "fakebin"
        self.fakebin.mkdir()
        self.log = self.base / "gh.jsonl"
        gh = self.fakebin / "gh"
        gh.write_text('''#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
with open(os.environ["SHARE_GH_LOG"], "a", encoding="utf-8") as file:
    file.write(json.dumps(args) + "\\n")
if args == ["auth", "status"]:
    sys.exit(int(os.environ.get("SHARE_AUTH_FAIL", "0")))
if args[:2] == ["repo", "view"]:
    print(json.dumps({"visibility": os.environ.get("SHARE_VISIBILITY", "PUBLIC"),
                      "description": "A \\"quoted\\" description"}))
elif args[:1] == ["api"]:
    if "POST" not in args and os.environ.get("SHARE_PAGES_MISSING"):
        print("HTTP 404")
        sys.exit(1)
    print(os.environ.get("SHARE_PAGES", '{"build_type":"legacy","source":{"branch":"gh-pages","path":"/"}}'))
''', encoding="utf-8")
        gh.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.fakebin) + os.pathsep + os.environ["PATH"], TMPDIR=str(self.base),
                        TRUSSC_DIR=str(ROOT), SHARE_GH_LOG=str(self.log),
                        TRUSSC_INTERNAL_SHARE_REMOTE=str(self.remote),
                        TRUSSC_INTERNAL_SHARE_WEB_DIR=str(self.web),
                        TRUSSC_INTERNAL_SHARE_NO_VIEWER="1")

    def git(self, *args, cwd=None):
        result = subprocess.run(["git", *args], cwd=cwd or self.project,
                                capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def cli(self, answers="y\ny\nn\n", *args, expected=0):
        result = subprocess.run([str(CLI), "share", "-p", str(self.project), *args],
                                cwd=self.base, env=self.env, input=answers,
                                capture_output=True, text=True, timeout=40, encoding="utf-8")
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def refs(self):
        return self.git("ls-remote", str(self.remote), "refs/heads/gh-pages")

    def files(self):
        return self.git("--git-dir=" + str(self.remote), "ls-tree", "-r", "--name-only", "gh-pages").splitlines()

    def show(self, name):
        return self.git("--git-dir=" + str(self.remote), "show", "gh-pages:" + name)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()] if self.log.exists() else []

    def seed(self, owned):
        seed = self.base / "seed"
        seed.mkdir()
        self.git("init", cwd=seed)
        self.git("config", "user.name", "Test", cwd=seed)
        self.git("config", "user.email", "test@example.invalid", cwd=seed)
        (seed / "old.txt").write_text("old content", encoding="utf-8")
        if owned:
            (seed / ".trussc-share").write_text("generator: trusscli share\n", encoding="utf-8")
        for i in range(2):
            (seed / "history.txt").write_text(str(i), encoding="utf-8")
            self.git("add", ".", cwd=seed)
            self.git("commit", "-m", "seed", cwd=seed)
        self.git("push", str(self.remote), "HEAD:refs/heads/gh-pages", cwd=seed)

    def assertSingleCommit(self):
        self.assertEqual(self.git("--git-dir=" + str(self.remote), "rev-list", "--count", "gh-pages"), "1")

    def test_no_remote(self):
        self.git("remote", "remove", "origin")
        self.assertIn("GitHub origin", self.cli(expected=1))
        self.assertFalse(self.refs())
        self.assertFalse(self.calls())

    def test_not_git(self):
        shutil.rmtree(self.project / ".git")
        self.assertIn("git repo", self.cli(expected=1))

    def test_non_github_origin(self):
        self.git("remote", "set-url", "origin", "https://example.invalid/owner/repo")
        self.assertIn("github.com", self.cli(expected=1))
        self.assertFalse(self.calls())

    def test_auth(self):
        self.env["SHARE_AUTH_FAIL"] = "1"
        self.assertIn("not logged in", self.cli(expected=1))
        self.assertFalse(self.refs())

    def test_private(self):
        self.env["SHARE_VISIBILITY"] = "PRIVATE"
        self.assertIn("public repo required", self.cli(expected=1))
        self.assertFalse(self.refs())
        self.assertEqual(len(self.calls()), 2)

    def test_publish_no(self):
        output = self.cli("y\nn\n")
        self.assertIn("Total:", output)
        self.assertIn("screenshot.png", output)
        self.assertFalse(self.refs())
        self.assertFalse(any(c[:1] == ["api"] for c in self.calls()))

    def test_foreign_branch(self):
        self.seed(False)
        before = self.refs()
        self.assertIn("hand-made", self.cli(expected=1))
        self.assertEqual(self.refs(), before)
        self.assertIn("hand-made", self.cli("", "--unshare", expected=1))
        self.assertEqual(self.refs(), before)
        self.assertFalse(any(c[:2] == ["repo", "edit"] for c in self.calls()))

    def test_owned_replaced_and_listing_no(self):
        self.seed(True)
        self.cli()
        self.assertSingleCommit()
        self.assertNotIn("old.txt", self.files())
        self.assertNotIn("trussc-share.json", self.files())
        self.assertNotIn("myApp", self.files())
        self.assertNotIn("private.txt", self.files())
        self.assertIn("index.html", self.files())
        self.assertIn(".nojekyll", self.files())
        self.assertEqual(self.show(".trussc-share"), "generator: trusscli share")
        self.assertFalse(any("--add-topic" in c for c in self.calls()))
        contents = subprocess.check_output(["git", "--git-dir=" + str(self.remote), "show", "gh-pages:screenshot.png"])
        self.assertEqual(contents, PNG)

    def test_listing_fields_and_unshare(self):
        self.env["SHARE_PAGES_MISSING"] = "1"
        self.cli("y\ny\ny\n")
        self.assertSingleCommit()
        marker = json.loads(self.show("trussc-share.json"))
        self.assertEqual(marker["schema"], 1)
        self.assertEqual(marker["generator"], "trusscli share")
        self.assertEqual(marker["title"], "myApp")
        self.assertEqual(marker["description"], 'A "quoted" description')
        self.assertEqual(marker["author"], "author")
        self.assertTrue(marker["trussc_version"])
        self.assertEqual(marker["screenshot"], "screenshot.png")
        self.assertIn(["repo", "edit", "author/myApp", "--add-topic", "trussc-share"], self.calls())
        self.assertIn(["api", "-X", "POST", "repos/author/myApp/pages", "-f", "source[branch]=gh-pages", "-f", "source[path]=/"], self.calls())
        self.cli("", "--unshare")
        self.assertSingleCommit()
        self.assertNotIn("trussc-share.json", self.files())
        self.assertIn(".trussc-share", self.files())
        self.assertIn("index.html", self.files())
        self.assertIn(["repo", "edit", "author/myApp", "--remove-topic", "trussc-share"], self.calls())

    def test_capture_retake_save(self):
        (self.project / "screenshot.png").unlink()
        fixture = self.base / "capture.png"
        fixture.write_bytes(PNG)
        self.env["TRUSSC_INTERNAL_SHARE_CAPTURE"] = str(fixture)
        self.cli("retake\ny\ny\ny\n")
        self.assertEqual((self.project / "screenshot.png").read_bytes(), PNG)
        self.assertIn("screenshot.png", self.files())

    def test_capture_failure_skip(self):
        (self.project / "screenshot.png").unlink()
        self.env["TRUSSC_INTERNAL_SHARE_CAPTURE"] = str(self.base / "missing")
        self.assertIn("Screenshot failed", self.cli("skip\ny\ny\n"))
        self.assertNotIn("screenshot.png", self.files())
        self.assertNotIn("screenshot", json.loads(self.show("trussc-share.json")))

    def test_jpeg_and_skip(self):
        (self.project / "screenshot.png").rename(self.project / "screenshot.jpg")
        self.cli("y\ny\ny\n")
        self.assertEqual(json.loads(self.show("trussc-share.json"))["screenshot"], "screenshot.jpg")
        self.cli("skip\ny\ny\n")
        self.assertNotIn("screenshot", json.loads(self.show("trussc-share.json")))
        self.assertNotIn("screenshot.jpg", self.files())

    def test_threaded_build_rejected(self):
        for source in ("new SharedArrayBuffer(1024)",
                       "new WebAssembly.Memory({'initial': 1, 'maximum': 2, 'shared': true})",
                       "new WebAssembly.Memory({initial:1,maximum:2,shared:!0})"):
            with self.subTest(source=source):
                (self.web / "myApp.js").write_text(source, encoding="utf-8")
                self.assertIn("COOP/COEP", self.cli("", expected=1))
                self.assertFalse(self.refs())

    def test_optional_thread_support_probe_allowed(self):
        (self.web / "myApp.js").write_text(
            "// PThreads reuse the runtime\nconst supported = typeof SharedArrayBuffer !== 'undefined'; const prefs = {shared: true};",
            encoding="utf-8")
        self.cli("skip\nn\n")
        self.assertFalse(self.refs())

    def test_missing_web_target(self):
        del self.env["TRUSSC_INTERNAL_SHARE_WEB_DIR"]
        self.assertIn("trusscli update --web", self.cli("", expected=1))
        self.assertFalse(self.refs())

    def test_existing_web_build_path(self):
        del self.env["TRUSSC_INTERNAL_SHARE_WEB_DIR"]
        shutil.copytree(self.web, self.project / "bin")
        (self.project / "CMakeLists.txt").write_text("# Build fixture\n", encoding="utf-8")
        (self.project / "CMakePresets.json").write_text(json.dumps({
            "version": 3,
            "configurePresets": [{"name": "web", "generator": "Unix Makefiles",
                                   "binaryDir": "${sourceDir}/build-web"}],
            "buildPresets": [{"name": "web", "configurePreset": "web"}],
        }), encoding="utf-8")
        cmake_log = self.base / "cmake.jsonl"
        self.env["SHARE_CMAKE_LOG"] = str(cmake_log)
        cmake = self.fakebin / "cmake"
        cmake.write_text('''#!/usr/bin/env python3
import json, os, sys
with open(os.environ["SHARE_CMAKE_LOG"], "a", encoding="utf-8") as file:
    file.write(json.dumps(sys.argv[1:]) + "\\n")
if sys.argv[1:] == ["--version"]:
    print("cmake version 3.28.3")
''', encoding="utf-8")
        cmake.chmod(0o755)
        self.cli("y\nn\n")
        commands = [json.loads(line) for line in cmake_log.read_text(encoding="utf-8").splitlines()]
        self.assertTrue(any(c[:2] == ["--preset", "web"] for c in commands), commands)
        self.assertTrue(any(c[:3] == ["--build", "--preset", "web"] for c in commands), commands)
        self.assertFalse(self.refs())

    def test_no_additional_flags(self):
        self.assertIn("unknown", self.cli("", "--yes", expected=1))

    def push_race(self, mode):
        self.seed(False)
        foreign = self.refs().split()[0]
        self.git("--git-dir=" + str(self.remote), "update-ref", "-d", "refs/heads/gh-pages")
        if mode in ("owned", "unshare"):
            self.cli()
        inspected = self.refs().split()[0] if self.refs() else ""
        push_log = self.base / "push.jsonl"
        wrapper = self.fakebin / "git"
        wrapper.write_text('''#!/usr/bin/env python3
import json, os, subprocess, sys
from pathlib import Path
args = sys.argv[1:]
real = os.environ["SHARE_REAL_GIT"]
if "push" in args:
    log = Path(os.environ["SHARE_PUSH_LOG"])
    count = len(log.read_text(encoding="utf-8").splitlines()) if log.exists() else 0
    remote = os.environ["TRUSSC_INTERNAL_SHARE_REMOTE"]
    before = subprocess.check_output([real, "ls-remote", remote, "refs/heads/gh-pages"], text=True).split()
    with log.open("a", encoding="utf-8") as file:
        file.write(json.dumps({"args": args, "before": before[0] if before else ""}) + "\\n")
    if count + 1 == int(os.environ["SHARE_RACE_PUSH"]):
        subprocess.run([real, "--git-dir=" + remote, "update-ref", "refs/heads/gh-pages", os.environ["SHARE_FOREIGN"]], check=True)
os.execv(real, [real, *args])
''', encoding="utf-8")
        wrapper.chmod(0o755)
        self.env.update(SHARE_REAL_GIT=shutil.which("git"), SHARE_PUSH_LOG=str(push_log),
                        SHARE_FOREIGN=foreign, SHARE_RACE_PUSH="2" if mode == "listing" else "1")
        output = self.cli("", "--unshare", expected=1) if mode == "unshare" else self.cli("y\ny\ny\n", expected=1)
        self.assertIn("gh-pages changed on GitHub during share; run share again", output)
        self.assertEqual(self.refs().split()[0], foreign)
        pushes = [json.loads(line) for line in push_log.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(len(pushes), 2 if mode == "listing" else 1)
        for i, push in enumerate(pushes):
            expected = inspected if i == 0 else push["before"]
            self.assertIn("--force-with-lease=refs/heads/gh-pages:" + expected, push["args"])
        self.assertFalse(any(c[:2] == ["repo", "edit"] for c in self.calls()[-2:]))

    def test_creation_lease(self):
        self.push_race("creation")

    def test_owned_publish_lease(self):
        self.push_race("owned")

    def test_listing_lease(self):
        self.push_race("listing")

    def test_unshare_lease(self):
        self.push_race("unshare")

    def test_global_ignore_does_not_change_publish_set(self):
        ignored = self.base / "ignore"
        ignored.write_text("*.wasm\n*.png\n.trussc-share\ntrussc-share.json\n", encoding="utf-8")
        config = self.base / "global.gitconfig"
        config.write_text('[core]\nexcludesFile = "' + str(ignored) + '"\n', encoding="utf-8")
        self.env["GIT_CONFIG_GLOBAL"] = str(config)
        output = self.cli("y\ny\ny\n")
        preview = output.split("Publish these files")[0]
        self.assertIn("myApp.wasm  4 bytes", preview)
        self.assertIn("screenshot.png  " + str(len(PNG)) + " bytes", preview)
        self.assertEqual(set(self.files()), {"index.html", "myApp.js", "myApp.wasm",
                                            ".nojekyll", ".trussc-share", "screenshot.png", "trussc-share.json"})

    def test_incompatible_pages_source(self):
        for source, label in (({"build_type": "legacy", "source": {"branch": "main", "path": "/docs"}}, "main:/docs"),
                              ({"build_type": "workflow"}, "workflow")):
            with self.subTest(source=source):
                self.env["SHARE_PAGES"] = json.dumps(source)
                output = self.cli("y\ny\ny\n", expected=1)
                self.assertIn("Pages is configured to serve " + label, output)
                self.assertIn("gh api -X PUT repos/author/myApp/pages", output)
                self.assertNotIn("List this app", output)
                self.assertNotIn("Pages: https://", output)
                self.assertNotIn("trussc-share.json", self.files())
                self.assertFalse(any("POST" in c or "PUT" in c or "--add-topic" in c for c in self.calls()))

    def test_only_exact_web_products(self):
        for name in ("myApp.private.js", "myApp.old.data", "myApp.js.map", "myApp.data"):
            (self.web / name).write_text("fixture", encoding="utf-8")
        output = self.cli()
        self.assertIn("myApp.data", self.files())
        for name in ("myApp.private.js", "myApp.old.data", "myApp.js.map"):
            self.assertNotIn(name, self.files())
            self.assertNotIn(name, output)

    def test_bundled_assets_preview(self):
        data = self.project / "bin/data"
        (data / "nested").mkdir(parents=True)
        (data / "nested/info.json").write_text("private config", encoding="utf-8")
        outside = self.base / "outside.txt"
        outside.write_text("outside asset", encoding="utf-8")
        (data / "linked.txt").symlink_to(outside)
        (data / "linked-dir").symlink_to(data / "nested", target_is_directory=True)
        (self.web / "myApp.data").write_bytes(b"bundle")
        output = self.cli("y\nn\n")
        preview = output.split("Publish these files")[0]
        self.assertIn("Bundled into myApp.data", preview)
        self.assertIn("nested/info.json  14 bytes", preview)
        self.assertIn("linked.txt  13 bytes -> " + str(outside), preview)
        self.assertIn("linked-dir -> ", preview)
        self.assertIn("(directory; not preloaded)", preview)
        self.assertFalse(self.refs())

    def test_long_whitespace_js(self):
        (self.web / "myApp.js").write_text("new WebAssembly.Memory({" + " " * 200000 + "shared:true})", encoding="utf-8")
        # Keep the pre-fix crash reproduction from producing a core file.
        result = subprocess.run([str(CLI), "share", "-p", str(self.project)], env=self.env,
                                input="", capture_output=True, text=True, encoding="utf-8", timeout=15,
                                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("COOP/COEP", result.stderr)
        self.assertFalse(self.refs())

    def test_invalid_native_port_skips_and_cleans_up(self):
        (self.project / "screenshot.png").unlink()
        app = self.base / "native"
        pid = self.base / "native.pid"
        app.write_text('''#!/usr/bin/env python3
import os, time
from pathlib import Path
Path(os.environ["SHARE_PID"]).write_text(str(os.getpid()), encoding="utf-8")
for port in ("999999999999999999999", "65536", "0"):
    print("HTTP server listening on http://127.0.0.1:" + port + "/mcp", flush=True)
time.sleep(120)
''', encoding="utf-8")
        app.chmod(0o755)
        self.env.update(TRUSSC_INTERNAL_SHARE_NATIVE_BIN=str(app), SHARE_PID=str(pid))
        try:
            result = subprocess.run([str(CLI), "share", "-p", str(self.project)], env=self.env,
                                    input="skip\nn\n", capture_output=True, text=True, encoding="utf-8", timeout=35,
                                    preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("MCP startup timed out", result.stdout)
            self.assertIn("Continue without a screenshot [skip]", result.stdout)
            self.assertFalse(list(self.base.glob("trusscli-share-*")))
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pid.read_text(encoding="utf-8")), 0)
        finally:
            if pid.exists():
                try:
                    os.killpg(int(pid.read_text(encoding="utf-8")), 9)
                except ProcessLookupError:
                    pass

    def test_capture_build_is_isolated(self):
        (self.project / "screenshot.png").unlink()
        native = "macos" if sys.platform == "darwin" else "linux"
        native_build = self.project / ("build-" + native)
        native_build.mkdir()
        (native_build / "CMakeCache.txt").write_bytes(b"CMAKE_BUILD_TYPE:STRING=Release\nTRUSSC_MCP_DEV:BOOL=OFF\n")
        binary = self.project / "bin/myApp"
        binary.parent.mkdir()
        binary.write_bytes(b"user release binary")
        data = self.project / "bin/data"
        data.mkdir()
        (data / "asset").write_bytes(b"runtime asset")
        (self.project / "CMakeLists.txt").write_text("# fixture\n", encoding="utf-8")
        (self.project / "CMakePresets.json").write_text(json.dumps({"version": 3, "configurePresets": [
            {"name": native, "generator": "Unix Makefiles", "binaryDir": "${sourceDir}/build-" + native}]}), encoding="utf-8")
        cmake = self.fakebin / "cmake"
        cmake.write_text('''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
with open(os.environ["SHARE_CMAKE_LOG"], "a", encoding="utf-8") as file:
    file.write(json.dumps(args) + "\\n")
if args == ["--version"]:
    print("cmake version 3.28.3")
elif "--build" in args:
    build = Path(args[args.index("--build") + 1])
    if not build.is_absolute():
        build = Path(os.environ["SHARE_NATIVE_BUILD"])
    output = build / "capture-bin"
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "myApp"
    if sys.platform == "darwin":
        binary = output / "myApp.app/Contents/MacOS/myApp"
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_text("#!/usr/bin/env python3\\nimport sys\\nfrom pathlib import Path\\nbin = Path(__file__).parents[3] if sys.platform == 'darwin' else Path(__file__).parent\\nassert (bin / 'data/asset').read_bytes() == b'runtime asset'\\nPath('capture-data-checked').touch()\\nsys.exit(0)\\n", encoding="utf-8")
    binary.chmod(0o755)
else:
    build = Path(args[args.index("-B") + 1]) if "-B" in args else Path(os.environ["SHARE_NATIVE_BUILD"])
    build.mkdir(parents=True, exist_ok=True)
    (build / "CMakeCache.txt").write_text("TRUSSC_MCP_DEV:BOOL=ON\\n", encoding="utf-8")
    # Validate that capture actually overrides trussc_app's target properties.
    hooks = [a.split("=", 1)[1] for a in args if a.startswith("-DCMAKE_PROJECT_") and "INCLUDE=" in a]
    if hooks:
        hook = Path(hooks[0]).read_text(encoding="utf-8")
        assert "DEFER" in hook and "RUNTIME_OUTPUT_DIRECTORY" in hook and "capture-bin" in hook
''', encoding="utf-8")
        cmake.chmod(0o755)
        cmake_log = self.base / "cmake.jsonl"
        self.env.update(SHARE_CMAKE_LOG=str(cmake_log), SHARE_PROJECT=str(self.project), SHARE_NATIVE_BUILD=str(native_build))
        def snapshot():
            return {str(p.relative_to(self.project)): p.read_bytes()
                    for root in (native_build, binary.parent) for p in root.rglob("*") if p.is_file()}
        before = snapshot()
        output = self.cli("skip\nn\n")
        self.assertEqual(snapshot(), before)
        self.assertIn("Screenshot failed", output)
        self.assertTrue((self.project / "capture-data-checked").exists())
        commands = [json.loads(line) for line in cmake_log.read_text(encoding="utf-8").splitlines()]
        configure = next(c for c in commands if "-B" in c)
        scratch = Path(configure[configure.index("-B") + 1])
        self.assertFalse(scratch.exists())
        self.assertTrue(any(c[:2] == ["--build", str(scratch)] for c in commands))
        self.assertIn("-DTRUSSC_MCP_DEV=ON", configure)


if __name__ == "__main__":
    unittest.main()
