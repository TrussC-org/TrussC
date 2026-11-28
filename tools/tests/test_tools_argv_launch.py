"""Configure and IDE launch arguments for project paths with special characters.

Run through tcbuild.sh with TRUSSCLI pointing to the built CLI.
"""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = os.environ.get("TRUSSCLI", str(ROOT / "tools/bin/trusscli"))


@unittest.skipUnless(sys.platform == "linux", "Linux launch fixture")
class LaunchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="argv-launch-driver-")
        cls.addClassCleanup(cls.scratch.cleanup)
        scratch = Path(cls.scratch.name)
        source = scratch / "main.cpp"
        source.write_text(r'''
#include "IdeHelper.h"
#include "GeneratedFile.h"
void writeGeneratedFile(const fs::path&, const std::string&) {}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    IdeType type;
    if (!IdeHelper::parseIdeId(argv[1], type)) return 3;
    return IdeHelper::openInIde(type, argv[2]).empty() ? 0 : 1;
}
''', encoding="utf-8")
        cls.driver = scratch / "driver"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                        "-Werror", "-I", str(ROOT / "tools/src"), str(source),
                        str(ROOT / "tools/src/IdeHelper.cpp"),
                        str(ROOT / "tools/src/ProcessCapture.cpp"), "-o", str(cls.driver)],
                       check=True)

    def test_copy_and_version_keep_git_path_arguments(self):
        with tempfile.TemporaryDirectory(prefix="argv-git-") as scratch:
            base = Path(scratch)
            for suffix in ("$(touch DOLLAR_MARKER)", "`touch BACKTICK_MARKER`"):
                with self.subTest(suffix=suffix):
                    source = base / ("source " + suffix)
                    source.mkdir()
                    for name in ("CMakeLists.txt", "addons.make", "kept.txt"):
                        (source / name).write_text("", encoding="utf-8")
                    (source / ".gitignore").write_text("ignored.txt\n", encoding="utf-8")
                    (source / "ignored.txt").write_text("", encoding="utf-8")
                    subprocess.run(["git", "init", "-q", str(source)], check=True)
                    env = dict(os.environ, TRUSSC_DIR=str(ROOT))
                    result = subprocess.run(
                        [CLI, "cp", str(source), str(source) + " copy", "--dry-run", "--tc-root", str(ROOT)],
                        cwd=base, env=env, capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("[git, dry-run]", result.stdout)
                    self.assertIn("  kept.txt\n", result.stdout)
                    self.assertNotIn("  ignored.txt\n", result.stdout)
                    framework = base / ("framework " + suffix)
                    framework.symlink_to(ROOT, target_is_directory=True)
                    env["TRUSSC_DIR"] = str(framework)
                    result = subprocess.run([CLI, "--version"], cwd=base, env=env,
                                            capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    version = subprocess.run(
                        ["git", "-C", str(ROOT), "describe", "--tags", "--always", "--dirty", "--match", "v[0-9]*"],
                        check=True, capture_output=True, text=True, encoding="utf-8").stdout.strip()
                    self.assertIn("TrussC   " + version + " at ", result.stdout)
                    for directory in (base, source, ROOT):
                        for marker in ("DOLLAR_MARKER", "BACKTICK_MARKER"):
                            self.assertFalse((directory / marker).exists())

    def test_configure_and_open_keep_project_argument(self):
        with tempfile.TemporaryDirectory(prefix="argv-launch-") as scratch:
            base = Path(scratch)
            project = base / "project $(touch MARKER) `touch MARKER`"
            log = base / "arguments.jsonl"
            fake_bin = base / "bin"
            fake_bin.mkdir()
            for name in ("cmake", "code", "cursor", "x-terminal-emulator", "gnome-terminal"):
                tool = fake_bin / name
                tool.write_text(f"#!{sys.executable}\n" + '''import json, os, sys
from pathlib import Path
with open(os.environ["LAUNCH_LOG"], "a") as log:
    log.write(json.dumps([Path(sys.argv[0]).name, os.getcwd(), sys.argv[1:]]) + "\\n")
if Path(sys.argv[0]).name == "x-terminal-emulator":
    sys.exit(1)
''', encoding="utf-8")
                tool.chmod(0o755)
            env = dict(os.environ, PATH=str(fake_bin) + os.pathsep + os.environ["PATH"],
                       LAUNCH_LOG=str(log), TRUSSC_DIR=str(ROOT))
            for command in ([CLI, "new", str(project), "--tc-root", str(ROOT), "--ide", "vscode", "--web"],
                            [CLI, "update", "-p", str(project), "--tc-root", str(ROOT)]):
                result = subprocess.run(command, cwd=base, env=env, capture_output=True, text=True, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for ide in ("vscode", "cursor", "cmake"):
                subprocess.run([str(self.driver), ide, str(project)], cwd=base, env=env, check=True)
            self.assertFalse((base / "MARKER").exists())
            self.assertFalse((project / "MARKER").exists())
            records = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(records, [
                ["cmake", str(project), ["--preset", "linux"]],
                ["cmake", str(project), ["--preset", "web"]],
                ["cmake", str(project), ["--preset", "linux"]],
                ["cmake", str(project), ["--preset", "web"]],
                ["code", str(project), [str(project)]],
                ["cursor", str(project), [str(project)]],
                ["x-terminal-emulator", str(project), ["--working-directory=" + str(project)]],
                ["gnome-terminal", str(project), ["--working-directory=" + str(project)]],
            ])


if __name__ == "__main__":
    unittest.main()
