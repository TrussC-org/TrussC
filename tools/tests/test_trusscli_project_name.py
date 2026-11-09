"""Project names: pure sanitizer checks and built CLI regressions.

Build tools/bin/trusscli from this tree, or override it with TRUSSCLI, for the
generate/update/build/run checks. Set TRUSSCLI_BASELINE to main's CLI for byte
identity checks. Run through the build guard when compiling. All project fixtures live outside the source
tree (the system temporary directory).
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
CLI = os.environ.get("TRUSSCLI", str(ROOT / "tools/bin/trusscli"))
BASELINE = os.environ.get("TRUSSCLI_BASELINE")
NATIVE = {"linux": "linux", "darwin": "macos", "win32": "windows"}[sys.platform]


def run(*args, cwd=None, env=None):
    result = subprocess.run(list(map(str, args)), cwd=cwd, capture_output=True,
                            encoding="utf-8", errors="replace",
                            env=dict(os.environ, TRUSSC_DIR=str(ROOT), **(env or {})))
    if result.returncode:
        raise AssertionError(f"{args}: {result.returncode}\n{result.stdout}{result.stderr}")
    return result.stdout


class SanitizerTests(unittest.TestCase):
    def test_portable_names_and_edge_cases(self):
        with tempfile.TemporaryDirectory(prefix="project-name-unit-") as scratch:
            source = Path(scratch) / "main.cpp"
            binary = Path(scratch) / "names"
            source.write_text(r'''
#include "ProjectName.h"
#include <cassert>
int main() {
    assert(sanitizeProjectName("sample project") == "sample_project");
    assert(sanitizeProjectName("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") == "___");
    assert(sanitizeProjectName("a\xff" "b") == "a_b");
    assert(sanitizeProjectName("a\xe6\x97" "b") == "a__b");
    assert(sanitizeProjectName("+ ( ) & ' #") == "+__________");
    assert(sanitizeProjectName("9valid_Name-1.2+3") == "9valid_Name-1.2+3");
    assert(sanitizeProjectName("") == "app");
    assert(sanitizeProjectName("0") == "0");
    assert(sanitizeProjectName("OFF") == "OFF");
    assert(sanitizeProjectName("_") == "_");
    assert(sanitizeProjectName("sample.") == "sample.");
    for (const char* reserved : {"all", "clean", "help", "install", "test",
             "package", "package_source", "edit_cache", "rebuild_cache",
             "preinstall", "ALL_BUILD", "ZERO_CHECK", "INSTALL"}) {
        assert(sanitizeProjectName(reserved) == std::string("app_") + reserved);
    }
    assert(sanitizeProjectName("con") == "app_con");
    assert(sanitizeProjectName("NUL.txt") == "app_NUL.txt");
    assert(sanitizeProjectName("LPT9") == "app_LPT9");
    assert(sanitizeProjectName("COM1") == "app_COM1");
    assert(sanitizeProjectName("COM10") == "COM10");
    assert(sanitizeProjectName("Console") == "Console");
    assert(sanitizeProjectName("All") == "All");
    assert(sanitizeProjectName("install_manifest") == "install_manifest");
    assert(sanitizeProjectName("list_install_components") == "list_install_components");
}
''', encoding="utf-8")
            run(os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                "-Werror", "-I", ROOT / "tools/src", source, "-o", binary)
            run(binary)

    def test_xcode_scheme_names_without_xcode(self):
        with tempfile.TemporaryDirectory(prefix="project-name-schemes-") as scratch:
            source = Path(scratch) / "main.cpp"
            binary = Path(scratch) / "schemes"
            # Only the file writer is replaced: exercise the production scheme
            # generator on a fixture .xcodeproj without requiring Apple tools.
            source.write_text(r'''
#include "IdeHelper.h"
#include "GeneratedFile.h"
#include <fstream>
void writeGeneratedFile(const fs::path& path, const std::string& text) {
    std::ofstream(path) << text;
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    IdeHelper::generateXcodeSchemes(argv[1]);
}
''', encoding="utf-8")
            run(os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                "-Werror", "-I", ROOT / "tools/src", source,
                ROOT / "tools/src/IdeHelper.cpp", "-o", binary)
            for folder, expected in (("sample project", "sample_project"),
                                     ("myApp", "myApp")):
                project = Path(scratch) / folder
                schemes = project / "xcode/fixture.xcodeproj/xcshareddata/xcschemes"
                schemes.mkdir(parents=True)
                (schemes / "original.xcscheme").write_text(
                    '<LaunchAction buildConfiguration="Debug"/>', encoding="utf-8")
                run(binary, project)
                self.assertEqual({p.name for p in schemes.iterdir()},
                                 {expected + " Debug.xcscheme", expected + " Release.xcscheme"})
                self.assertIn('buildConfiguration="RelWithDebInfo"',
                              (schemes / (expected + " Release.xcscheme")).read_text(encoding="utf-8"))


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="project-name-cli-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name).resolve()
        self.assertNotIn(ROOT, self.base.parents, "fixtures must be outside the repository")

    def cli(self, *args, binary=CLI):
        return run(binary, *args, cwd=self.base)

    def generated_files(self, project):
        paths = [project / "CMakeLists.txt", project / "CMakePresets.json"]
        paths += sorted((project / ".vscode").glob("*.json"))
        paths += sorted(project.glob("build-web.*"))
        return {p.relative_to(project).as_posix(): p.read_bytes() for p in paths}

    def assert_build(self, project, expected, external_build=False):
        # Keep compilation small: the fixture needs no display or framework API.
        shutil.rmtree(project / "src")
        (project / "src").mkdir()
        (project / "src/main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        build = (self.base / ("build-" + expected) if external_build
                 else project / ("build-" + NATIVE))
        if external_build:
            # CMake 3.28 rejects '#' in internal custom-target OUTPUT paths.
            # Validate the generated project/target with a build path without '#'.
            preset_path = project / "CMakePresets.json"
            presets = json.loads(preset_path.read_text(encoding="utf-8"))
            for preset in presets["configurePresets"]:
                if preset["name"] == NATIVE:
                    preset["binaryDir"] = str(build)
            preset_path.write_text(json.dumps(presets), encoding="utf-8")
        if external_build:
            query = build / ".cmake/api/v1/query/codemodel-v2"
            query.parent.mkdir(parents=True, exist_ok=True)
            query.touch()
        run("cmake", "-S", project, "-B", build, "-DTRUSSC_DIR=" + str(ROOT / "core"),
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON", "-DCMAKE_BUILD_TYPE=RelWithDebInfo")
        if external_build:
            # On CMake 3.28 Unix Makefiles also mishandles '#' in source paths.
            # Configure is sufficient here; inspect CMake's actual output name.
            targets = (json.loads(path.read_text(encoding="utf-8"))
                       for path in (build / ".cmake/api/v1/reply").glob("target-*.json"))
            target = next(target for target in targets if target.get("name") == expected)
            artifact = Path(target["artifacts"][0]["path"])
            self.assertEqual(artifact.name, expected + (".exe" if sys.platform == "win32" else ""))
            return
        run("cmake", "--build", build, "--target", expected, "--config", "RelWithDebInfo")
        executable = project / "bin" / expected
        if sys.platform == "win32":
            executable = executable.with_name(expected + ".exe")
        elif sys.platform == "darwin":
            executable = executable.with_name(expected + ".app") / "Contents/MacOS" / expected
        self.assertTrue(executable.is_file(), executable)
        self.cli("run", "--relwithdebinfo", "-j", "2", "-p", project)
        self.assertTrue(project.is_dir())

    def test_new_and_update_keep_folder_and_use_sanitized_target(self):
        cases = [("sample project", "sample_project"), ("日本語", "___"),
                 ("+ ( ) & '", "+________"), ("hash#project", "hash_project"),
                 ("9valid_Name-1.2+3", "9valid_Name-1.2+3"),
                 ("all", "app_all"), ("test", "app_test")]
        if sys.platform != "win32":  # Windows cannot create this folder.
            cases.append(("NUL", "app_NUL"))
        for folder, expected in cases:
            with self.subTest(folder=folder):
                project = self.base / folder
                external_build = "#" in folder
                ide = "cmake" if external_build else "vscode"
                self.cli("new", project, "--tc-root", ROOT, "--ide", ide)
                self.assertEqual(project.name, folder)
                if not external_build:
                    launch = json.loads((project / ".vscode/launch.json").read_text(encoding="utf-8"))
                    config = launch["configurations"][0]
                    for host in ("linux", "windows", "osx"):
                        self.assertIn(expected if folder != expected else "${workspaceFolderBasename}",
                                      config[host]["program"])
                self.assert_build(project, expected, external_build)
                self.cli("update", "-p", project, "--tc-root", ROOT)
                self.assertIn(f"project({expected})", (project / "CMakeLists.txt").read_text(encoding="utf-8"))
                self.assert_build(project, expected, external_build)

    @unittest.skipUnless(BASELINE, "set TRUSSCLI_BASELINE for main byte identity")
    def test_valid_name_is_byte_identical_to_main(self):
        for ide in ("vscode", "cursor", "cmake"):
            project = self.base / "9valid_Name-1.2+3"
            with self.subTest(ide=ide):
                self.cli("new", project, "--ide", ide, "--tc-root", ROOT, binary=BASELINE)
                original_new = self.generated_files(project)
                self.cli("update", "-p", project, "--ide", ide, "--tc-root", ROOT, binary=BASELINE)
                original_update = self.generated_files(project)
                shutil.rmtree(project)
                self.cli("new", project, "--ide", ide, "--tc-root", ROOT)
                self.assertEqual(original_new, self.generated_files(project))
                self.cli("update", "-p", project, "--ide", ide, "--tc-root", ROOT)
                self.assertEqual(original_update, self.generated_files(project))
                shutil.rmtree(project)

    @unittest.skipUnless(sys.platform == "linux", "Linux helper script fixture")
    def test_web_script_uses_binary_name(self):
        fake_bin = self.base / "fake-bin"
        fake_bin.mkdir()
        for tool in ("cmake", "ninja"):
            executable = fake_bin / tool
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(0o755)
        env = {"PATH": str(fake_bin) + os.pathsep + os.environ["PATH"]}
        project = self.base / "sample project"
        run(CLI, "new", project, "--ide", "cmake", "--web", "--tc-root", ROOT, env=env)
        script = (project / "build-web.sh").read_text(encoding="utf-8")
        self.assertIn("http://localhost:8080/sample_project.html", script)
        self.assertNotIn("sample project.html", script)
        run(CLI, "update", "-p", project, "--tc-root", ROOT, env=env)
        self.assertEqual(script, (project / "build-web.sh").read_text(encoding="utf-8"))

        if BASELINE:
            project = self.base / "valid_name-1.2+3"
            run(BASELINE, "new", project, "--ide", "cmake", "--web", "--tc-root", ROOT, env=env)
            original_new = self.generated_files(project)
            run(BASELINE, "update", "-p", project, "--tc-root", ROOT, env=env)
            original_update = self.generated_files(project)
            shutil.rmtree(project)
            run(CLI, "new", project, "--ide", "cmake", "--web", "--tc-root", ROOT, env=env)
            self.assertEqual(original_new, self.generated_files(project))
            run(CLI, "update", "-p", project, "--tc-root", ROOT, env=env)
            self.assertEqual(original_update, self.generated_files(project))


if __name__ == "__main__":
    unittest.main()
