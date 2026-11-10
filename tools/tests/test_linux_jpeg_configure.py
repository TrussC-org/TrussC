"""Offline checks of the same JPEG dependency module used by the Linux core."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('cmake') and shutil.which('pkg-config'),
                     'Linux CMake/pkg-config checks')
class LinuxJpegConfigure(unittest.TestCase):
    def configure(self, classic_only):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            scratch = Path(directory)
            (scratch / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.20)\nproject(jpegDependency CXX)\n'
                f'include("{ROOT}/core/cmake/tc_linux_jpeg.cmake")\n', encoding='utf-8')
            pc = scratch / 'pkgconfig'
            pc.mkdir()
            if classic_only:
                (scratch / 'jpeglib.h').write_text('enum J_COLOR_SPACE { JCS_RGB };\n', encoding='utf-8')
                (pc / 'libjpeg.pc').write_text(
                    f'Name: libjpeg\nDescription: Classic-only test stub\nVersion: 1.0\n'
                    f'Cflags: -I{scratch}\nLibs:\n', encoding='utf-8')
            env = dict(os.environ, PKG_CONFIG_LIBDIR=str(pc), PKG_CONFIG_PATH='')
            result = subprocess.run(['cmake', '-S', str(scratch), '-B', str(scratch / 'build')],
                                    env=env, capture_output=True, text=True, encoding='utf-8')
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            output = ' '.join((result.stdout + result.stderr).split())
            self.assertIn('libjpeg-dev', output)
            if classic_only:
                self.assertIn('JCS_EXT_RGBA', output)
            else:
                self.assertIn('pkg-config libjpeg', output)

    def test_missing_library(self):
        self.configure(False)

    def test_classic_only_library(self):
        self.configure(True)


if __name__ == '__main__':
    unittest.main()
