#!/usr/bin/env python3
"""Regression checks for core sweep selection, registration and timing (#586)."""

import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "build_all", Path(__file__).resolve().parents[1] / "examples" / "build_all.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class CoreTestRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = io.StringIO()
        self.enterContext(contextlib.redirect_stdout(self.output))
        self.enterContext(patch.object(runner, "ROOT_DIR", str(self.root)))
        self.options = SimpleNamespace(verbose=False, core_test_timings=[])
        self.platform = {"os": "linux", "build_dir": "build-linux", "cmake_generator": None}

    def project(self, name, daily=False, own=False, unit=False, web=False):
        directory = self.root / "core" / "tests" / name
        directory.mkdir(parents=True)
        if unit:
            (directory / "CMakeLists.txt").touch()
        else:
            (directory / "src").mkdir()
            (directory / "src" / "main.cpp").touch()
        for marker, enabled in (("daily-only", daily), ("own-binary", own), ("web-test", web)):
            if enabled:
                (directory / marker).touch()
        return str(directory)

    def test_daily_selection_for_every_test_shape(self):
        self.project("allCoreTests")
        regular = self.project("regular", web=True)
        daily = self.project("daily", daily=True, web=True)
        own = self.project("own", own=True)
        daily_own = self.project("dailyOwn", daily=True, own=True)
        unit = self.project("unit", unit=True)
        daily_unit = self.project("dailyUnit", daily=True, unit=True)
        for include_daily in (False, True):
            with self.subTest(include_daily=include_daily):
                self.assertEqual(set(runner.find_core_tests(self.root, include_daily)),
                                 {regular, own} | ({daily, daily_own} if include_daily else set()))
                self.assertEqual(set(runner.find_core_unit_tests(self.root, include_daily)),
                                 {unit} | ({daily_unit} if include_daily else set()))
                self.assertEqual(set(runner.find_core_web_tests(self.root, include_daily)),
                                 {regular} | ({daily} if include_daily else set()))

    def combined(self, selected, names, list_status=0):
        listing = subprocess.CompletedProcess([], list_status, "\n".join(names).encode())
        with patch.object(runner, "build_test_project", return_value=("fake-binary", None)), \
                patch.object(runner.subprocess, "run", return_value=listing), \
                patch.object(runner, "run_test_binary", return_value=True) as run:
            rc = runner.run_combined_core_tests(selected, "pg", self.platform, self.options)
        return rc, [call.kwargs["args"][0] for call in run.call_args_list]

    def test_registered_daily_test_skips_pr_and_runs_daily(self):
        regular = self.project("regular")
        daily = self.project("daily", daily=True)
        self.assertEqual(self.combined([regular], ["daily", "regular"]), (0, ["regular"]))
        self.assertEqual(self.combined([regular, daily], ["daily", "regular"]),
                         (0, ["daily", "regular"]))

    def test_missing_registration_fails_even_for_skipped_daily_test(self):
        regular = self.project("regular")
        self.project("daily", daily=True)
        self.assertEqual(self.combined([regular], ["regular"])[0], 1)
        self.assertIn("daily (not registered", self.output.getvalue())

    def test_unknown_registration_fails(self):
        regular = self.project("regular")
        self.assertEqual(self.combined([regular], ["regular", "unknown"])[0], 1)
        self.assertIn("unknown (registered, but no such combined test dir)", self.output.getvalue())

    def test_all_daily_inventory_is_still_checked_without_selected_runs(self):
        self.project("daily", daily=True)
        self.assertEqual(self.combined([], ["daily"]), (0, []))
        self.assertEqual(self.combined([], ["unknown"]), (1, []))

    def test_failed_list_output_does_not_start_tests(self):
        regular = self.project("regular")
        self.assertEqual(self.combined([regular], ["regular"], list_status=1), (1, []))

    def test_cli_daily_flag_selects_combined_own_unit_and_web_tests(self):
        daily = self.project("daily", daily=True, web=True)
        own = self.project("own", daily=True, own=True)
        unit = self.project("unit", daily=True, unit=True)
        for include_daily in (False, True):
            with self.subTest(include_daily=include_daily):
                argv = ["build_all.py", "--core-tests-only", "--web"]
                if include_daily:
                    argv.append("--include-daily")
                with patch.object(sys, "argv", argv), \
                        patch.object(runner, "find_project_generator", return_value="pg"), \
                        patch.object(runner, "get_platform_info", return_value=self.platform), \
                        patch.object(runner, "run_combined_core_tests", return_value=0) as combined, \
                        patch.object(runner, "run_test_suite", return_value=0) as suite, \
                        self.assertRaises(SystemExit) as exit_status:
                    runner.main()
                self.assertEqual(exit_status.exception.code, 0)
                self.assertEqual(combined.call_args.args[0], [daily] if include_daily else [])
                self.assertEqual([call.args[0] for call in suite.call_args_list],
                                 [[own], [unit], [daily]] if include_daily else [])

    def test_process_wall_times_include_failures_and_timeouts(self):
        for result, expected_ok in (
                (subprocess.CompletedProcess([], 0, b"passed"), True),
                (subprocess.CompletedProcess([], 1, b"failed"), False),
                (subprocess.TimeoutExpired("fake", 600, output=b"partial"), False)):
            with self.subTest(result=result):
                timings = []
                kwargs = {"side_effect": result} if isinstance(result, Exception) else {"return_value": result}
                with patch.object(runner.subprocess, "run", **kwargs), \
                        patch.object(runner.time, "monotonic", side_effect=[10.0, 14.25]):
                    ok = runner.run_test_binary("fake", str(self.root), timings=timings, timing_name="test")
                self.assertEqual(ok, expected_ok)
                self.assertEqual(timings, [("test", 4.25, expected_ok)])
        self.assertIn("Wall time: 4.250s (test)", self.output.getvalue())

    def test_native_and_unit_runs_collect_timings_after_build(self):
        project = self.project("own", own=True)
        unit = self.project("unit", unit=True)
        with patch.object(runner, "build_test_project", return_value=("fake", None)), \
                patch.object(runner, "run_command", return_value=True), \
                patch.object(runner, "find_test_binary", return_value="fake"), \
                patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, b"")), \
                patch.object(runner.time, "monotonic", side_effect=[10, 12, 30, 33]):
            self.assertEqual(runner.build_and_run_test(project, "pg", self.platform, self.options), (True, None))
            self.assertEqual(runner.build_and_run_unit_test(unit, "pg", self.platform, self.options), (True, None))
        self.assertEqual(self.options.core_test_timings,
                         [("core/tests/own", 2, True), ("core/tests/unit", 3, True)])

    def test_summary_sorts_all_runs_and_marks_failure(self):
        runner.print_core_test_timings([
            ("core/tests/combined", 1, True),
            ("core/tests/unit", 3, True),
            ("core/tests/own", 2, False)])
        output = self.output.getvalue()
        self.assertLess(output.index("core/tests/unit"), output.index("core/tests/own"))
        self.assertLess(output.index("core/tests/own"), output.index("core/tests/combined"))
        self.assertIn("core/tests/own (FAILED)", output)


if __name__ == "__main__":
    unittest.main()
