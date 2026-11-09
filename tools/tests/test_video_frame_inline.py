"""Compile the production frame facade: only compile-time Inline delivery is accepted."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VideoFrameInlineTests(unittest.TestCase):
    def test_inline_only(self):
        header = (ROOT/'core/include/tc/video/tcVideoGrabber.h').read_text(encoding='utf-8')
        # Compile the real view/facade declarations against the real Event.
        # The rest of VideoGrabber requires the graphics umbrella header.
        declarations = header.split('namespace internal { struct GrabberSharedState; }', 1)[1]
        declarations = declarations.split('\nnamespace internal {\n', 1)[0]
        prefix = '''
#include "tc/utils/tcAnnotations.h"
#include "tc/events/tcEvent.h"
namespace trussc {
enum class PixelFormat { U8, F32 };
namespace internal { struct GrabberSharedState; }
'''
        # The Android stub uses this same shared facade without a camera SDK.
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch)/'probe.cpp'
            for arguments, rejected in (('', False), (', 5', False),
                                        (', trussc::Deliver::Inline', False),
                                        (', trussc::Deliver::Inline, 5', False),
                                        (', trussc::Deliver::Main', True),
                                        (', trussc::Deliver::Main, 5', True),
                                        (', runtime', True), (', runtime, 5', True)):
                source.write_text(prefix + declarations + '\n}\nvoid probe(trussc::Deliver runtime) {\n'
                                  'trussc::VideoFrameEvent event;\n'
                                  'auto listener = event.listen([](const trussc::VideoFrameView&) {}'
                                  + arguments + ');\n}\n', encoding='utf-8')
                result = subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                                         '-fsyntax-only', '-I'+str(ROOT/'core/include'),
                                         '-D__ANDROID__', str(source)], capture_output=True, text=True, encoding='utf-8')
                self.assertEqual(result.returncode != 0, rejected, result.stderr)
                if rejected:
                    self.assertIn('constant expression', result.stderr)
                    if 'Main' in arguments:
                        self.assertIn('getQueuedFrames()', result.stderr)
                        self.assertIn('isFrameNew()/getPixels()', result.stderr)


if __name__ == '__main__':
    unittest.main()
