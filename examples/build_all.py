#!/usr/bin/env python3
import os
import sys
import platform
import subprocess
import shutil
import argparse
import glob
from pathlib import Path
import time

# =============================================================================
# Configuration & Constants
# =============================================================================

# Colors for terminal output
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

    @staticmethod
    def print(msg, color=ENDC):
        if sys.stdout.isatty():
            print(f"{color}{msg}{Colors.ENDC}")
        else:
            print(msg)

# Paths
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

# =============================================================================
# Helper Functions
# =============================================================================

def get_platform_info():
    system = platform.system()
    if system == "Windows":
        # Default to the VS generator for local dev, but allow CI (or anyone) to
        # override it via TRUSSC_CMAKE_GENERATOR — e.g. "Ninja", which lets
        # sccache cache compilation (the compiler-launcher hook is ignored by the
        # Visual Studio generator). Requires cl.exe/ninja on PATH (vcvars).
        win_gen = os.environ.get("TRUSSC_CMAKE_GENERATOR") or "Visual Studio 17 2022"
        return {
            "os": "windows",
            "build_dir": "build-windows",
            "cmake_generator": win_gen,
            "pg_bin_path": ["tools", "bin", "trusscli.exe"]
        }
    elif system == "Darwin":
        return {
            "os": "macos",
            "build_dir": "build-macos",
            "cmake_generator": None,
            "pg_bin_path": ["tools", "bin", "trusscli.app", "Contents", "MacOS", "trusscli"]
        }
    else:
        return {
            "os": "linux",
            "build_dir": "build-linux",
            "cmake_generator": None,
            "pg_bin_path": ["tools", "bin", "trusscli"]
        }

def find_project_generator(root_dir, platform_info):
    exe_name = "trusscli.exe" if platform_info["os"] == "windows" else "trusscli"

    # Priority list of paths to check
    search_paths = [
        os.path.join(root_dir, *platform_info["pg_bin_path"]),
        os.path.join(root_dir, "tools", "bin", exe_name),
        os.path.join(root_dir, "tools", "bin", "trusscli.app", "Contents", "MacOS", "trusscli"),
        os.path.join(root_dir, "tools", "bin", "Release", exe_name),
    ]

    for pg_path in search_paths:
        if os.path.exists(pg_path):
            Colors.print(f"Found trusscli at: {pg_path}", Colors.GREEN)
            return pg_path

    Colors.print(f"trusscli not found. Searched locations:", Colors.RED)
    for p in search_paths:
        print(f" - {p}")
    sys.exit(1)

def find_examples(root_dir):
    examples_dir = os.path.join(root_dir, "examples")
    addons_dir = os.path.join(root_dir, "addons")
    
    example_paths = []
    
    for root, dirs, files in os.walk(examples_dir):
        if "src" in dirs:
            if "templates" in root or "tools" in root or "build" in root or "bin" in root or "emscripten" in root or "CMakeFiles" in root:
                continue
            example_paths.append(root)

    if os.path.exists(addons_dir):
        for addon in os.listdir(addons_dir):
            addon_path = os.path.join(addons_dir, addon)
            if os.path.isdir(addon_path):
                for item in os.listdir(addon_path):
                    if item.startswith("example-"):
                        example_path = os.path.join(addon_path, item)
                        if os.path.exists(os.path.join(example_path, "src")):
                            example_paths.append(example_path)

    return sorted(list(set(example_paths)))

def find_addon_tests(root_dir):
    # Behavioral test harnesses bundled with monorepo addons: addons/*/tests/
    # (a console TrussC project whose main() returns non-zero on failure). Same
    # convention as standalone addon repos (ci-actions runs tests/ if present).
    addons_dir = os.path.join(root_dir, "addons")
    test_paths = []
    if os.path.exists(addons_dir):
        for addon in sorted(os.listdir(addons_dir)):
            tdir = os.path.join(addons_dir, addon, "tests")
            if os.path.isdir(tdir) and os.path.exists(os.path.join(tdir, "src")):
                test_paths.append(tdir)
    return test_paths

def find_core_tests(root_dir):
    # Headless behavioral regression tests for the core: core/tests/*/ (each a
    # console TrussC project whose main() returns non-zero on failure). Same
    # convention as addon tests, but owned by core.
    tests_dir = os.path.join(root_dir, "core", "tests")
    test_paths = []
    if os.path.exists(tests_dir):
        for name in sorted(os.listdir(tests_dir)):
            tdir = os.path.join(tests_dir, name)
            if os.path.isdir(tdir) and os.path.exists(os.path.join(tdir, "src")):
                test_paths.append(tdir)
    return test_paths

def find_core_unit_tests(root_dir):
    # Standalone headless unit tests for the core: core/tests/*/ dirs that ship
    # their OWN committed CMakeLists.txt (built with plain cmake, NOT trusscli).
    # Used for tests that must compile sokol/etc. directly (e.g. with the dummy
    # backend) and therefore cannot link libTrussC. Distinguished from trusscli
    # project tests (which have a src/ dir and a generated, gitignored CMakeLists).
    tests_dir = os.path.join(root_dir, "core", "tests")
    test_paths = []
    if os.path.exists(tests_dir):
        for name in sorted(os.listdir(tests_dir)):
            tdir = os.path.join(tests_dir, name)
            # Standalone == committed CMakeLists.txt and no src/ (trusscli project
            # tests have src/ and only a generated, gitignored CMakeLists.txt).
            if (os.path.isdir(tdir)
                    and os.path.exists(os.path.join(tdir, "CMakeLists.txt"))
                    and not os.path.exists(os.path.join(tdir, "src"))):
                test_paths.append(tdir)
    return test_paths

def find_test_binary(test_dir, platform_info):
    # trusscli names the binary after the project dir. addons/*/tests -> "tests";
    # core/tests/<name> -> "<name>". On macOS a TrussC app is
    # <name>.app/Contents/MacOS/<name>; on Windows <name>.exe. Search the tree.
    base = os.path.basename(os.path.normpath(test_dir))
    names = [base + ".exe"] if platform_info["os"] == "windows" else [base, base + "_debug"]
    for name in names:
        for p in glob.glob(os.path.join(test_dir, "**", name), recursive=True):
            if os.path.isfile(p) and (platform_info["os"] == "windows" or os.access(p, os.X_OK)):
                return p
    return None

def run_command(cmd, cwd, verbose=False):
    try:
        if verbose:
            subprocess.run(cmd, cwd=cwd, check=True)
        else:
            subprocess.run(cmd, cwd=cwd, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return True
    except subprocess.CalledProcessError as e:
        if not verbose:
            if e.stdout:
                print(e.stdout.decode('utf-8', errors='replace'))
        return False

def cmake_config_cmd(build_dir_name, platform_info):
    # Assemble the `cmake -S . -B <dir>` configure command for the current
    # platform/generator. Single-config generators (Ninja, Makefiles) need the
    # build type pinned at configure time (--config is multi-config only), so
    # add it when a non-multi-config generator is explicitly selected. The
    # default (None) path — mac/linux Makefiles — is left untouched.
    cmd = ["cmake", "-S", ".", "-B", build_dir_name]
    gen = platform_info["cmake_generator"]
    if gen:
        cmd.extend(["-G", gen])
        if "Visual Studio" not in gen and "Xcode" not in gen:
            cmd.append("-DCMAKE_BUILD_TYPE=Release")
    return cmd


def build_and_run_test(test_dir, pg_bin, platform_info, args):
    # Build a native console test project, then RUN it (non-zero exit = failure).
    # Returns (ok, stage) where stage names what failed for the summary.
    pg_cmd = [str(pg_bin), "update", "-p", test_dir, "--tc-root", ROOT_DIR, "--ide", "cmake"]
    if not run_command(pg_cmd, cwd=ROOT_DIR, verbose=args.verbose):
        return False, "update"

    build_dir_name = platform_info["build_dir"]
    cmd_config = cmake_config_cmd(build_dir_name, platform_info)
    if not run_command(cmd_config, cwd=test_dir, verbose=args.verbose):
        return False, "configure"

    cmd_build = ["cmake", "--build", build_dir_name, "--config", "Release"]
    if "Visual Studio" not in (platform_info["cmake_generator"] or ""):
        import multiprocessing
        cmd_build.extend(["-j", str(multiprocessing.cpu_count())])
    if not run_command(cmd_build, cwd=test_dir, verbose=args.verbose):
        return False, "build"

    binary = find_test_binary(test_dir, platform_info)
    if not binary:
        Colors.print("  Test binary not found after build!", Colors.RED)
        return False, "binary-missing"

    Colors.print(f"  Running {os.path.relpath(binary, test_dir)} ...", Colors.YELLOW)
    if not run_command([binary], cwd=test_dir, verbose=True):  # always stream test output
        return False, "run"
    return True, None

def build_and_run_unit_test(test_dir, pg_bin, platform_info, args):
    # Build a standalone CMake test (its own committed CMakeLists.txt, no
    # trusscli), then RUN it (non-zero exit = failure). pg_bin is unused.
    build_dir_name = platform_info["build_dir"]
    cmd_config = cmake_config_cmd(build_dir_name, platform_info)
    if not run_command(cmd_config, cwd=test_dir, verbose=args.verbose):
        return False, "configure"

    cmd_build = ["cmake", "--build", build_dir_name, "--config", "Release"]
    if "Visual Studio" not in (platform_info["cmake_generator"] or ""):
        import multiprocessing
        cmd_build.extend(["-j", str(multiprocessing.cpu_count())])
    if not run_command(cmd_build, cwd=test_dir, verbose=args.verbose):
        return False, "build"

    binary = find_test_binary(test_dir, platform_info)
    if not binary:
        Colors.print("  Test binary not found after build!", Colors.RED)
        return False, "binary-missing"

    Colors.print(f"  Running {os.path.relpath(binary, test_dir)} ...", Colors.YELLOW)
    if not run_command([binary], cwd=test_dir, verbose=True):  # always stream test output
        return False, "run"
    return True, None

def run_test_suite(tests, label, pg_bin, platform_info, args, builder=build_and_run_test):
    # Build AND run a set of console test projects (addon or core). Streams each
    # test's output; returns 0 if all pass, 1 if any fails.
    Colors.print(f"Found {len(tests)} {label} test harness(es)", Colors.YELLOW)
    print("")
    failed = []
    for i, tdir in enumerate(tests):
        name = os.path.relpath(tdir, ROOT_DIR)
        Colors.print(f"[{i+1}/{len(tests)}] Building & running: {name}", Colors.YELLOW)
        ok, stage = builder(tdir, pg_bin, platform_info, args)
        if ok:
            Colors.print("  Passed!", Colors.GREEN)
        else:
            Colors.print(f"  FAILED ({stage})", Colors.RED)
            failed.append(f"{name} ({stage})")
    print("")
    Colors.print(f"=== {label.capitalize()} Test Summary ===", Colors.BLUE)
    print(f"Total:  {len(tests)}")
    Colors.print(f"Passed: {len(tests) - len(failed)}", Colors.GREEN)
    if failed:
        Colors.print(f"Failed: {len(failed)}", Colors.RED)
        for f in failed:
            print(f"  - {f}")
        return 1
    Colors.print(f"All {label} tests passed!", Colors.GREEN)
    return 0

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="TrussC Batch Build Script")
    parser.add_argument('--clean', action='store_true', help="Clean build directories before building")
    parser.add_argument('--web', action='store_true', help="Also build for WebAssembly")
    parser.add_argument('--web-only', action='store_true', help="Build for WebAssembly only (skip native build)")
    parser.add_argument('--test-only', action='store_true', help="Build ONLY AllFeaturesExample for quick CI check")
    parser.add_argument('--one-per-addon', action='store_true', help="Build the first example-* of each bundled addon (per-addon dependency compile coverage)")
    parser.add_argument('--test-hot-reload', action='store_true', help="Build ONLY HotReloadExample (exercises the host/guest split + addon includes)")
    parser.add_argument('--addon-tests-only', action='store_true', help="Build AND RUN every addons/*/tests/ harness (console, non-zero exit fails). No-op if none exist.")
    parser.add_argument('--core-tests-only', action='store_true', help="Build AND RUN every core/tests/*/ harness (console, non-zero exit fails). No-op if none exist.")
    parser.add_argument('--verbose', action='store_true', help="Show detailed build output")
    args = parser.parse_args()

    if args.web_only:
        args.web = True

    platform_info = get_platform_info()
    pg_bin = find_project_generator(ROOT_DIR, platform_info)
    
    Colors.print("=== TrussC Examples Batch Build (Python) ===", Colors.BLUE)
    Colors.print(f"Platform: {platform_info['os']}", Colors.YELLOW)
    Colors.print(f"ProjectGenerator: {pg_bin}", Colors.YELLOW)
    if args.web_only:
        Colors.print("Mode: Web Only", Colors.YELLOW)
    if args.test_only:
        Colors.print("Mode: AllFeaturesExample Only", Colors.YELLOW)
    if args.one_per_addon:
        Colors.print("Mode: One example per addon", Colors.YELLOW)
    if args.test_hot_reload:
        Colors.print("Mode: HotReloadExample Only", Colors.YELLOW)
    if args.addon_tests_only:
        Colors.print("Mode: Addon tests (build + run)", Colors.YELLOW)
    if args.core_tests_only:
        Colors.print("Mode: Core tests (build + run)", Colors.YELLOW)
    print("")

    # Addon test harnesses (addons/*/tests/): build AND run each. A cheap,
    # behavioral gate meant to run on every CI, separate from the example
    # builds. No-op (exit 0) when no addon ships a tests/ dir.
    if args.addon_tests_only:
        tests = find_addon_tests(ROOT_DIR)
        if not tests:
            Colors.print("No addon tests found (addons/*/tests/); nothing to do.", Colors.YELLOW)
            sys.exit(0)
        sys.exit(run_test_suite(tests, "addon", pg_bin, platform_info, args))

    # Core behavioral tests (core/tests/*/): same build+run gate, owned by core.
    # Two flavours: trusscli project tests (src/, link libTrussC) and standalone
    # CMake unit tests (committed CMakeLists.txt, no libTrussC link — e.g. sokol
    # dummy-backend tests). Both run; the job fails if either has a failure.
    if args.core_tests_only:
        tests = find_core_tests(ROOT_DIR)
        unit_tests = find_core_unit_tests(ROOT_DIR)
        if not tests and not unit_tests:
            Colors.print("No core tests found (core/tests/*/); nothing to do.", Colors.YELLOW)
            sys.exit(0)
        rc = 0
        if tests:
            rc |= run_test_suite(tests, "core", pg_bin, platform_info, args)
        if unit_tests:
            rc |= run_test_suite(unit_tests, "core unit", pg_bin, platform_info, args,
                                 builder=build_and_run_unit_test)
        sys.exit(rc)

    if args.test_only:
        test_example = os.path.join(ROOT_DIR, "examples", "tests", "AllFeaturesExample")
        if os.path.exists(test_example):
            example_dirs = [test_example]
        else:
            Colors.print(f"AllFeaturesExample not found at: {test_example}", Colors.RED)
            sys.exit(1)
    elif args.test_hot_reload:
        hr_example = os.path.join(ROOT_DIR, "examples", "tests", "HotReloadExample")
        if os.path.exists(hr_example):
            example_dirs = [hr_example]
        else:
            Colors.print(f"HotReloadExample not found at: {hr_example}", Colors.RED)
            sys.exit(1)
    elif args.one_per_addon:
        # Dependency compile coverage: one example is enough to prove an addon
        # (and its fetched third-party deps) still compiles on this platform.
        # find_examples() is sorted, so this picks each addon's first example.
        addons_dir = os.path.join(ROOT_DIR, "addons")
        seen = set()
        example_dirs = []
        for path in find_examples(ROOT_DIR):
            if not path.startswith(addons_dir + os.sep):
                continue
            addon = os.path.relpath(path, addons_dir).split(os.sep)[0]
            if addon not in seen:
                seen.add(addon)
                example_dirs.append(path)
    else:
        example_dirs = find_examples(ROOT_DIR)

    if not example_dirs:
        Colors.print("No example directories found!", Colors.RED)
        sys.exit(1)

    total = len(example_dirs)
    print(f"Found {total} examples")
    print("")

    if args.clean:
        trussc_dir = os.path.join(ROOT_DIR, "core")
        Colors.print("Cleaning TrussC shared build...", Colors.YELLOW)
        if not args.web_only:
            shared_build = os.path.join(trussc_dir, platform_info["build_dir"])
            if os.path.exists(shared_build):
                shutil.rmtree(shared_build)
        
        if args.web:
            web_build = os.path.join(trussc_dir, "build-web")
            if os.path.exists(web_build):
                shutil.rmtree(web_build)

    success_count = 0
    failed_count = 0
    failed_list = []

    for i, example_dir in enumerate(example_dirs):
        try:
            example_name = os.path.relpath(example_dir, ROOT_DIR)
        except ValueError:
            example_name = example_dir

        Colors.print(f"[{i+1}/{total}] Updating & Building: {example_name}", Colors.YELLOW)

        # Regenerate build files for each example before building
        pg_cmd = [str(pg_bin), "update", "-p", example_dir, "--tc-root", ROOT_DIR, "--ide", "cmake"]
        if args.web:
            pg_cmd.append("--web")
        
        if not run_command(pg_cmd, cwd=ROOT_DIR, verbose=args.verbose):
            Colors.print(f"  Project update failed!", Colors.RED)
            failed_count += 1
            failed_list.append(f"{example_name} (update)")
            continue

        if not args.web_only:
            build_dir_name = platform_info["build_dir"]
            build_dir = os.path.join(example_dir, build_dir_name)
            
            if args.clean and os.path.exists(build_dir):
                shutil.rmtree(build_dir)

            cmd_config = cmake_config_cmd(build_dir_name, platform_info)

            if not run_command(cmd_config, cwd=example_dir, verbose=args.verbose):
                Colors.print(f"  Native Configure failed!", Colors.RED)
                failed_count += 1
                failed_list.append(f"{example_name} (native-conf)")
                continue

            cmd_build = ["cmake", "--build", build_dir_name, "--config", "Release"]
            
            if "Visual Studio" not in (platform_info["cmake_generator"] or ""):
                import multiprocessing
                jobs = str(multiprocessing.cpu_count())
                cmd_build.extend(["-j", jobs])

            if not run_command(cmd_build, cwd=example_dir, verbose=args.verbose):
                Colors.print(f"  Native Build failed!", Colors.RED)
                failed_count += 1
                failed_list.append(f"{example_name} (native-build)")
                continue

        if args.web:
            build_dir_web = os.path.join(example_dir, "build-web")
            if args.clean and os.path.exists(build_dir_web):
                shutil.rmtree(build_dir_web)

            if shutil.which("emcmake"):
                cmd_config_web = ["emcmake", "cmake", "-S", ".", "-B", "build-web"]
            else:
                cmd_config_web = ["cmake", "-S", ".", "-B", "build-web"]
            
            if not run_command(cmd_config_web, cwd=example_dir, verbose=args.verbose):
                Colors.print(f"  Web Configure failed!", Colors.RED)
                failed_count += 1
                failed_list.append(f"{example_name} (web-conf)")
                continue

            cmd_build_web = ["cmake", "--build", "build-web"]
            import multiprocessing
            jobs = str(multiprocessing.cpu_count())
            cmd_build_web.extend(["-j", jobs])

            if not run_command(cmd_build_web, cwd=example_dir, verbose=args.verbose):
                Colors.print(f"  Web Build failed!", Colors.RED)
                failed_count += 1
                failed_list.append(f"{example_name} (web-build)")
                continue

        Colors.print("  Success!", Colors.GREEN)
        success_count += 1

    print("")
    Colors.print("=== Build Summary ===", Colors.BLUE)
    print(f"Total:   {total}")
    Colors.print(f"Success: {success_count}", Colors.GREEN)
    
    if failed_count > 0:
        Colors.print(f"Failed:  {failed_count}", Colors.RED)
        print("")
        Colors.print("Failed examples:", Colors.RED)
        for fail in failed_list:
            print(f"  - {fail}")
        sys.exit(1)
    else:
        Colors.print(f"Failed:  0", Colors.ENDC)
        print("")
        Colors.print("All examples built successfully!", Colors.GREEN)
        sys.exit(0)

if __name__ == "__main__":
    main()