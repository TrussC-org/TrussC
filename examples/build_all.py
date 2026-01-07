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
        return {
            "os": "windows",
            "build_dir": "build-windows",
            "cmake_generator": "Visual Studio 17 2022",
            "pg_bin_path": ["projectGenerator", "tools", "projectGenerator", "bin", "projectGenerator.exe"]
        }
    elif system == "Darwin":
        return {
            "os": "macos",
            "build_dir": "build-macos",
            "cmake_generator": None, 
            "pg_bin_path": ["projectGenerator", "tools", "projectGenerator", "bin", "projectGenerator.app", "Contents", "MacOS", "projectGenerator"]
        }
    else:
        return {
            "os": "linux",
            "build_dir": "build-linux",
            "cmake_generator": None,
            "pg_bin_path": ["projectGenerator", "tools", "projectGenerator", "bin", "projectGenerator"]
        }

def find_project_generator(root_dir, platform_info):
    exe_name = "projectGenerator.exe" if platform_info["os"] == "windows" else "projectGenerator"
    
    # Priority list of paths to check
    search_paths = [
        os.path.join(root_dir, *platform_info["pg_bin_path"]),
        os.path.join(root_dir, "projectGenerator", "tools", "projectGenerator", "bin", exe_name),
        os.path.join(root_dir, "projectGenerator", exe_name),
        os.path.join(root_dir, "projectGenerator", "tools", "projectGenerator", "build", "Release", exe_name),
        os.path.join(root_dir, "projectGenerator", "tools", "projectGenerator", "bin", "Release", exe_name),
        os.path.join(root_dir, "projectGenerator", "tools", "projectGenerator", "bin", "projectGenerator.app", "Contents", "MacOS", "projectGenerator"),
    ]

    for pg_path in search_paths:
        if os.path.exists(pg_path):
            Colors.print(f"Found ProjectGenerator at: {pg_path}", Colors.GREEN)
            return pg_path
            
    Colors.print(f"ProjectGenerator not found. Searched locations:", Colors.RED)
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

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="TrussC Batch Build Script")
    parser.add_argument('--clean', action='store_true', help="Clean build directories before building")
    parser.add_argument('--web', action='store_true', help="Also build for WebAssembly")
    parser.add_argument('--web-only', action='store_true', help="Build for WebAssembly only (skip native build)")
    parser.add_argument('--test-only', action='store_true', help="Build ONLY AllFeaturesExample for quick CI check")
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
    print("")

    if args.test_only:
        test_example = os.path.join(ROOT_DIR, "examples", "tests", "AllFeaturesExample")
        if os.path.exists(test_example):
            example_dirs = [test_example]
        else:
            Colors.print(f"AllFeaturesExample not found at: {test_example}", Colors.RED)
            sys.exit(1)
    else:
        example_dirs = find_examples(ROOT_DIR)
    
    if not example_dirs:
        Colors.print("No example directories found!", Colors.RED)
        sys.exit(1)

    total = len(example_dirs)
    print(f"Found {total} examples")
    print("")

    if args.clean:
        trussc_dir = os.path.join(ROOT_DIR, "trussc")
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

        pg_cmd = [str(pg_bin), "--update", example_dir, "--tc-root", ROOT_DIR]
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

            cmd_config = ["cmake", "-S", ".", "-B", build_dir_name]
            if platform_info["cmake_generator"]:
                cmd_config.extend(["-G", platform_info["cmake_generator"]])
            
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