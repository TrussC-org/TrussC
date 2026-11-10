"""Compile the Linux capture sizing helper without camera hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'core/platform/linux/tcVideoGrabber_linux.cpp'


class V4l2FrameSizeTests(unittest.TestCase):
    def test_checked_rgba_bytes(self):
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('requires GCC')
        source = SOURCE.read_text(encoding='utf-8')
        start = source.index('static bool checkedRgbaBytes(')
        helper = source[start:source.index('\n}\n', start) + 3]
        program = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
''' + helper + '\nnamespace narrowSize {\n' + helper.replace('size_t', 'uint16_t') + r'''
}
int main() {
    size_t bytes = 17;
    assert(checkedRgbaBytes(640, 480, bytes) && bytes == size_t(640) * 480 * 4);
    assert(checkedRgbaBytes(1, 1, bytes) && bytes == 4);
    assert(checkedRgbaBytes(16384, 16384, bytes) && bytes == size_t(1) << 30);
    for (int64_t value : {int64_t(0), int64_t(-1), int64_t(16385),
                         int64_t(UINT32_MAX), INT64_MAX}) {
        bytes = 17;
        assert(!checkedRgbaBytes(value, 480, bytes) && bytes == 17);
        assert(!checkedRgbaBytes(640, value, bytes) && bytes == 17);
    }
    assert(!checkedRgbaBytes(INT64_MAX, INT64_MAX, bytes));
    // A narrower size_t model exercises the product boundary within the axis limit.
    uint16_t narrowBytes = 0;
    assert(narrowSize::checkedRgbaBytes(8191, 2, narrowBytes) && narrowBytes == 65528);
    narrowBytes = 17;
    assert(!narrowSize::checkedRgbaBytes(8192, 2, narrowBytes) && narrowBytes == 17);
    assert(!narrowSize::checkedRgbaBytes(2, 8192, narrowBytes) && narrowBytes == 17);
}
'''
        program = '#include <initializer_list>\n' + program
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as temporary:
            path = Path(temporary)
            cpp = path / 'frame_size.cpp'
            cpp.write_text(program, encoding='utf-8')
            binary = path / 'frame_size'
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
