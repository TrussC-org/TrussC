"""Compile the production audio Event overloads, including rejected calls."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AudioEventDeliveryTests(unittest.TestCase):
    def test_delivery_choices(self):
        header = (ROOT / 'core/include/tc/sound/tcSound.h').read_text(encoding='utf-8')
        buffers = header[header.index('struct AudioOutBuffer {'):header.index('struct PlayingSoundInfo {')]
        events = '\n'.join(line for line in header.splitlines()
                           if 'Event<AudioOutBuffer>' in line or 'Event<AudioInBuffer>' in line)
        prefix = '#include "tc/events/tcEvent.h"\nnamespace trussc {\n' + buffers
        prefix += 'struct EngineEvents {\n' + events + '\n};\n}\nusing namespace trussc;\n'
        compilers = [os.environ['CXX']] if 'CXX' in os.environ else [
            path for name in ('g++', 'clang++') if (path := shutil.which(name))]
        if not compilers:
            self.skipTest('requires a C++20 compiler')
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'probe.cpp'
            for compiler in compilers:
                for event, buffer, message in (
                    ('audioOut', 'AudioOutBuffer', 'AudioEngine::getAnalysisBuffer()'),
                    ('audioIn', 'AudioInBuffer', 'MicInput::getBuffer()'),
                ):
                    for member in (False, True):
                        callback = '&receiver, &Receiver::receive' if member else f'[]({buffer}&) {{}}'
                        for priority in ('', ', 500'):
                            for delivery, rejected in (('', False), (', Deliver::Inline', False),
                                                       (', Deliver::Main', True), (', runtime', True)):
                                with self.subTest(compiler=compiler, event=event, member=member,
                                                  priority=priority, delivery=delivery):
                                    source.write_text(prefix + f'''
struct Receiver {{ void receive({buffer}&) {{}} }};
void probe(Deliver runtime) {{
    (void)runtime;
    EngineEvents engine;
    [[maybe_unused]] Receiver receiver;
    auto listener = engine.{event}.listen({callback}{delivery}{priority});
}}
''', encoding='utf-8')
                                    result = subprocess.run(
                                        [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                         '-fsyntax-only', '-I'+str(ROOT/'core/include'), str(source)],
                                        capture_output=True, text=True, encoding='utf-8')
                                    self.assertEqual(result.returncode != 0, rejected, result.stderr)
                                    if delivery == ', Deliver::Main':
                                        self.assertIn(message, result.stderr)
                                    elif rejected:
                                        self.assertIn('constant expression', result.stderr)


if __name__ == '__main__':
    unittest.main()
