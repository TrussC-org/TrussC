"""Generated projects explain missing core paths (#948 item 12).

Set TRUSSCLI to a trusscli built from this tree. No dependencies are downloaded.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()
HOST_PRESET = {"darwin": "macos", "linux": "linux", "win32": "windows"}[sys.platform]


class CoreGuardTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="trusscli-core-guard-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name).resolve()
        self.assertNotIn(ROOT, self.base.parents, "Use a temporary directory outside the repository")
        self.project = self.base / "sample"
        self.run_command([CLI, "new", self.project, "--tc-root", ROOT, "--ide", "cmake"])

    def run_command(self, command, *, cwd=None, success=True):
        result = subprocess.run(list(map(str, command)), cwd=cwd,
                                capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def assert_guard(self):
        text = (self.project / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../core")', text)
        self.assertLess(text.index('if(NOT EXISTS "${TRUSSC_DIR}/cmake/trussc_app.cmake")'),
                        text.index('include("${TRUSSC_DIR}/cmake/trussc_app.cmake")'))
        self.assertNotIn(str(ROOT), text)
        self.assertFalse((self.project / "../../../core").exists())
        output = self.run_command(["cmake", "-S", self.project,
                                   "-B", self.project / "build-missing"], success=False)
        # CMake wraps message text according to terminal width.
        output = " ".join(output.split())
        for message in ("TrussC core not found.", "trusscli update",
                        "cmake --preset " + HOST_PRESET,
                        "-DTRUSSC_DIR=<TrussC-root>/core"):
            self.assertIn(message, output)
        self.assertNotIn("include could not find requested file", output)

    def test_new_and_update_guard_and_supported_configures(self):
        self.assert_guard()
        self.run_command([CLI, "update", "-p", self.project,
                          "--tc-root", ROOT, "--ide", "cmake"])
        self.assert_guard()
        offline = ["--no-warn-unused-cli", "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"]
        self.run_command(["cmake", "-S", self.project, "-B", self.project / "build-explicit",
                          "-DTRUSSC_DIR=" + str(ROOT / "core"), *offline])
        self.run_command(["cmake", "--preset", HOST_PRESET, *offline], cwd=self.project)

    def test_host_preset_names_in_cmake_guard(self):
        text = (self.project / "CMakeLists.txt").read_text(encoding="utf-8")
        guard = text[text.index('if(NOT EXISTS'):text.index('include(')]
        for system, preset in (("Darwin", "macos"), ("Linux", "linux"), ("Windows", "windows")):
            with self.subTest(system=system):
                script = self.base / "guard.cmake"
                script.write_text(f'set(CMAKE_HOST_SYSTEM_NAME "{system}")\n'
                                  f'set(TRUSSC_DIR "{self.base.as_posix()}/missing core")\n' + guard,
                                  encoding="utf-8")
                output = self.run_command(["cmake", "-P", script], success=False)
                self.assertIn("cmake --preset " + preset, " ".join(output.split()))


if __name__ == "__main__":
    unittest.main()
