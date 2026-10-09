#!/usr/bin/env python3
"""Exercise #417 with the freshly built CLI, in a chcp 65001 console."""
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli.exe")).resolve()


@unittest.skipUnless(sys.platform == "win32", "requires Windows/MSVC and a UTF-8 console")
class WindowsHeaderDependencies(unittest.TestCase):
    def test_configure_build_and_doctor(self):
        # Some runners launch cmd without a console. Give this test a shared
        # console before applying chcp, so configure and build can inherit it.
        if ctypes.windll.kernel32.GetConsoleCP() == 0:
            self.assertTrue(ctypes.windll.kernel32.AllocConsole())
            self.addCleanup(ctypes.windll.kernel32.FreeConsole)
            subprocess.run(["cmd", "/c", "chcp 65001"], check=True,
                           stdout=subprocess.PIPE)
        self.assertEqual(ctypes.windll.kernel32.GetConsoleOutputCP(), 65001)
        with tempfile.TemporaryDirectory(prefix="trusscli-deps-") as scratch:
            project = Path(scratch) / "fboExample"
            shutil.copytree(ROOT / "examples/graphics/fboExample/src", project / "src")
            shutil.copyfile(ROOT / "examples/graphics/fboExample/addons.make", project / "addons.make")
            env = dict(os.environ, TRUSSC_DIR=str(ROOT))
            # Exercise cl directly, without a compiler cache replaying includes.
            for lang in ("C", "CXX"):
                env.pop(f"CMAKE_{lang}_COMPILER_LAUNCHER", None)

            def run(*args, check=True):
                result = subprocess.run(list(map(str, args)), cwd=project, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        encoding="utf-8", errors="replace")
                if check:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result

            run(CLI, "update", "--no-web", "--no-android", "--no-ios")
            build = project / "build-windows"
            cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
            self.assertIn("CMAKE_BUILD_TYPE:STRING=RelWithDebInfo", cache)
            ninja = re.search(r"^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$", cache, re.M).group(1).strip()
            targets = run(ninja, "-C", build, "-t", "targets", "all").stdout
            main_object = next(line.rsplit(": ", 1)[0] for line in targets.splitlines()
                               if line.rsplit(": ", 1)[0].endswith("/src/main.cpp.obj"))

            def assert_deps():
                deps = run(ninja, "-C", build, "-t", "deps", main_object).stdout
                match = re.search(r": #deps (\d+),", deps)
                self.assertIsNotNone(match, deps)
                self.assertGreater(int(match.group(1)), 0, deps)

            # Check the update configure itself before --debug reconfigures it.
            run("cmake", "--build", "--preset", "windows", "--target", main_object)
            assert_deps()
            run(CLI, "build", "--debug")
            assert_deps()
            self.assertIn("CMAKE_BUILD_TYPE:STRING=Debug",
                          (build / "CMakeCache.txt").read_text(encoding="utf-8"))

            def doctor_status():
                result = run(CLI, "doctor", "--json", check=False)
                return next(item for item in json.loads(result.stdout)
                            if item["name"] == "Header dependencies")

            self.assertEqual(doctor_status()["status"], "ok")
            # A missing deps database after a build must not report success.
            deps_file = build / ".ninja_deps"
            backup = build / ".ninja_deps.saved"
            deps_file.rename(backup)
            try:
                self.assertEqual(doctor_status()["status"], "error")
            finally:
                backup.rename(deps_file)

            objects = [build / main_object,
                       next(build.glob("CMakeFiles/*.dir/src/tcApp.cpp.obj"))]
            previous = [obj.stat().st_mtime_ns for obj in objects]
            header = project / "src/tcApp.h"
            with header.open("a", encoding="utf-8") as stream:
                stream.write("\n// Dependency regression check.\n")
            # Establish file ordering explicitly; do not wait for a clock tick.
            stamp = max(header.stat().st_mtime_ns, *previous) + 1_000_000_000
            os.utime(header, ns=(stamp, stamp))
            run(CLI, "build", "--debug")
            for obj, before in zip(objects, previous):
                self.assertNotEqual(obj.stat().st_mtime_ns, before, obj.name)
            assert_deps()


if __name__ == "__main__":
    unittest.main()
