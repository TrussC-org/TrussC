"""Offline, concurrent tests of the production sokol-shdc installer (#676)."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "core/cmake/tc_acquire_shdc.cmake"


class ShdcDownloadTests(unittest.TestCase):
    def run_fixture(self, stale=False, bad_hash=False, rename_failure="", mutation="",
                    slow_holder=False):
        with tempfile.TemporaryDirectory(prefix="tc shdc ", dir=ROOT) as temporary:
            base = Path(temporary)
            final = base / "sokol-shdc"
            sentinel = base / "sokol-shdc.someone-else.download"
            sentinel.write_bytes(b"another configure owns this")
            if stale:
                final.write_bytes(b"old compiler")
            source = HELPER.read_text(encoding="utf-8")
            if mutation == "no_publish":
                start = source.index('    execute_process(COMMAND "${CMAKE_COMMAND}" -E rename')
                end = source.index('    file(REMOVE', start)
                source = source[:start] + '    set(_TC_RENAME_RESULT 0)\n' + source[end:]
            if mutation == "no_recheck":
                start = source.index('    # Recheck under the lock:')
                end = source.index('    string(RANDOM', start)
                source = source[:start] + source[end:]
            helper = base / "helper.cmake"
            helper.write_text(source, encoding="utf-8")
            # Pin a local fake compiler so this test needs no preinstalled tool.
            binary = base / "source compiler"
            binary.write_bytes(b"offline shader compiler fixture\n")
            digest = hashlib.sha256(binary.read_bytes()).hexdigest()
            script = base / "run.cmake"
            script.write_text(r'''
cmake_minimum_required(VERSION 3.20)
# Observe production file operations and force overlap using lock-attempt
# markers. No assertion depends on how long a process waited.
macro(file operation)
    if("${operation}" STREQUAL "LOCK")
        _file(WRITE "${BASE}/attempt-${WORKER}" "")
    elseif("${operation}" STREQUAL "DOWNLOAD")
        if(STALE)
            _file(READ "${FINAL}" previous)
            if(NOT previous STREQUAL "old compiler")
                message(FATAL_ERROR "Old compiler changed before verification")
            endif()
        elseif(EXISTS "${FINAL}")
            message(FATAL_ERROR "Partial compiler published")
        endif()
        if(NOT "${ARGV2}" MATCHES "/sokol-shdc[.][0-9a-f]+[.]download$")
            message(FATAL_ERROR "Download must use an owned unique temporary file")
        endif()
        _file(APPEND "${BASE}/downloads" "${ARGV2}\n")
        execute_process(COMMAND "${PYTHON}" -c
            "import pathlib,sys,time; p=pathlib.Path(sys.argv[1]); deadline=time.monotonic()+20; exec('while len(list(p.glob(\"attempt-*\"))) < int(sys.argv[2]):\n if time.monotonic()>deadline: raise SystemExit(1)\n time.sleep(0.01)')"
            "${BASE}" "${WORKERS}" RESULT_VARIABLE barrier)
        if(NOT barrier EQUAL 0)
            message(FATAL_ERROR "Other configure did not reach the lock")
        endif()
        if(SLOW_HOLDER)
            # Pause the actual file:// download with a waiter already at LOCK.
            # Exceed the former 120-second deadline without altering the helper.
            execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 130)
        endif()
    endif()
    _file(${ARGV})
endmacro()

function(execute_process)
    if("${ARGV3}" STREQUAL "rename" AND RENAME_FAILURE)
        if(RENAME_FAILURE STREQUAL "winner")
            # Simulate a successful competing installer followed by our rename
            # failure (e.g. an executable in use on Windows).
            _execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${ARGV4}" "${ARGV5}")
        endif()
        set(_TC_RENAME_RESULT 1 PARENT_SCOPE)
        set(_TC_RENAME_ERROR "injected rename failure" PARENT_SCOPE)
    else()
        _execute_process(${ARGV})
        if("${ARGV3}" STREQUAL "rename")
            set(_TC_RENAME_RESULT "${_TC_RENAME_RESULT}" PARENT_SCOPE)
            set(_TC_RENAME_ERROR "${_TC_RENAME_ERROR}" PARENT_SCOPE)
        else()
            set(barrier "${barrier}" PARENT_SCOPE)
        endif()
    endif()
endfunction()
include("${HELPER}")
trussc_acquire_shdc("${FINAL}" "${URL}" "${HASH}" linux fixture-pin)
''', encoding="utf-8")
            count = 1 if bad_hash or rename_failure else (2 if slow_holder else 3)
            processes = []
            try:
                for worker in range(count):
                    command = [
                        "cmake", f"-DBASE={base}", f"-DFINAL={final}",
                        f"-DHELPER={helper}", f"-DURL={binary.as_uri()}",
                        f"-DHASH={'0' * 64 if bad_hash else digest}",
                        f"-DSTALE={'ON' if stale else 'OFF'}",
                        f"-DRENAME_FAILURE={rename_failure}",
                        f"-DWORKER={worker}", f"-DWORKERS={count}",
                        f"-DSLOW_HOLDER={'ON' if slow_holder else 'OFF'}",
                        f"-DPYTHON={sys.executable}", "-P", str(script),
                    ]
                    processes.append(subprocess.Popen(
                        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, encoding="utf-8", errors="replace"))
                for process in processes:
                    output, _ = process.communicate(timeout=180 if slow_holder else 45)
                    expected_failure = bad_hash or rename_failure == "blocked"
                    self.assertEqual(process.returncode == 0, not expected_failure, output)
                if bad_hash or rename_failure == "blocked":
                    self.assertEqual(final.read_bytes(), b"old compiler")
                else:
                    self.assertTrue(final.exists(), "Verified compiler was not published")
                    self.assertEqual(hashlib.sha256(final.read_bytes()).hexdigest(), digest)
                downloads = (base / "downloads").read_text(encoding="utf-8").splitlines()
                self.assertEqual(len(downloads), 1, downloads)
                self.assertEqual(list(base.glob("*.download")), [sentinel])
                self.assertEqual(sentinel.read_bytes(), b"another configure owns this")
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.kill()
                    process.communicate()

    def test_parallel_fresh_install(self):
        self.run_fixture()

    def test_parallel_stale_install(self):
        self.run_fixture(stale=True)

    def test_waiter_outlasts_former_lock_deadline(self):
        self.run_fixture(slow_holder=True)

    @unittest.skipUnless(os.name == "posix" and os.geteuid() != 0,
                         "requires enforced POSIX directory permissions")
    def test_valid_read_only_cache_configures_without_lock_access(self):
        for existing_lock in (False, True):
            with self.subTest(existing_lock=existing_lock), tempfile.TemporaryDirectory(
                    prefix="tc shdc readonly ", dir=ROOT) as temporary:
                base = Path(temporary)
                cache = base / "cache"
                cache.mkdir()
                final = cache / "sokol-shdc"
                final.write_bytes(b"cached shader compiler fixture\n")
                final.chmod(0o555)
                before = final.stat()
                digest = hashlib.sha256(final.read_bytes()).hexdigest()
                lock = cache / "sokol-shdc.lock"
                if existing_lock:
                    lock.write_bytes(b"read-only lock sentinel")
                    lock.chmod(0o444)
                    lock_before = lock.stat()
                (base / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(ReadOnlyShdcCache NONE)
include("{HELPER.as_posix()}")
trussc_acquire_shdc("{final.as_posix()}" "{(base / 'missing').as_uri()}"
    "{digest}" linux fixture-pin)
''', encoding="utf-8")
                cache.chmod(0o555)
                try:
                    result = subprocess.run(
                        ["cmake", "-S", str(base), "-B", str(base / "build")],
                        capture_output=True, text=True, encoding="utf-8",
                        errors="replace", timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(final.stat().st_mtime_ns, before.st_mtime_ns)
                    self.assertEqual(final.stat().st_mode, before.st_mode)
                    self.assertEqual(hashlib.sha256(final.read_bytes()).hexdigest(), digest)
                    self.assertEqual(lock.exists(), existing_lock)
                    if existing_lock:
                        self.assertEqual(lock.read_bytes(), b"read-only lock sentinel")
                        self.assertEqual(lock.stat().st_mtime_ns, lock_before.st_mtime_ns)
                        self.assertEqual(lock.stat().st_mode, lock_before.st_mode)
                finally:
                    cache.chmod(0o755)

    def test_bad_hash_preserves_stale_binary_and_cleans_temp(self):
        self.run_fixture(stale=True, bad_hash=True)

    def test_failed_rename_accepts_verified_winner(self):
        self.run_fixture(rename_failure="winner")

    def test_failed_rename_preserves_stale_binary(self):
        self.run_fixture(stale=True, rename_failure="blocked")

    def test_mutations_are_detected(self):
        for mutation in ("no_publish", "no_recheck"):
            with self.subTest(mutation=mutation), self.assertRaises(AssertionError):
                self.run_fixture(mutation=mutation)


if __name__ == "__main__":
    unittest.main()
