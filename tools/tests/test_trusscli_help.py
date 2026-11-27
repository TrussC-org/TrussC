"""Headless command help regressions; set TRUSSCLI to the binary to test."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()


class HelpTests(unittest.TestCase):
    def cli(self, *args):
        # Each invocation starts in an empty directory, without project metadata.
        with tempfile.TemporaryDirectory(prefix="trusscli-help-") as scratch:
            result = subprocess.run(
                [str(CLI), *args], cwd=scratch,
                env=dict(os.environ, TRUSSC_DIR=str(ROOT)),
                capture_output=True, encoding="utf-8", errors="replace",
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(list(Path(scratch).iterdir()), [], args)
            return result.stdout

    def listed_commands(self, help_text):
        self.assertIn("Commands:\n", help_text)
        block = help_text.split("Commands:\n", 1)[1].split("\n\n", 1)[0]
        commands = re.findall(r"^  ([a-z]+)\b", block, re.MULTILINE)
        self.assertTrue(commands, help_text)
        return commands

    def test_every_listed_command_prints_help(self):
        commands = [(name,) for name in self.listed_commands(self.cli("--help"))]
        commands += [("addon", name) for name in
                     self.listed_commands(self.cli("addon", "--help"))]
        commands += [("cache", name) for name in
                     self.listed_commands(self.cli("cache", "--help"))]
        # Completion is dispatched but not advertised in the top-level list.
        commands.append(("completion",))
        for command in commands:
            for flag in ("--help", "-h"):
                with self.subTest(command=command, flag=flag):
                    output = self.cli(*command, flag)
                    self.assertTrue(output.startswith("Usage: trusscli " + " ".join(command)),
                                    output)

    def test_run_help_describes_busy_x11_display(self):
        output = self.cli("run", "--help")
        self.assertIn("x11 errors if the display :0 socket or lock file already exists.", output)

    def test_version_help_does_not_print_version(self):
        for flag in ("--help", "-h"):
            with self.subTest(flag=flag):
                output = self.cli("version", flag)
                self.assertTrue(output.startswith("Usage: trusscli version\n"), output)
                self.assertIn("Show version (trusscli + current TrussC).\n", output)
                self.assertIn("-h, --help", output)
                self.assertNotIn("(built ", output)
                self.assertNotIn(str(ROOT), output)

    def test_cache_remove_syntax_is_listed(self):
        for args in (("--help",), ("cache", "--help")):
            output = self.cli(*args)
            self.assertIn("remove <spec>", output)
            self.assertIn("remove --addon <spec>", output)

    def test_command_description_columns_align(self):
        for args in (("--help",), ("cache", "--help")):
            output = self.cli(*args)
            block = output.split("Commands:\n", 1)[1].split("\n\n", 1)[0]
            columns = []
            for line in block.splitlines():
                match = re.match(r"^  \S.*?\s{2,}(\S.*)$", line)
                self.assertIsNotNone(match, line)
                columns.append(match.start(1))
            self.assertEqual(len(set(columns)), 1, block)
        self.assertIn("Manage cached versions: list, remove, autoremove, clear", self.cli("--help"))

    def test_plain_version_matches_existing_aliases(self):
        output = self.cli("version")
        self.assertRegex(output, r"^trusscli .+ \(built .+\)\nTrussC   ")
        self.assertEqual(output, self.cli("--version"))
        self.assertEqual(output, self.cli("-v"))


if __name__ == "__main__":
    unittest.main()
