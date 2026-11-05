#!/usr/bin/env python3
import os
import sys
import platform
import subprocess
import shutil
import argparse
import re
import contextlib
from collections import deque
from pathlib import Path
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_build_warnings import run_build

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
        close_dots()
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

# =============================================================================
# Short output (#1240)
#
# By default build_all prints a short screen: a dot line for progress, the
# diagnostics and FAIL lines, per-suite counts and a final summary. The full
# output of every test and every build goes to its own log file. --verbose
# restores the full output (CI uses it); the log files are written either way.
# =============================================================================

# Logs live in <repo>/build-logs/ (ignored by git through build-*/), one file
# per test project, overwritten on each run: build-logs/core/tests/foo.log.
LOG_DIR_NAME = "build-logs"
# False once main() has seen no --verbose: only used by code that has no
# `args` at hand (find_* skip notices, trusscli lookup). Direct callers keep
# the full output.
VERBOSE = True

# Lines of a failed command or test that is shown on the screen.
FAIL_CONTEXT_LINES = 5      # lines after a FAIL line that belong to it
MAX_FAIL_LINES = 30         # screen lines for the FAIL lines of one test
TAIL_LINES = 10             # last lines of a crashed or failed command
MAX_NAMES = 3               # file names listed in ERROR: / WARNING:

_dot_open = False

def close_dots():
    # A line passed through while the dot line is open starts on a new line.
    global _dot_open
    if _dot_open:
        sys.stdout.write("\n")
        sys.stdout.flush()
        _dot_open = False

def say(msg=""):
    close_dots()
    print(msg, flush=True)

def write_dots(text):
    global _dot_open
    sys.stdout.write(text)
    sys.stdout.flush()
    _dot_open = True

class Progress:
    # A dot per 5 %, "50%" instead of the 10th dot and "100%" instead of the
    # 20th: "Building myApp .........50%.........100%". update() takes the
    # finished fraction (0..1); dots only ever move forward.
    STEPS = 20

    def __init__(self, label):
        self.label = label
        self.step = 0
        self.opened = False

    def begin(self):
        close_dots()
        write_dots(self.label + " ")
        self.opened = True

    def update(self, fraction):
        target = min(self.STEPS, int(fraction * self.STEPS + 1e-9))
        if target <= self.step:
            return
        if not self.opened:
            self.begin()
        while self.step < target:
            self.step += 1
            if self.step == self.STEPS:
                write_dots("100%")
            elif self.step == self.STEPS // 2:
                write_dots("50%")
            else:
                write_dots(".")

    def dot(self):
        # No percentage known (unknown build output): one dot per step.
        if not self.opened:
            self.begin()
        write_dots(".")

    def finish(self):
        if not self.opened:
            self.begin()
        close_dots()

# Output of make / ninja / MSBuild that is only progress or configure chatter.
MAKE_PROGRESS = re.compile(r"^\[\s*(\d+)%\]")
NINJA_PROGRESS = re.compile(r"^\[(\d+)/(\d+)\]")
MSVC_COMPILE_STEP = re.compile(r"^\s*[\w\-. /\\]+\.(?:cpp|cc|cxx|c|mm|m)\s*$")
BUILD_NOISE = re.compile(
    r"^(?:-- "
    r"|Scanning dependencies"
    r"|Built target "
    r"|Consolidate compiler generated dependencies"
    r"|g?make(?:\[\d+\])?: (?:Entering|Leaving) directory"
    r"|Compiling\.\.\.\s*$)")
# trusscli update chatter (the Note: line is not hidden, see ShortOutput).
UPDATE_NOISE = re.compile(
    r"^(?:Updating project\.\.\.|Updating addons\.make\.\.\."
    r"|Writing CMakeLists\.txt\.\.\.|Writing CMakePresets\.json\.\.\."
    r"|Checking: |Not found: |Update complete!|Project updated: )")

# Diagnostics: GCC/Clang, MSVC, CMake, and link errors without a source file.
GCC_DIAG = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^\s:][^:]*?)(?::\d+(?::\d+)?)?: (?:fatal )?(?P<kind>error|warning):")
MSVC_DIAG = re.compile(r"^(?P<file>.+?)\(\d+(?:,\d+)?\)\s*: (?:fatal )?(?P<kind>error|warning) [A-Z]+\d+")
CMAKE_DIAG = re.compile(r"^CMake (?P<kind>Error|Warning)(?: \(dev\))?(?: at (?P<file>[^:\s]+))?")
LINK_DIAG = re.compile(r"undefined reference to|multiple definition of|: error LNK\d+|\bLNK\d{4}: ")

def diagnostic_of(line):
    # (kind, file or None) for a compiler/linker diagnostic line, else None.
    line = line.rstrip()
    m = MSVC_DIAG.match(line) or GCC_DIAG.match(line)
    if m:
        return m.group("kind"), os.path.basename(m.group("file").replace("\\", "/"))
    m = CMAKE_DIAG.match(line)
    if m:
        file = m.group("file")
        return m.group("kind").lower(), os.path.basename(file) if file else "CMake"
    if LINK_DIAG.search(line):
        return "error", None
    return None

class ShortOutput:
    # Screen state of one short-output run: diagnostics seen in builds, the
    # test groups that finished and the failures.
    def __init__(self):
        self.errors = []        # file name (None: link error) per error
        self.warnings = []
        self.groups = []        # (label, passed, total)
        self.failures = []      # names of failed tests / projects
        self.shown_notes = set()
        self.started = time.monotonic()

    def build_line(self, line, progress=None, update=False):
        # Handle one line of a build command: returns True if it was shown.
        text = line.rstrip("\n")
        m = MAKE_PROGRESS.match(text)
        if m:
            if progress:
                progress.update(int(m.group(1)) / 100)
            return False
        m = NINJA_PROGRESS.match(text)
        if m:
            if progress and int(m.group(2)):
                progress.update(int(m.group(1)) / int(m.group(2)))
            return False
        if BUILD_NOISE.match(text) or (update and UPDATE_NOISE.match(text)):
            return False
        if MSVC_COMPILE_STEP.match(text):
            if progress:
                progress.dot()
            return False
        diag = diagnostic_of(text)
        if diag:
            (self.errors if diag[0] == "error" else self.warnings).append(diag[1])
        elif is_pass_line(text):
            # A build step that runs a test (x11Regression) prints PASS lines.
            return False
        elif update:
            # The same Note: of every trusscli update is shown once.
            if text in self.shown_notes:
                return False
            self.shown_notes.add(text)
        say(text)
        return True

    def names(self, files):
        seen = []
        for f in files:
            f = f or "link failed"
            if f not in seen:
                seen.append(f)
        shown = ", ".join(seen[:MAX_NAMES])
        if len(seen) > MAX_NAMES:
            shown += f", +{len(seen) - MAX_NAMES} more"
        return shown

    def summary(self, rc):
        elapsed = int(time.monotonic() - self.started)
        total = sum(g[2] for g in self.groups)
        passed = sum(g[1] for g in self.groups)
        took = f"{elapsed // 60}m{elapsed % 60:02d}s"
        where = f"Logs: {display_path(log_dir())}"
        if rc == 0:
            what = f"{passed}/{total} passed" if self.groups else "nothing failed"
            lines = [f"Done: {what} ({took}). {where}"]
        else:
            what = f"{passed}/{total} passed, " if self.groups else ""
            lines = [f"Failed: {what}{len(self.failures)} failed ({took}). {where}"]
        if self.errors:
            lines.append(f"ERROR: {len(self.errors)} ({self.names(self.errors)})")
        if self.warnings:
            lines.append(f"WARNING: {len(self.warnings)} ({self.names(self.warnings)})")
        if self.failures:
            lines.append(f"FAIL: {len(self.failures)} ({self.names(self.failures)})")
        return lines

SHORT = ShortOutput()

def reset_short_output():
    global SHORT, _dot_open
    SHORT = ShortOutput()
    _dot_open = False

def log_dir():
    return os.path.join(ROOT_DIR, LOG_DIR_NAME)

def display_path(path):
    # Shortest honest path: relative to the current directory when inside it.
    try:
        rel = os.path.relpath(path)
    except ValueError:
        return path
    return path if rel.startswith("..") else rel

def log_path_for(directory, suffix=""):
    try:
        rel = os.path.relpath(directory, ROOT_DIR)
    except ValueError:
        rel = ""
    if not rel or rel == "." or rel.startswith(".."):
        rel = os.path.basename(os.path.normpath(directory))
    return os.path.join(log_dir(), rel + suffix + ".log")

class RunLog:
    # The full output of one test project (its update, configure, build and
    # run), written as it comes and overwritten on the next run.
    def __init__(self, path):
        self.path = path
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.file = open(path, "w", encoding="utf-8", errors="replace")

    def write(self, text):
        self.file.write(text)
        self.file.flush()

    def command(self, cmd, cwd):
        self.write("$ " + " ".join(str(c) for c in cmd) + f"\n(cwd: {cwd})\n")

    def close(self):
        self.file.close()

_log_stack = []
LAST_LOG_PATH = None

def current_log():
    return _log_stack[-1] if _log_stack else None

@contextlib.contextmanager
def unit_log(directory, suffix=""):
    # Everything run_command / run_test_binary prints inside also goes to the
    # log file of `directory`. An unwritable log folder only loses the file.
    global LAST_LOG_PATH
    path = log_path_for(directory, suffix)
    LAST_LOG_PATH = path
    try:
        log = RunLog(path)
    except OSError:
        log = None
    _log_stack.append(log)
    try:
        yield log
    finally:
        _log_stack.pop()
        if log:
            log.close()

def failed_line(name, stage, log=True):
    # The one line that names a failure and where its full output is.
    where = f" Log: {display_path(LAST_LOG_PATH)}" if log and LAST_LOG_PATH else ""
    say(f"{name}: FAILED ({stage}).{where}")
    SHORT.failures.append(f"{name} ({stage})")

FAIL_WORD = re.compile(r"\bFAIL(?:ED|URE|S)?\b")
PASS_WORD = re.compile(r"\bPASS(?:ED)?\b")
PASS_END = re.compile(r"\bPASS(?:ED)?\s*$")

def is_pass_line(line):
    # "name  PASS" is a pass even when the name says FAILED ("sg pipeline
    # FAILED rolls back  PASS").
    return bool(PASS_END.search(line) or (PASS_WORD.search(line) and not FAIL_WORD.search(line)))

def is_fail_line(line):
    return bool(FAIL_WORD.search(line)) and not PASS_END.search(line)

def summarize_test_output(text):
    # (pass line count, screen lines of the FAIL lines with their details)
    lines = text.splitlines()
    passed = 0
    shown = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if is_fail_line(line):
            shown.append(line)
            j = i + 1
            while (j < len(lines) and j <= i + FAIL_CONTEXT_LINES
                   and not is_fail_line(lines[j]) and not is_pass_line(lines[j])):
                shown.append(lines[j])
                j += 1
            i = j
            continue
        if is_pass_line(line):
            passed += 1
        i += 1
    if len(shown) > MAX_FAIL_LINES:
        more = len(shown) - MAX_FAIL_LINES
        shown = shown[:MAX_FAIL_LINES] + [f"... ({more} more lines in the log)"]
    return passed, shown

def say_indented(lines):
    for line in lines:
        say("    " + line)

def configure_output():
    # Write our stdout/stderr as UTF-8 and replace what cannot be encoded,
    # instead of raising. Test output is echoed through print() and carries
    # non-ASCII text (filePath logs Japanese file names). On Windows a pipe
    # defaults to the ANSI code page (cp1252 on the CI runners), where print()
    # raised UnicodeEncodeError and ended the whole run.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")

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
            if VERBOSE:
                Colors.print(f"Found trusscli at: {pg_path}", Colors.GREEN)
            return pg_path

    Colors.print(f"trusscli not found. Searched locations:", Colors.RED)
    for p in search_paths:
        say(f" - {p}")
    sys.exit(1)


_target_flags_cache = {}

def update_target_flags(pg_bin, web):
    # `trusscli update` keeps the targets saved in a project's (gitignored)
    # CMakePresets.json, so a native sweep after an earlier --web run would
    # reconfigure build-web every time (and fail without emsdk). Name the
    # exact target set instead: --web or --no-web, never android / ios.
    # A trusscli older than --no-web does not keep targets and rejects the
    # flag, so it gets --web only.
    key = str(pg_bin)
    if key not in _target_flags_cache:
        try:
            r = subprocess.run([key, "update", "--help"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, timeout=60, encoding="utf-8", errors="replace")
            _target_flags_cache[key] = "--no-web" in r.stdout
        except (OSError, subprocess.SubprocessError):
            _target_flags_cache[key] = False
    supported = _target_flags_cache[key]
    if web:
        return ["--web", "--no-android", "--no-ios"] if supported else ["--web"]
    return ["--no-web", "--no-android", "--no-ios"] if supported else []

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

SKIPPED_DAILY = set()

def note_skipped(name):
    # daily-only harness left out of this run: one notice per harness, and
    # the short output sums them up in one line (see skipped_line).
    if VERBOSE:
        Colors.print(f"Skipping {name} (daily-only; pass --include-daily)", Colors.YELLOW)
    SKIPPED_DAILY.add(name)

def find_addon_tests(root_dir, include_daily=False, addons=None):
    # Behavioral test harnesses bundled with monorepo addons: addons/*/tests/
    # (a console TrussC project whose main() returns non-zero on failure). Same
    # convention as standalone addon repos (ci-actions runs tests/ if present).
    # A harness with a `daily-only` marker file is heavy to build (e.g. tcxTls
    # fetches and builds mbedTLS) and runs only with include_daily, which the
    # daily workflow and affected-addon PR job pass.
    # An addon may also ship tests-<name>/ next to tests/, e.g. a daily-only
    # harness that needs a heavy dependency the per-PR tests/ avoids
    # (tcxCurl: tests/ is curl-free, tests-curl/ links libcurl).
    addons_dir = os.path.join(root_dir, "addons")
    test_paths = []
    if os.path.exists(addons_dir):
        for addon in sorted(os.listdir(addons_dir)):
            if addons is not None and addon not in addons:
                continue
            addon_dir = os.path.join(addons_dir, addon)
            if not os.path.isdir(addon_dir):
                continue
            names = sorted(n for n in os.listdir(addon_dir) if n == "tests" or n.startswith("tests-"))
            for name in names:
                tdir = os.path.join(addon_dir, name)
                if not (os.path.isdir(tdir) and os.path.exists(os.path.join(tdir, "src"))):
                    continue
                if not include_daily and os.path.isfile(os.path.join(tdir, "daily-only")):
                    note_skipped(os.path.relpath(tdir, root_dir))
                    continue
                test_paths.append(tdir)
    return test_paths

# core/tests/allCoreTests: one app that holds every combinable core test (see
# its local.cmake and core/tests/common/tcCoreTest.h). It is the runner, not
# a test itself.
ALL_CORE_TESTS_NAME = "allCoreTests"

def include_core_test(test_dir, include_daily):
    if not include_daily and os.path.isfile(os.path.join(test_dir, "daily-only")):
        note_skipped(f"core/tests/{os.path.basename(test_dir)}")
        return False
    return True

def find_core_tests(root_dir, include_daily=False):
    # Headless behavioral regression tests for the core: core/tests/*/ (each a
    # console TrussC project whose main() returns non-zero on failure). Same
    # convention as addon tests, but owned by core.
    tests_dir = os.path.join(root_dir, "core", "tests")
    test_paths = []
    if os.path.exists(tests_dir):
        for name in sorted(os.listdir(tests_dir)):
            tdir = os.path.join(tests_dir, name)
            if name == ALL_CORE_TESTS_NAME:
                continue
            if (os.path.isdir(tdir) and os.path.exists(os.path.join(tdir, "src"))
                    and include_core_test(tdir, include_daily)):
                test_paths.append(tdir)
    return test_paths

def is_combined_core_test(test_dir):
    # Built into allCoreTests: a src/main.cpp and no `own-binary` marker. The
    # same rule as core/tests/allCoreTests/local.cmake; keep the two in sync.
    return (os.path.isfile(os.path.join(test_dir, "src", "main.cpp"))
            and not os.path.isfile(os.path.join(test_dir, "own-binary")))

def find_core_web_tests(root_dir, include_daily=False):
    # Core tests that ALSO run as a WebAssembly build under node: those
    # carrying a `web-test` marker file. Web-only behaviour
    # (#ifdef __EMSCRIPTEN__) is otherwise only ever compiled in CI, never run.
    tests = (find_core_tests(root_dir, include_daily=include_daily)
             + find_core_unit_tests(root_dir, include_daily=include_daily))
    return [t for t in tests
            if os.path.isfile(os.path.join(t, "web-test"))]

def find_core_unit_tests(root_dir, include_daily=False):
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
                    and not os.path.exists(os.path.join(tdir, "src"))
                    and include_core_test(tdir, include_daily)):
                test_paths.append(tdir)
    return test_paths

def find_test_binary(test_dir, platform_info):
    # trusscli projects (allCoreTests, own-binary core tests, addon tests and
    # examples) emit into bin/, as set by trussc_app.cmake. Standalone CMake
    # unit tests emit into the platform build dir, or its Release/ subdir for
    # multi-config generators. Never search sibling/manual build trees.
    base = os.path.basename(os.path.normpath(test_dir))
    system = platform_info["os"]
    project = os.path.isdir(os.path.join(test_dir, "src"))
    if project:
        output_dir = os.path.join(test_dir, "bin")
    else:
        build_dir = os.path.join(test_dir, platform_info.get("build_dir", f"build-{system}"))
        # CMake can select a generator via the environment or reuse one from
        # an existing build tree even when configure did not pass -G.
        generator = ""
        multi_config = False
        try:
            with open(os.path.join(build_dir, "CMakeCache.txt"), encoding="utf-8", errors="replace") as cache:
                for line in cache:
                    if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                        generator = line.partition("=")[2].strip()
                    elif line.startswith("CMAKE_CONFIGURATION_TYPES:"):
                        multi_config = True
        except OSError:
            pass
        generator = (generator or platform_info.get("cmake_generator")
                     or os.environ.get("CMAKE_GENERATOR", ""))
        multi_config = multi_config or any(g in generator for g in ("Visual Studio", "Xcode", "Multi-Config"))
        # This standalone shader-ordering fixture defines its executable in
        # consumer/CMakeLists.txt to test cross-directory shader dependencies.
        if base == "shaderTargetOrdering":
            build_dir = os.path.join(build_dir, "consumer")
        output_dir = os.path.join(build_dir, "Release") if multi_config else build_dir

    names = [base + ".exe"] if system == "windows" else [base, base + "_debug"]
    candidates = []
    for name in names:
        if system == "macos" and project:
            candidates.append(os.path.join(output_dir, name + ".app", "Contents", "MacOS", name))
        else:
            candidates.append(os.path.join(output_dir, name))
    for candidate in candidates:
        if os.path.isfile(candidate) and (system == "windows" or os.access(candidate, os.X_OK)):
            return candidate
    Colors.print("  Test binary not found. Expected executable at:", Colors.RED)
    for candidate in candidates:
        say(f"   - {candidate}")
    return None

def run_test_binary(binary, cwd, launcher=None, args=(), timings=None, timing_name=None, verbose=True):
    # Run a test executable, CAPTURE its output and echo it through our own
    # (flushed) stdout. Inherited-handle child output gets lost or reordered
    # on the Windows CI runners, which made failing tests undiagnosable.
    # launcher: an interpreter to run it with (node for a web test's .js).
    # args: arguments after the binary (the test name for allCoreTests).
    # verbose=False: the output goes to the log file only; the screen gets
    # the FAIL lines with their details, or the tail of a run that failed
    # without one (the caller prints the "FAILED" line with the log path).
    cmd = ([launcher] if launcher else []) + [binary] + list(args)
    t0 = time.monotonic()
    try:
        r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=600)
    except subprocess.TimeoutExpired as e:
        r = None
        timed_out = e
    elapsed = time.monotonic() - t0
    if r is None:
        e = timed_out
        text = e.stdout.decode('utf-8', errors='replace') if e.stdout else ""
        notes = ["  (test timed out after 600s)"]
    else:
        text = r.stdout.decode('utf-8', errors='replace') if r.stdout else ""
        notes = []
    if r is not None and r.returncode != 0:
        notes.append(f"  (exit code {r.returncode} / 0x{r.returncode & 0xFFFFFFFF:08X})")
    ok = r is not None and r.returncode == 0
    log = current_log()
    if log:
        log.command(cmd, cwd)
        log.write(text if text.endswith("\n") or not text else text + "\n")
        log.write("".join(n.strip() + "\n" for n in notes))
    if verbose:
        if text:
            say(text)
        for n in notes:
            say(n)
    else:
        # A run that exited 0 passed, whatever its lines say (some tests print
        # expected failures); only a failed run shows its FAIL lines.
        if not ok:
            _, fail_lines = summarize_test_output(text)
            if fail_lines:
                say_indented(fail_lines)
            else:
                say_indented(text.splitlines()[-TAIL_LINES:])
            say_indented(n.strip() for n in notes)
    if timings is not None:
        timings.append((timing_name, elapsed, ok))
        if verbose:
            say(f"  Wall time: {elapsed:.3f}s ({timing_name})")
    return ok

def run_command(cmd, cwd, verbose=False, progress=None):
    # Run a build step. verbose: echo everything. Otherwise only diagnostics
    # and unknown lines reach the screen, progress goes to `progress`; the
    # full output is in the log file either way.
    log = current_log()
    if log:
        log.command(cmd, cwd)
    tail = deque(maxlen=TAIL_LINES)
    errors_before = len(SHORT.errors)
    is_update = (len(cmd) > 1 and os.path.basename(str(cmd[0])).startswith("trusscli")
                 and cmd[1] == "update")

    def on_line(line):
        if log:
            log.write(line)
        if not verbose:
            tail.append(line.rstrip("\n"))
            SHORT.build_line(line, progress, update=is_update)

    try:
        run_build(cmd, cwd=cwd, check=True, echo=verbose, on_line=on_line)
        return True
    except (subprocess.CalledProcessError, RuntimeError) as e:
        if verbose:
            say(str(e))
            return False
        # Failed without an error line on the screen: bring the hidden tail
        # back, so it is clear where the command stopped.
        if isinstance(e, subprocess.CalledProcessError):
            if len(SHORT.errors) == errors_before:
                say_indented(tail)
            say(f"  (exit code {e.returncode} of {os.path.basename(str(cmd[0]))})")
        else:
            # The warning gate: the warnings themselves were already shown.
            say("  " + str(e).split("\n")[0])
        return False

def run_timed_test_binary(binary, cwd, options, launcher=None, args=()):
    # Only core sweeps collect timings. Measure the process run, excluding
    # configure/build and output printing by the suite around this call.
    verbose = getattr(options, "verbose", True)
    timings = getattr(options, "core_test_timings", None)
    if timings is None:
        return run_test_binary(binary, cwd, launcher=launcher, args=args, verbose=verbose)
    name = os.path.relpath(cwd, ROOT_DIR)
    if launcher:
        name += " (web)"
    return run_test_binary(binary, cwd, launcher=launcher, args=args,
                           timings=timings, timing_name=name, verbose=verbose)

def print_core_test_timings(timings, verbose=True):
    # The short output keeps the table in the log folder instead of the screen.
    sink = None
    if not verbose:
        try:
            path = os.path.join(log_dir(), "core-test-times.log")
            os.makedirs(log_dir(), exist_ok=True)
            sink = open(path, "w", encoding="utf-8", errors="replace")
        except OSError:
            return
    def out(text=""):
        if sink:
            sink.write(text + "\n")
        else:
            say(text)
    out("")
    if sink:
        out("=== Core Test Wall Times (slowest first; runs only) ===")
    else:
        Colors.print("=== Core Test Wall Times (slowest first; runs only) ===", Colors.BLUE)
    if not timings:
        out("No core tests ran.")
    for name, elapsed, ok in sorted(timings, key=lambda t: (-t[1], t[0])):
        out(f"  {elapsed:9.3f}s  {name}" + (" (FAILED)" if not ok else ""))
    if sink:
        sink.close()

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


def build_test_project(test_dir, pg_bin, platform_info, args, progress=None):
    # trusscli update + configure + build a native console project. Returns
    # (binary, stage): the built executable, or None and the stage that failed.
    # progress: dot line fed by the build step (short output only).
    pg_cmd = [str(pg_bin), "update", "-p", test_dir, "--tc-root", ROOT_DIR, "--ide", "cmake"]
    pg_cmd += update_target_flags(pg_bin, web=False)
    if not run_command(pg_cmd, cwd=ROOT_DIR, verbose=args.verbose):
        return None, "update"

    build_dir_name = platform_info["build_dir"]
    cmd_config = cmake_config_cmd(build_dir_name, platform_info)
    if os.path.basename(test_dir) == ALL_CORE_TESTS_NAME:
        if platform_info["os"] == "linux":
            cmd_config.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        # Which combined tests to compile (--shard). Passed on every configure,
        # empty for all of them, so a build dir never keeps an earlier shard's
        # selection.
        select = getattr(args, "core_test_select", None)
        cmd_config.append("-DTC_CORE_TEST_SELECT=" + ";".join(select or []))
    if not run_command(cmd_config, cwd=test_dir, verbose=args.verbose):
        return None, "configure"

    cmd_build = ["cmake", "--build", build_dir_name, "--config", "Release"]
    if "Visual Studio" not in (platform_info["cmake_generator"] or ""):
        import multiprocessing
        cmd_build.extend(["-j", str(multiprocessing.cpu_count())])
    if not run_command(cmd_build, cwd=test_dir, verbose=args.verbose, progress=progress):
        return None, "build"

    binary = find_test_binary(test_dir, platform_info)
    if not binary:
        Colors.print("  Test binary not found after build!", Colors.RED)
        return None, "binary-missing"
    return binary, None

def build_and_run_test(test_dir, pg_bin, platform_info, args):
    # Build a native console test project, then RUN it (non-zero exit = failure).
    # Returns (ok, stage) where stage names what failed for the summary.
    with unit_log(test_dir):
        binary, stage = build_test_project(test_dir, pg_bin, platform_info, args)
        if not binary:
            return False, stage

        if args.verbose:
            Colors.print(f"  Running {os.path.relpath(binary, test_dir)} ...", Colors.YELLOW)
        if not run_timed_test_binary(binary, test_dir, args):
            return False, "run"
        return True, None

def run_combined_core_tests(tests, pg_bin, platform_info, args):
    # Build core/tests/allCoreTests ONCE (every test in `tests` is compiled
    # into it), then run each test as its own process: `allCoreTests <name>`
    # with cwd = that test's own dir, so process-wide state is never shared
    # and the test sees the same cwd as when built alone. Returns 0 if all
    # pass, 1 otherwise (also when the build fails or a test is missing).
    runner_dir = os.path.join(ROOT_DIR, "core", "tests", ALL_CORE_TESTS_NAME)
    runner_name = os.path.relpath(runner_dir, ROOT_DIR)
    by_name = {os.path.basename(t): t for t in tests}
    # local.cmake deliberately compiles daily-only tests too. Validate the
    # FULL inventory before selecting runs, so skipping a daily test cannot
    # hide a missing registration or a stale CMake selection rule.
    expected_names = {os.path.basename(t) for t in find_core_tests(ROOT_DIR, include_daily=True)
                      if is_combined_core_test(t)}
    select = getattr(args, "core_test_select", None)
    if select is not None:   # --shard: this build holds only its share
        expected_names &= set(select)
    verbose = args.verbose
    if verbose:
        Colors.print(f"Found {len(tests)} core test(s) for {ALL_CORE_TESTS_NAME}", Colors.YELLOW)
        print("")

    failed = []
    if verbose:
        Colors.print(f"Building: {runner_name}", Colors.YELLOW)
        build_progress = None
    else:
        # The label shows with the first dot, after trusscli's own lines.
        build_progress = Progress(f"Building {runner_name}")
    t0 = time.monotonic()
    with unit_log(runner_dir):
        binary, stage = build_test_project(runner_dir, pg_bin, platform_info, args,
                                           progress=build_progress)
        if binary and platform_info["os"] == "linux":
            cmd = [sys.executable, os.path.join(ROOT_DIR, "tools", "check_core_test_symbols.py"),
                   os.path.join(runner_dir, platform_info["build_dir"])]
            if not run_command(cmd, cwd=ROOT_DIR, verbose=verbose):
                stage = "duplicate-symbols"
        build_secs = time.monotonic() - t0
        names = []
        if binary and not stage:
            if verbose:
                Colors.print(f"  Built in {build_secs:.1f}s", Colors.GREEN)
            else:
                close_dots()
            try:
                r = subprocess.run([binary, "--list"], cwd=runner_dir, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=60)
                out = r.stdout.decode("utf-8", errors="replace")
                log = current_log()
                if log:
                    log.command([binary, "--list"], runner_dir)
                    log.write(out)
                if r.returncode != 0:
                    say(out)
                    stage = "list"
                else:
                    names = [n.strip() for n in out.splitlines() if n.strip()]
            except (OSError, subprocess.SubprocessError) as e:
                say(f"  {e}")
                stage = "list"
            if not stage and not names:
                Colors.print("  --list returned no tests", Colors.RED)
                stage = "list-empty"
    close_dots()
    if stage:
        if verbose:
            Colors.print(f"  FAILED ({stage})", Colors.RED)
        else:
            failed_line(runner_name, stage)
        failed.append(f"{runner_name} ({stage})")

    if stage:
        failed.append(f"({len(tests)} core tests not run)")
    else:
        # Every combined test dir must be registered, and nothing else.
        for n in sorted(expected_names - set(names)):
            failed.append(f"core/tests/{n} (not registered in {ALL_CORE_TESTS_NAME})")
            if not verbose:
                failed_line(f"core/tests/{n}", f"not registered in {ALL_CORE_TESTS_NAME}", log=False)
        for n in sorted(set(names) - expected_names):
            failed.append(f"core/tests/{n} (registered, but no such combined test dir)")
            if not verbose:
                failed_line(f"core/tests/{n}", "registered, but no such combined test dir", log=False)

    run_names = [n for n in names if n in by_name] if not stage else []
    passed = 0
    run_progress = Progress(f"Running {len(run_names)} core tests")
    if run_names and not verbose:
        run_progress.begin()
    t0 = time.monotonic()
    for i, n in enumerate(run_names):
        tdir = by_name[n]
        if verbose:
            Colors.print(f"[{i+1}/{len(run_names)}] Running: {os.path.relpath(tdir, ROOT_DIR)} "
                         f"({ALL_CORE_TESTS_NAME} {n})", Colors.YELLOW)
        with unit_log(tdir):
            ok = run_timed_test_binary(binary, tdir, args, args=[n])
        if ok:
            if verbose:
                Colors.print("  Passed!", Colors.GREEN)
            passed += 1
        else:
            if verbose:
                Colors.print("  FAILED (run)", Colors.RED)
            else:
                failed_line(os.path.relpath(tdir, ROOT_DIR), "run")
            failed.append(f"{os.path.relpath(tdir, ROOT_DIR)} (run)")
        if not verbose:
            run_progress.update((i + 1) / len(run_names))
    run_secs = time.monotonic() - t0
    close_dots()

    if not verbose:
        SHORT.groups.append((ALL_CORE_TESTS_NAME, passed, len(tests)))
        say(f"{ALL_CORE_TESTS_NAME}: {passed}/{len(tests)} passed "
            f"(build {build_secs:.1f}s, runs {run_secs:.1f}s)")
        return 1 if failed else 0

    print("")
    Colors.print("=== Core Test Summary ===", Colors.BLUE)
    print(f"{ALL_CORE_TESTS_NAME} build: {build_secs:.1f}s, runs: {run_secs:.1f}s")
    print(f"Total:  {len(tests)}")
    Colors.print(f"Passed: {passed}", Colors.GREEN)
    if failed:
        Colors.print(f"Failed: {len(failed)}", Colors.RED)
        for f in failed:
            print(f"  - {f}")
        return 1
    Colors.print("All core tests passed!", Colors.GREEN)
    return 0

def build_and_run_unit_test(test_dir, pg_bin, platform_info, args):
    # Build a standalone CMake test (its own committed CMakeLists.txt, no
    # trusscli), then RUN it (non-zero exit = failure). pg_bin is unused.
    with unit_log(test_dir):
        return _build_and_run_unit_test(test_dir, platform_info, args)

def _build_and_run_unit_test(test_dir, platform_info, args):
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

    if args.verbose:
        Colors.print(f"  Running {os.path.relpath(binary, test_dir)} ...", Colors.YELLOW)
    if not run_timed_test_binary(binary, test_dir, args):
        return False, "run"
    return True, None

def find_node():
    # emsdk_env exports EMSDK_NODE (the node emsdk ships and emcc targets);
    # fall back to whatever node is on PATH.
    node = os.environ.get("EMSDK_NODE")
    if node and os.path.isfile(node):
        return node
    return shutil.which("node")

def build_and_run_web_test(test_dir, pg_bin, platform_info, args):
    # Build a core test for WebAssembly, then run its JavaScript under node
    # (non-zero exit = failure). Project tests use trusscli's Web preset like Web
    # examples; standalone tests use their own CMake project. Neither needs
    # a canvas or GPU, and the project test's .html shell is unused.
    with unit_log(test_dir, ".web"):
        return _build_and_run_web_test(test_dir, pg_bin, platform_info, args)

def _build_and_run_web_test(test_dir, pg_bin, platform_info, args):
    node = find_node()
    if not node:
        Colors.print("  node not found (source emsdk_env or put node on PATH)", Colors.RED)
        return False, "node-missing"

    standalone = not os.path.isdir(os.path.join(test_dir, "src"))
    if not standalone:
        pg_cmd = [str(pg_bin), "update", "-p", test_dir, "--tc-root", ROOT_DIR, "--ide", "cmake"]
        pg_cmd += update_target_flags(pg_bin, web=True)
        if not run_command(pg_cmd, cwd=ROOT_DIR, verbose=args.verbose):
            return False, "update"

    if not standalone:
        cmd_config = ["cmake", "--preset", "web"]
    elif shutil.which("emcmake"):
        cmd_config = ["emcmake", "cmake", "-S", ".", "-B", "build-web"]
    else:
        cmd_config = ["cmake", "-S", ".", "-B", "build-web"]
    if not run_command(cmd_config, cwd=test_dir, verbose=args.verbose):
        return False, "web-configure"

    import multiprocessing
    cmd_build = ["cmake", "--build", "build-web", "-j", str(multiprocessing.cpu_count())]
    if not run_command(cmd_build, cwd=test_dir, verbose=args.verbose):
        return False, "web-build"

    base = os.path.basename(os.path.normpath(test_dir))
    js = os.path.join(test_dir, "build-web" if standalone else "bin", base + ".js")
    if not os.path.isfile(js):
        Colors.print(f"  Web test output not found: {os.path.relpath(js, test_dir)}", Colors.RED)
        return False, "binary-missing"

    if args.verbose:
        Colors.print(f"  Running node {os.path.relpath(js, test_dir)} ...", Colors.YELLOW)
    if not run_timed_test_binary(js, test_dir, args, launcher=node):
        return False, "web-run"
    return True, None

def run_test_suite(tests, label, pg_bin, platform_info, args, builder=build_and_run_test):
    # Build AND run a set of console test projects (addon or core). Streams each
    # test's output; returns 0 if all pass, 1 if any fails.
    # Short output: one dot line for the set, FAIL details, and a count line.
    verbose = args.verbose
    if verbose:
        Colors.print(f"Found {len(tests)} {label} test harness(es)", Colors.YELLOW)
        print("")
    else:
        progress = Progress(f"{label.capitalize()} tests ({len(tests)})")
        progress.begin()
    failed = []
    for i, tdir in enumerate(tests):
        name = os.path.relpath(tdir, ROOT_DIR)
        if verbose:
            Colors.print(f"[{i+1}/{len(tests)}] Building & running: {name}", Colors.YELLOW)
        ok, stage = builder(tdir, pg_bin, platform_info, args)
        if ok:
            if verbose:
                Colors.print("  Passed!", Colors.GREEN)
        else:
            if verbose:
                Colors.print(f"  FAILED ({stage})", Colors.RED)
            else:
                failed_line(name, stage)
            failed.append(f"{name} ({stage})")
        if not verbose:
            progress.update((i + 1) / len(tests))
    if not verbose:
        close_dots()
        SHORT.groups.append((f"{label} tests", len(tests) - len(failed), len(tests)))
        say(f"{label.capitalize()} tests: {len(tests) - len(failed)}/{len(tests)} passed")
        return 1 if failed else 0
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

def build_example(example_dir, example_name, pg_bin, platform_info, args):
    # trusscli update + native and/or web configure + build of one example.
    # Returns the stage that failed, or None.
    # Regenerate build files for each example before building
    pg_cmd = [str(pg_bin), "update", "-p", example_dir, "--tc-root", ROOT_DIR, "--ide", "cmake"]
    pg_cmd += update_target_flags(pg_bin, web=args.web)

    if not run_command(pg_cmd, cwd=ROOT_DIR, verbose=args.verbose):
        if args.verbose:
            Colors.print(f"  Project update failed!", Colors.RED)
        return "update"

    if not args.web_only:
        build_dir_name = platform_info["build_dir"]
        build_dir = os.path.join(example_dir, build_dir_name)

        if args.clean and os.path.exists(build_dir):
            shutil.rmtree(build_dir)

        cmd_config = cmake_config_cmd(build_dir_name, platform_info)

        if not run_command(cmd_config, cwd=example_dir, verbose=args.verbose):
            if args.verbose:
                Colors.print(f"  Native Configure failed!", Colors.RED)
            return "native-conf"

        cmd_build = ["cmake", "--build", build_dir_name, "--config", "Release"]

        if "Visual Studio" not in (platform_info["cmake_generator"] or ""):
            import multiprocessing
            jobs = str(multiprocessing.cpu_count())
            cmd_build.extend(["-j", jobs])

        if not run_command(cmd_build, cwd=example_dir, verbose=args.verbose):
            if args.verbose:
                Colors.print(f"  Native Build failed!", Colors.RED)
            return "native-build"

    if args.web:
        build_dir_web = os.path.join(example_dir, "build-web")
        if args.clean and os.path.exists(build_dir_web):
            shutil.rmtree(build_dir_web)

        # The generated preset carries the host generator and toolchain.
        # emcmake may add its own -G on Windows, overriding that choice.
        cmd_config_web = ["cmake", "--preset", "web"]

        if not run_command(cmd_config_web, cwd=example_dir, verbose=args.verbose):
            if args.verbose:
                Colors.print(f"  Web Configure failed!", Colors.RED)
            return "web-conf"

        cmd_build_web = ["cmake", "--build", "build-web"]
        import multiprocessing
        jobs = str(multiprocessing.cpu_count())
        cmd_build_web.extend(["-j", jobs])

        if not run_command(cmd_build_web, cwd=example_dir, verbose=args.verbose):
            if args.verbose:
                Colors.print(f"  Web Build failed!", Colors.RED)
            return "web-build"

    return None

# =============================================================================
# Main
# =============================================================================

def main():
    configure_output()
    parser = argparse.ArgumentParser(description="TrussC Batch Build Script")
    parser.add_argument('--clean', action='store_true', help="Clean build directories before building")
    parser.add_argument('--web', action='store_true', help="Also build for WebAssembly")
    parser.add_argument('--web-only', action='store_true', help="Build for WebAssembly only (skip native build)")
    parser.add_argument('--test-only', action='store_true', help="Build ONLY AllFeaturesExample for quick CI check")
    parser.add_argument('--one-per-addon', action='store_true', help="Build the first example-* of each bundled addon (per-addon dependency compile coverage)")
    parser.add_argument('--addon-tests-only', action='store_true', help="Build AND RUN every addons/*/tests/ harness (console, non-zero exit fails). No-op if none exist. Harnesses with a daily-only marker are skipped unless --include-daily is given.")
    parser.add_argument('--addons', nargs='+', help="With --addon-tests-only: select addon names instead of the full suite")
    parser.add_argument('--include-daily', action='store_true', help="With --addon-tests-only or --core-tests-only: also run harnesses marked daily-only (the daily workflow passes this)")
    parser.add_argument('--core-tests-only', action='store_true', help="Build AND RUN every core/tests/*/ harness (console, non-zero exit fails). Daily-only harnesses are skipped unless --include-daily is given. No-op if none exist. With --web also, with --web-only instead: build the ones with a web-test marker for WebAssembly and run them under node.")
    parser.add_argument('--shard', metavar='N/M', default='1/1', help="With --core-tests-only: run shard N of M (CI splits the core tests over M jobs). Shard 1 builds and runs the own-binary and unit tests; the combined tests, sorted by name, are dealt round-robin over shards 2..M (all in shard 1 when M is 1). Default 1/1: everything.")
    parser.add_argument('--verbose', action='store_true', help="Show the full output of every build and test (CI uses this). The default prints a dot line, diagnostics, FAIL lines and a summary; the full output is in build-logs/ either way.")
    args = parser.parse_args()
    global VERBOSE
    VERBOSE = args.verbose
    reset_short_output()

    if args.addons is not None:
        if not args.addon_tests_only:
            parser.error('--addons requires --addon-tests-only')
        addons_dir = os.path.join(ROOT_DIR, 'addons')
        available = {name for name in os.listdir(addons_dir)
                     if os.path.isdir(os.path.join(addons_dir, name))}
        unknown = set(args.addons) - available
        if unknown:
            parser.error(f"Unknown addons: {', '.join(sorted(unknown))}")

    m = re.fullmatch(r'(\d+)/(\d+)', args.shard)
    if not m or not 1 <= int(m.group(1)) <= int(m.group(2)):
        parser.error(f"--shard expects N/M with 1 <= N <= M, got {args.shard!r}")
    args.shard_index, args.shard_count = int(m.group(1)), int(m.group(2))

    if args.web_only:
        args.web = True

    platform_info = get_platform_info()
    pg_bin = find_project_generator(ROOT_DIR, platform_info)

    def finish(rc):
        # Short output: the last lines are the result, then the diagnostics.
        if not args.verbose:
            close_dots()
            skipped = len(SKIPPED_DAILY)
            if skipped:
                say(f"Skipped {skipped} daily-only harness(es); --include-daily runs them.")
            for line in SHORT.summary(rc):
                say(line)
        sys.exit(rc)

    if args.verbose:
        Colors.print("=== TrussC Examples Batch Build (Python) ===", Colors.BLUE)
        Colors.print(f"Platform: {platform_info['os']}", Colors.YELLOW)
        Colors.print(f"ProjectGenerator: {pg_bin}", Colors.YELLOW)
    modes = []
    if args.web_only:
        modes.append("Web Only")
    if args.test_only:
        modes.append("AllFeaturesExample Only")
    if args.one_per_addon:
        modes.append("One example per addon")
    if args.addon_tests_only:
        modes.append("Addon tests (build + run)")
    if args.core_tests_only:
        where = "web, under node" if args.web_only else ("native + web" if args.web else "native")
        modes.append(f"Core tests (build + run, {where})")
    if args.verbose:
        for mode in modes:
            Colors.print(f"Mode: {mode}", Colors.YELLOW)
        print("")
    else:
        say(f"build_all ({platform_info['os']}): " + (", ".join(modes) or "all examples")
            + ". Full output: " + display_path(log_dir()) + "/ (--verbose prints it here)")

    # Addon test harnesses (addons/*/tests/): build AND run each. A cheap,
    # behavioral gate meant to run on every CI, separate from the example
    # builds. No-op (exit 0) when no addon ships a tests/ dir.
    if args.addon_tests_only:
        tests = find_addon_tests(ROOT_DIR, include_daily=args.include_daily, addons=args.addons)
        if not tests:
            Colors.print("No addon tests found (addons/*/tests/); nothing to do.", Colors.YELLOW)
            sys.exit(0)
        finish(run_test_suite(tests, "addon", pg_bin, platform_info, args))

    # Core behavioral tests (core/tests/*/): same build+run gate, owned by core.
    # Two flavours: trusscli project tests (src/, link libTrussC; most are
    # built into the one app core/tests/allCoreTests, see
    # run_combined_core_tests) and standalone CMake unit tests (committed
    # CMakeLists.txt, no libTrussC link — e.g. sokol dummy-backend tests).
    # Both run; the job fails if either has a failure.
    # With --web / --web-only, the project tests carrying a `web-test` marker
    # are also / instead built for WebAssembly and run under node.
    if args.core_tests_only:
        native = not args.web_only
        tests = find_core_tests(ROOT_DIR, include_daily=args.include_daily) if native else []
        unit_tests = find_core_unit_tests(ROOT_DIR, include_daily=args.include_daily) if native else []
        web_tests = find_core_web_tests(ROOT_DIR, include_daily=args.include_daily) if args.web else []
        all_combined = [t for t in find_core_tests(ROOT_DIR, include_daily=True)
                        if is_combined_core_test(t)] if native else []
        if not all_combined and not tests and not unit_tests and not web_tests:
            Colors.print("No core tests found (core/tests/*/); nothing to do.", Colors.YELLOW)
            sys.exit(0)
        rc = 0
        args.core_test_timings = []
        # Most project tests are built into ONE app (core/tests/allCoreTests)
        # and run one process each; those with an `own-binary` marker are
        # built and run alone, as before.
        combined = [t for t in tests if is_combined_core_test(t)]
        own = [t for t in tests if not is_combined_core_test(t)]
        # Other projects (including the shaderScopeDummy unit harness) can
        # relink libTrussC; build the reload host last to keep it current.
        own.sort(key=lambda t: os.path.basename(t) == "hotReloadLifecycle")
        args.core_test_select = None
        if args.shard_count > 1:
            # Deterministic: the same names always land in the same shard.
            shard, shards = args.shard_index, args.shard_count
            names = sorted(os.path.basename(t) for t in all_combined)
            selected = [] if shard == 1 else \
                [n for i, n in enumerate(names) if 2 + i % (shards - 1) == shard]
            args.core_test_select = selected
            all_combined = [t for t in all_combined if os.path.basename(t) in selected]
            combined = [t for t in combined if os.path.basename(t) in selected]
            if shard != 1:
                own, unit_tests, web_tests = [], [], []
            Colors.print(f"Shard {shard}/{shards}: {len(all_combined)} combined, "
                         f"{len(own)} own-binary, {len(unit_tests)} unit test(s)", Colors.YELLOW)
        if all_combined:
            rc |= run_combined_core_tests(combined, pg_bin, platform_info, args)
        if unit_tests:
            rc |= run_test_suite(unit_tests, "core unit", pg_bin, platform_info, args,
                                 builder=build_and_run_unit_test)
        if own:
            rc |= run_test_suite(own, "core own-binary", pg_bin, platform_info, args)
        if web_tests:
            rc |= run_test_suite(web_tests, "core web", pg_bin, platform_info, args,
                                 builder=build_and_run_web_test)
        print_core_test_timings(args.core_test_timings, verbose=args.verbose)
        finish(rc)

    if args.test_only:
        test_example = os.path.join(ROOT_DIR, "examples", "tests", "AllFeaturesExample")
        if os.path.exists(test_example):
            example_dirs = [test_example]
        else:
            Colors.print(f"AllFeaturesExample not found at: {test_example}", Colors.RED)
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
    if args.verbose:
        print(f"Found {total} examples")
        print("")

    if args.clean:
        trussc_dir = os.path.join(ROOT_DIR, "core")
        if args.verbose:
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
    progress = None
    if not args.verbose:
        progress = Progress(f"Building {total} examples")
        progress.begin()

    for i, example_dir in enumerate(example_dirs):
        try:
            example_name = os.path.relpath(example_dir, ROOT_DIR)
        except ValueError:
            example_name = example_dir

        if args.verbose:
            Colors.print(f"[{i+1}/{total}] Updating & Building: {example_name}", Colors.YELLOW)

        with unit_log(example_dir, ".web" if args.web_only else ""):
            stage = build_example(example_dir, example_name, pg_bin, platform_info, args)
        if stage:
            failed_count += 1
            failed_list.append(f"{example_name} ({stage})")
            if not args.verbose:
                failed_line(example_name, stage)
        else:
            if args.verbose:
                Colors.print("  Success!", Colors.GREEN)
            success_count += 1
        if progress:
            progress.update((i + 1) / total)

    if not args.verbose:
        close_dots()
        SHORT.groups.append(("examples", success_count, total))
        say(f"Examples: {success_count}/{total} built")
        finish(1 if failed_count else 0)

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
