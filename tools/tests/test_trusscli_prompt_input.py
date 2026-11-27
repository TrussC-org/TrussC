"""Prompt byte preservation and UTF-8 dist names; run through tcbuild.sh.

Set TRUSSCLI to a freshly built CLI for the terminal integration check.
"""
import errno
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PromptInputTests(unittest.TestCase):
    def test_pipe_and_file_match_getline(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('requires a C++ compiler')
        with tempfile.TemporaryDirectory(prefix='prompt-input-') as scratch:
            base = Path(scratch)
            source = base / 'probe.cpp'
            source.write_text('''#include "PromptInput.h"
int main(int argc, char**) {
    std::string line;
    while (argc > 1 ? static_cast<bool>(std::getline(std::cin, line))
                    : trusscli::readPromptLine(line)) {
        std::cout << line.size() << ':' << line << '\\n';
    }
    return std::cin.bad() ? 1 : 0;
}
''', encoding='utf-8')
            binary = base / ('probe.exe' if os.name == 'nt' else 'probe')
            subprocess.run([compiler, '-std=c++17', '-Wall',
                            '-Wextra', '-Werror', '-I', str(ROOT / 'tools/src'),
                            str(source), '-o', str(binary)], check=True)
            for content in (b'', b'\n\ny\nn\n', 'Dist テスト\n作者 테스트\n'.encode(),
                            b'raw\xff\x83\r\n' + b'x' * 4096 + b'\nlast line'):
                with self.subTest(content=content[:20]):
                    expected = subprocess.check_output([str(binary), 'getline'], input=content)
                    self.assertEqual(subprocess.check_output([str(binary)], input=content), expected)
                    data = base / 'input'
                    data.write_bytes(content)
                    with data.open('rb') as stream:
                        self.assertEqual(subprocess.check_output([str(binary)], stdin=stream), expected)

    @unittest.skipUnless(os.name == 'posix' and os.environ.get('TRUSSCLI'),
                         'requires a POSIX terminal and a freshly built TRUSSCLI')
    def test_dist_init_terminal_utf8(self):
        import pty
        with tempfile.TemporaryDirectory(prefix='prompt-dist-') as scratch:
            project = Path(scratch)
            (project / 'src').mkdir()
            (project / 'CMakeLists.txt').write_text('# TRUSSC_DIR\n', encoding='utf-8')
            (project / 'addons.make').write_text('', encoding='utf-8')
            master, slave = pty.openpty()
            process = subprocess.Popen([os.environ['TRUSSCLI'], 'dist', '--init'], cwd=project,
                                       env=dict(os.environ, TRUSSC_DIR=str(ROOT)),
                                       stdin=slave, stdout=slave, stderr=slave)
            os.close(slave)
            output = b''
            try:
                os.write(master, 'Dist テスト\n1.2.3\n作者 테스트\n'.encode())
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    if not select.select([master], [], [], 0.1)[0]:
                        continue
                    try:
                        chunk = os.read(master, 4096)
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        break
                    if not chunk:
                        break
                    output += chunk
                self.assertEqual(process.wait(timeout=5), 0, output)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                os.close(master)
            config = json.loads((project / 'dist.json').read_text(encoding='utf-8'))
            self.assertEqual(config['name'], 'Dist テスト')
            self.assertEqual(config['version'], '1.2.3')
            self.assertEqual(config['author'], '作者 테스트')


if __name__ == '__main__':
    unittest.main()
