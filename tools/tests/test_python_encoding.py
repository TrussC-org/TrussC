"""Keep the cheap encoding check honest, including a failing source fixture."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('encoding_check', ROOT / 'tools/check_python_encoding.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class PythonEncodingTests(unittest.TestCase):
    def test_missing_encodings(self):
        for source in [
            'open("source")', 'open("source", "w")',
            'from pathlib import Path; Path("source").read_text()',
            'path.write_text("data")', 'path.open("a")',
            'import io as stream; stream.open("source")',
            'from codecs import open as load; load("source")',
            'import subprocess as sp; sp.run(["tool"], text=True)',
            'from subprocess import check_output as output; output([], universal_newlines=True)',
            'path.read_text(encoding=None)',
        ]:
            with self.subTest(source=source):
                self.assertEqual(len(list(check.violations(source))), 1)

    def test_explicit_encodings_and_binary_apis(self):
        source = '''
import io
import codecs
import tarfile
from zipfile import ZipFile
import subprocess
open("source", encoding="utf-8")
open("source", "r", -1, "utf-8")
io.open("source", "rb")
codecs.open("source", "r", "utf-8")
path.open("r", -1, "utf-8")
path.read_text("utf-8")
path.write_text("data", encoding="utf-8")
tarfile.open("archive")
ZipFile("archive").open("entry", "r")
subprocess.run([], text=True, encoding="utf-8")
subprocess.run([])
'''
        self.assertEqual(list(check.violations(source)), [])

    def test_archive_open_receivers(self):
        factories = [
            ('import zipfile', 'zipfile.ZipFile'),
            ('import zipfile as zf', 'zf.ZipFile'),
            ('from zipfile import ZipFile', 'ZipFile'),
            ('from zipfile import ZipFile as Zip', 'Zip'),
            ('import tarfile', 'tarfile.open'),
            ('import tarfile as tf', 'tf.open'),
            ('from tarfile import open as open_tar', 'open_tar'),
            ('from tarfile import TarFile', 'TarFile'),
            ('from tarfile import TarFile as Tar', 'Tar.open'),
        ]
        for imports, factory in factories:
            for mode in ('r', 'w'):
                for operation in (
                    f'{factory}("archive").open("entry", "{mode}")',
                    f'with {factory}("archive") as archive:\n'
                    f'    archive.open("entry", "{mode}")',
                    f'archive = {factory}("archive")\n'
                    f'archive.open("entry", mode="{mode}")',
                    f'archive: object = {factory}("archive")\n'
                    f'archive.open("entry", "{mode}")',
                ):
                    source = f'{imports}\n{operation}\n'
                    with self.subTest(source=source):
                        self.assertEqual(list(check.violations(source)), [])

    def test_reassigned_archive_receiver_still_checked(self):
        source = '''
from pathlib import Path
from zipfile import ZipFile
archive = ZipFile("archive")
archive = Path("source")
archive.open("r")
'''
        self.assertEqual(len(list(check.violations(source))), 1)

    def test_parameter_shadowing_archive_receiver_still_checked(self):
        for reader in (
            'def read_source(archive):\n    return archive.open("r")',
            'read_source = lambda archive: archive.open("r")',
        ):
            source = '''\
from pathlib import Path
from zipfile import ZipFile

def read_member():
    with ZipFile("archive.zip") as archive:
        return archive.open("entry", "r")

''' + reader + '\nread_source(Path("source.txt"))\n'
            with self.subTest(reader=reader):
                problems = list(check.violations(source))
                self.assertIn(
                    (8 if reader.startswith('read_source =') else 9,
                     'archive.open: text I/O requires an explicit encoding'),
                    problems,
                )

    def test_bare_read_text_fixture_fails_command(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / 'fixture.py').write_text('from pathlib import Path\nPath("source").read_text()\n', encoding='utf-8')
            with patch.object(check, 'ROOT', root), patch('builtins.print') as output:
                self.assertEqual(check.main(), 1)
            self.assertIn('fixture.py:2:', output.call_args_list[0].args[0])

    def test_repo_passes(self):
        with patch('builtins.print'):
            self.assertEqual(check.main(), 0)


if __name__ == '__main__':
    unittest.main()
