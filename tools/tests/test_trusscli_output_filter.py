"""Short build output (#1240): the filter behind `trusscli build` / `run` / `update`.

tools/src/BuildOutputFilter is compiled with a small driver
(build_output_filter_driver.cpp) that feeds stdin through the filter and prints
the screen text. Fixtures are in tools/tests/fixtures/build_output/. No device,
cmake or trusscli binary is needed; run through the build guard:

    tcbuild.sh python3 -m unittest tools/tests/test_trusscli_output_filter.py
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent
FIXTURES = TESTS / "fixtures" / "build_output"
LOG = "build-linux/build.log"


def fixture(name):
    return (FIXTURES / name).read_text(encoding="utf-8")


class FilterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.mkdtemp(prefix="build-output-filter-")
        cls.driver = Path(cls.scratch) / "driver"
        compiler = os.environ.get("CXX", "c++")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "tools/src"),
                        str(TESTS / "build_output_filter_driver.cpp"),
                        str(ROOT / "tools/src/BuildOutputFilter.cpp"),
                        "-o", str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.scratch, ignore_errors=True)

    def run_filter(self, text, *args, chunk=0):
        log = Path(self.scratch) / "build.log"
        if log.exists():
            log.unlink()
        extra = ["--chunk", str(chunk)] if chunk else []
        result = subprocess.run(
            [str(self.driver), "--log-display", LOG, "--log-file", str(log), *extra, *args],
            input=text.encode("utf-8"), capture_output=True, check=True)
        return result.stdout.decode("utf-8"), log.read_bytes().decode("utf-8")

    def screen(self, text, *args, **kwargs):
        return self.run_filter(text, *args, **kwargs)[0]

    # --- progress ----------------------------------------------------------

    def test_make_progress_is_a_dot_per_5_percent(self):
        # Real Make output of a clean build, including the shader step lines
        # and the out-of-order percentages between them.
        out, _ = self.run_filter(fixture("make_build.txt"), "--total", "44", "--seconds", "23")
        self.assertEqual(out,
            "Building myApp (44 files) .........50%.........100%\n"
            "Done: 44 files (0m23s). Full log: build-linux/build.log\n")

    def test_ninja_progress_is_a_dot_per_5_percent(self):
        out = self.screen(fixture("ninja_build.txt"), "--total", "44", "--seconds", "5")
        self.assertEqual(out,
            "Building myApp (44 files) .........50%.........100%\n"
            "Done: 43 files, 1 up to date (0m05s). Full log: build-linux/build.log\n")

    def test_dots_are_cumulative_across_chunks(self):
        whole = self.screen(fixture("make_build.txt"), "--total", "44")
        for chunk in (1, 7, 64):
            with self.subTest(chunk=chunk):
                self.assertEqual(self.screen(fixture("make_build.txt"), "--total", "44",
                                             chunk=chunk), whole)

    def test_unknown_format_gets_a_dot_per_compile_step(self):
        text = ("CompileC /b/a.o /s/a.cpp normal arm64 c++ com.apple.compilers.llvm.clang.1_0.compiler\n"
                "CompileC /b/b.o /s/b.cpp normal arm64 c++ com.apple.compilers.llvm.clang.1_0.compiler\n"
                "CompileC /b/c.o /s/c.cpp normal arm64 c++ com.apple.compilers.llvm.clang.1_0.compiler\n")
        out = self.screen(text, "--total", "10")
        self.assertEqual(out,
            "Building myApp (10 files) ...\n"
            "Done: 3 files, 7 up to date (0m00s). Full log: build-linux/build.log\n")

    def test_nothing_parseable_gives_header_and_summary_only(self):
        out = self.screen("", "--total", "44")
        self.assertEqual(out,
            "Building myApp (44 files) \n"
            "Done: all 44 up to date (0m00s). Full log: build-linux/build.log\n")
        out = self.screen("some tool\nanother line\n")
        self.assertEqual(out,
            "Building myApp \nsome tool\nanother line\n"
            "Done: nothing to compile (0m00s). Full log: build-linux/build.log\n")

    # --- hidden and passed-through lines ----------------------------------

    def test_only_known_noise_is_hidden(self):
        text = ("-- Configuring done\n"
                "Scanning dependencies of target foo\n"
                "[ 10%] Built target foo\n"
                "Built target bar\n"
                "[ 20%] Building CXX object x.o\n"
                "[ 30%] Linking CXX static library libfoo.a\n"
                "[ 40%] [TrussC] Compiling shader: a.glsl\n"
                "[ 50%] Generating something\n"
                "note: an unexpected note\n"
                "gmake[2]: *** [x] Error 1\n"
                "In file included from a.cpp:1:\n")
        out = self.screen(text, "--total", "44")
        for hidden in ("-- Configuring", "Scanning", "Built target", "Building CXX",
                       "Linking CXX", "Compiling shader"):
            self.assertNotIn(hidden, out)
        for shown in ("[ 50%] Generating something", "note: an unexpected note",
                      "gmake[2]: *** [x] Error 1", "In file included from a.cpp:1:"):
            self.assertIn(shown + "\n", out)

    def test_diagnostic_mid_dots_starts_a_new_line_and_dots_continue(self):
        out = self.screen(fixture("gcc_diagnostics.txt"), "--total", "44",
                          "--exit", "2", "--seconds", "12")
        lines = out.split("\n")
        # The dot line is closed before the first passed-through line ...
        self.assertEqual(lines[0], "Building myApp (44 files) ....")
        self.assertEqual(lines[1], "In file included from /src/app/src/tcApp.cpp:1:")
        # ... and the next dots start on a fresh line, again closed by a diagnostic.
        self.assertIn("\n..\n/src/app/src/scene.cpp:7:10: fatal error: missing.h", out)
        # No line other than the dot lines contains dots-only text glued to a diagnostic
        for line in lines:
            if "error:" in line or "warning:" in line:
                self.assertTrue(line.startswith("/src/"), line)

    def test_gcc_summary_counts_and_names(self):
        out = self.screen(fixture("gcc_diagnostics.txt"), "--total", "44",
                          "--exit", "2", "--seconds", "12")
        self.assertTrue(out.endswith(
            "Failed: stopped after 3 files (0m12s). Full log: build-linux/build.log\n"
            "ERROR: 2 (scene.cpp)\n"
            "WARNING: 3 (tcApp.cpp, scene.h)\n"), out)

    def test_successful_build_with_warnings(self):
        out = self.screen(fixture("gcc_diagnostics.txt").split("[ 30%]")[0],
                          "--total", "44", "--seconds", "30")
        self.assertTrue(out.endswith(
            "Done: 2 files, 42 up to date (0m30s). Full log: build-linux/build.log\n"
            "WARNING: 3 (tcApp.cpp, scene.h)\n"), out)
        self.assertNotIn("ERROR", out)

    def test_names_are_limited_to_three_with_more_count(self):
        text = "".join("/s/f%d.cpp:1:1: warning: w%d\n" % (i, i) for i in range(5))
        text += "".join("/s/e%d.cpp:1:1: error: e\n" % i for i in range(4))
        out = self.screen(text, "--exit", "1")
        self.assertIn("ERROR: 4 (e0.cpp, e1.cpp, e2.cpp, +1 more)\n", out)
        self.assertIn("WARNING: 5 (f0.cpp, f1.cpp, f2.cpp, +2 more)\n", out)

    def test_same_file_is_listed_once_but_counted_each_time(self):
        text = "".join("/s/a.cpp:%d:1: warning: w\n" % i for i in range(1, 8))
        out = self.screen(text)
        self.assertIn("WARNING: 7 (a.cpp)\n", out)

    def test_note_lines_are_not_counted(self):
        out = self.screen("/s/a.cpp:3:1: note: declared here\n/s/a.cpp:3:1: required from here\n")
        self.assertNotIn("WARNING", out)
        self.assertNotIn("ERROR", out)

    def test_ansi_colored_diagnostics_are_counted_and_shown_unchanged(self):
        line = "\x1b[01m\x1b[K/s/a.cpp:3:1:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kbad\n"
        out = self.screen(line, "--exit", "1")
        self.assertIn(line, out)
        self.assertIn("ERROR: 1 (a.cpp)\n", out)

    # --- MSVC and linker ---------------------------------------------------

    def test_msvc_formats(self):
        out = self.screen(fixture("msvc_diagnostics.txt"), "--total", "6",
                          "--exit", "1", "--seconds", "3")
        self.assertTrue(out.endswith(
            "Failed: stopped after 3 files (0m03s). Full log: build-linux/build.log\n"
            "ERROR: 2 (scene.cpp)\n"
            "WARNING: 3 (tcApp.cpp, scene.h)\n"), out)
        # Everything else still reaches the screen
        self.assertIn("FAILED: CMakeFiles/app.dir/src/scene.cpp.obj\n", out)
        self.assertIn("ninja: build stopped: subcommand failed.\n", out)

    def test_gcc_link_error_without_source_file(self):
        out = self.screen(fixture("link_error.txt"), "--exit", "2")
        self.assertIn("main.cpp:(.text+0x10): undefined reference to `missingFunction()'\n", out)
        self.assertTrue(out.endswith(
            "Failed: stopped after 1 file (0m00s). Full log: build-linux/build.log\n"
            "ERROR: link failed\n"), out)

    def test_msvc_link_error_without_source_file(self):
        out = self.screen(fixture("msvc_link_error.txt"), "--exit", "1")
        self.assertTrue(out.endswith("ERROR: link failed\n"), out)
        self.assertNotIn("WARNING", out)

    def test_clang_linker_failure(self):
        out = self.screen("clang++: error: linker command failed with exit code 1\n", "--exit", "1")
        self.assertTrue(out.endswith("ERROR: link failed\n"), out)

    def test_link_error_next_to_compile_errors(self):
        out = self.screen("/s/a.cpp:1:1: error: bad\ncollect2: error: ld returned 1 exit status\n",
                          "--exit", "1")
        self.assertIn("ERROR: 2 (a.cpp, link failed)\n", out)

    # --- Windows / Ninja (#1240 follow-up) ---------------------------------

    def test_ninja_status_lines_are_progress_and_noise(self):
        # "Re-checking globbed directories" and the custom command line are
        # Ninja status lines: hidden, and they must not break the dot line.
        out = self.screen(fixture("windows_ninja_partial.txt"), "--total", "56", "--seconds", "9")
        self.assertNotIn("Re-checking", out)
        self.assertNotIn("cmd.exe", out)
        self.assertNotIn("_tc_check_hot_reload", out)
        self.assertEqual(len(out.splitlines()), 2, out)

    def test_ninja_line_that_is_not_a_status_line_is_kept(self):
        out = self.screen("[1/6] Building CXX object a.obj\nninja: warning: manifest\n"
                          "[note] [1/2] not a status line\n")
        self.assertIn("ninja: warning: manifest\n", out)
        self.assertIn("[note] [1/2] not a status line\n", out)

    def test_successful_ninja_build_always_ends_at_100_percent(self):
        # The last status line is [45/56]: 80 %, but the build succeeded.
        out = self.screen(fixture("windows_ninja_partial.txt"), "--total", "56")
        self.assertTrue(out.startswith("Building myApp (56 files) .........50%.........100%\n"), out)
        self.assertIn("Done: 44 files, 12 up to date", out)

    def test_failed_ninja_build_stays_where_it_is(self):
        text = fixture("windows_ninja_partial.txt")
        out = self.screen(text, "--total", "56", "--exit", "1")
        first = out.splitlines()[0]
        self.assertTrue(first.startswith("Building myApp (56 files) .........50%"), first)
        self.assertFalse(first.endswith("100%"), first)

    def test_make_still_reaches_100_percent_once(self):
        out = self.screen(fixture("make_build.txt"), "--total", "44")
        self.assertEqual(out.splitlines()[0], "Building myApp (44 files) .........50%.........100%")

    def test_failed_step_command_line_is_hidden_but_failed_line_stays(self):
        out = self.screen(fixture("ninja_failed.txt"), "--total", "6", "--exit", "1")
        self.assertIn("FAILED: CMakeFiles/app.dir/src/scene.cpp.obj\n", out)
        self.assertNotIn("cl.exe", out)
        self.assertNotIn("/nologo", out)
        self.assertIn("scene.cpp(9): error C2143", out)
        self.assertIn("ninja: build stopped: subcommand failed.\n", out)
        self.assertIn("ERROR: 1 (scene.cpp)\n", out)

    def test_only_a_compiler_command_after_failed_is_hidden(self):
        for name, line in (
                ("custom step", 'cmd.exe /C "cd /D C:\\p && cmake.exe -P x.cmake"'),
                ("plain text", "something went wrong in -c mode"),
                ("linker", "link.exe /nologo a.obj /out:app.exe"),
                ("gcc without compile flag", "/usr/bin/c++ --version")):
            with self.subTest(name=name):
                out = self.screen("FAILED: x\n" + line + "\n", "--exit", "1")
                self.assertIn(line + "\n", out)
        for line in ('/usr/bin/c++ -DX -c /s/a.cpp -o a.o',
                     'ccache /usr/bin/c++ -DX -c /s/a.cpp -o a.o',
                     '"C:\\Program Files\\VC\\bin\\cl.exe" /nologo /Fo a.obj -c a.cpp'):
            with self.subTest(line=line):
                out = self.screen("FAILED: x\n" + line + "\n", "--exit", "1")
                self.assertNotIn(line, out)
                self.assertIn("FAILED: x\n", out)

    def test_command_line_is_hidden_only_right_after_failed(self):
        line = "/usr/bin/c++ -DX -c /s/a.cpp -o a.o"
        out = self.screen("FAILED: x\nsome text\n" + line + "\n", "--exit", "1")
        self.assertIn(line + "\n", out)

    def test_log_keeps_the_hidden_command_line(self):
        _, log = self.run_filter(fixture("ninja_failed.txt"), "--exit", "1")
        self.assertIn("cl.exe", log)

    # --- configure output on Windows (vcvars, preset block) ---------------

    def test_windows_configure_noise_is_hidden(self):
        out, log = self.run_filter(fixture("windows_configure.txt"), "--configure")
        self.assertEqual(out, "")
        self.assertEqual(log, fixture("windows_configure.txt"))

    def test_preset_block_is_hidden_on_every_os_up_to_its_blank_line(self):
        text = ("Preset CMake variables:\n\n  CMAKE_EXPORT_COMPILE_COMMANDS=\"ON\"\n\n"
                "-- The C compiler identification is GNU 13.3.0\n"
                "CMake Error at CMakeLists.txt:3 (message):\n  bad\n")
        out = self.screen(text, "--configure", "--exit", "1")
        self.assertNotIn("Preset", out)
        self.assertNotIn("CMAKE_EXPORT", out)
        self.assertIn("CMake Error at CMakeLists.txt:3 (message):\n  bad\n", out)

    def test_preset_block_does_not_swallow_a_following_error(self):
        text = "Preset CMake variables:\n\n  A=\"1\"\nCMake Error: right after the block\n"
        out = self.screen(text, "--configure", "--exit", "1")
        self.assertIn("CMake Error: right after the block\n", out)

    def test_other_not_recognized_lines_stay_visible(self):
        text = ("'foo' is not recognized as an internal or external command,\n"
                "operable program or batch file.\n")
        out = self.screen(text, "--configure", "--exit", "1")
        self.assertIn("'foo' is not recognized as an internal or external command,\n", out)
        self.assertIn("operable program or batch file.\n", out)

    def test_japanese_vswhere_probe_line_is_hidden(self):
        text = "'vswhere.exe' \u306f\u3001\u5185\u90e8\u30b3\u30de\u30f3\u30c9\u307e\u305f\u306f\u5916\u90e8\u30b3\u30de\u30f3\u30c9\u3001\u64cd\u4f5c\u53ef\u80fd\u306a\u30d7\u30ed\u30b0\u30e9\u30e0\u307e\u305f\u306f\u30d0\u30c3\u30c1 \u30d5\u30a1\u30a4\u30eb\u3068\u3057\u3066\u8a8d\u8b58\u3055\u308c\u3066\u3044\u307e\u305b\u3093\u3002\nreal output\n"
        out = self.screen(text, "--configure")
        self.assertEqual(out, "real output\n")

    def test_vcvars_banner_is_not_hidden_in_a_build(self):
        out = self.screen("** Visual Studio banner-like text\n", "--exit", "1")
        self.assertIn("** Visual Studio banner-like text\n", out)

    # --- Done / Failed ----------------------------------------------------

    def test_done_forms(self):
        two = "[ 50%] Building CXX object a.o\n[100%] Building CXX object b.o\n"
        out = self.screen(two, "--total", "44", "--seconds", "30")
        self.assertTrue(out.endswith(
            "Done: 2 files, 42 up to date (0m30s). Full log: build-linux/build.log\n"), out)
        out = self.screen("[100%] Built target app\n", "--total", "44", "--seconds", "2")
        self.assertTrue(out.endswith(
            "Done: all 44 up to date (0m02s). Full log: build-linux/build.log\n"), out)
        out = self.screen("[100%] Building CXX object a.o\n", "--total", "44", "--seconds", "75")
        self.assertIn("Done: 1 file, 43 up to date (1m15s).", out)
        # Without compile_commands.json only what was compiled is known
        out = self.screen(two)
        self.assertIn("Done: 2 files (0m00s).", out)

    def test_failed_before_compiling(self):
        out = self.screen("make: *** No rule to make target 'all'.  Stop.\n", "--exit", "2")
        self.assertIn("Failed: stopped before compiling (0m00s).", out)

    def test_first_word_is_the_result(self):
        self.assertTrue(self.screen("", "--exit", "0").splitlines()[1].startswith("Done:"))
        self.assertTrue(self.screen("", "--exit", "3").splitlines()[1].startswith("Failed:"))

    # --- verbose and log ---------------------------------------------------

    def test_verbose_passes_everything_and_prints_no_summary(self):
        text = fixture("make_configure.txt") + fixture("make_build.txt") + fixture("gcc_diagnostics.txt")
        out, log = self.run_filter(text, "--verbose", "--total", "44", "--exit", "2")
        self.assertEqual(out, text)
        self.assertEqual(log, text)

    def test_log_has_everything_in_every_mode(self):
        text = (fixture("make_configure.txt") + fixture("make_build.txt") +
                fixture("gcc_diagnostics.txt"))
        for args in ([], ["--configure"], ["--verbose"], ["--exit", "2"]):
            with self.subTest(args=args):
                _, log = self.run_filter(text, *args)
                self.assertEqual(log, text)
        _, log = self.run_filter(text, chunk=5)
        self.assertEqual(log, text)

    def test_crlf_input(self):
        out = self.screen("[ 50%] Building CXX object a.o\r\n/s/a.cpp:1:1: warning: w\r\n")
        self.assertNotIn("\r", out)
        self.assertIn("/s/a.cpp:1:1: warning: w\n", out)
        self.assertIn("WARNING: 1 (a.cpp)\n", out)

    def test_last_line_without_newline_is_not_lost(self):
        out = self.screen("/s/a.cpp:1:1: error: no newline at the end", "--exit", "1")
        self.assertIn("/s/a.cpp:1:1: error: no newline at the end\n", out)
        self.assertIn("ERROR: 1 (a.cpp)\n", out)

    def test_very_long_line_is_bounded_and_kept(self):
        text = "x" * (300 * 1024)
        out, log = self.run_filter(text)
        self.assertEqual(log, text)
        # Split into several lines, none longer than the bound; no byte lost.
        pieces = [line for line in out.split("\n") if line and set(line) == {"x"}]
        self.assertGreater(len(pieces), 1)
        self.assertEqual(sum(map(len, pieces)), len(text))
        self.assertLessEqual(max(map(len, pieces)), 64 * 1024)

    # --- configure phase ---------------------------------------------------

    def test_configure_hides_dash_lines_and_says_nothing_on_success(self):
        out, log = self.run_filter(fixture("make_configure.txt"), "--configure")
        self.assertEqual(out, "")
        self.assertEqual(log, fixture("make_configure.txt"))

    def test_configure_error_passes_through_and_names_the_log(self):
        out = self.screen(fixture("configure_error.txt"), "--configure", "--exit", "1")
        self.assertIn("CMake Error at CMakeLists.txt:12 (find_package):\n", out)
        self.assertIn('Could not find a package configuration file provided by "Missing".\n', out)
        self.assertNotIn("-- The C compiler", out)
        self.assertTrue(out.endswith(
            "Failed: configure stopped (0m00s). Full log: build-linux/build.log\n"), out)

    def test_configure_verbose_shows_everything(self):
        text = fixture("make_configure.txt")
        self.assertEqual(self.screen(text, "--configure", "--verbose"), text)


if __name__ == "__main__":
    unittest.main()
