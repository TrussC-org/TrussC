"""Offline release packaging and workflow contract checks (existing PyYAML)."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/package_trusscli_release.py'
WORKFLOW = yaml.load((ROOT / '.github/workflows/trusscli-release.yml').read_text(encoding='utf-8'), Loader=yaml.BaseLoader)


class PackageTests(unittest.TestCase):
    def test_payload_names_modes_order_checksums_and_isolation(self):
        with tempfile.TemporaryDirectory() as scratch:
            folder = Path(scratch)
            bin_dir, output = folder / 'bin', folder / 'out'
            bin_dir.mkdir()
            (bin_dir / 'unrelated').write_text('excluded', encoding='utf-8')
            names = {'linux': 'trusscli', 'windows': 'trusscli.exe', 'macos': 'trusscli.app/Contents/MacOS/trusscli'}
            archives = []
            for os_name, name in names.items():
                binary = bin_dir / name
                binary.parent.mkdir(parents=True, exist_ok=True)
                binary.write_bytes(b'stub')
                binary.chmod(0o755)
                subprocess.run([sys.executable, '-I', str(SCRIPT), 'package', '--tag', 'v1', '--os', os_name,
                                '--arch', 'arm64' if os_name == 'macos' else 'x86_64',
                                '--bin-dir', str(bin_dir), '--out', str(output)], check=True)
                archive = next(output.glob(f'trusscli-v1-{os_name}-*.tar.gz'))
                archives.append(archive)
                with tarfile.open(archive) as stream:
                    members = stream.getmembers()
                    self.assertEqual([m.name for m in members], sorted(m.name for m in members))
                    self.assertEqual([m.name for m in members if m.isfile()], [name])
                    self.assertEqual(stream.getmember(name).mode & 0o111, binary.stat().st_mode & 0o111)
                    self.assertTrue(all(not m.name.startswith('/') and '..' not in m.name.split('/') for m in members))
            subprocess.run([sys.executable, '-I', str(SCRIPT), 'checksums', '--tag', 'v1', '--dir', str(output)], check=True)
            self.assertEqual((output / 'trusscli-v1-SHA256SUMS.txt').read_text(encoding='utf-8'), ''.join(
                hashlib.sha256(a.read_bytes()).hexdigest() + '  ' + a.name + '\n' for a in sorted(archives)))

    def test_unsafe_tag_is_rejected(self):
        result = subprocess.run([sys.executable, '-I', str(SCRIPT), 'checksums', '--tag', '../v1', '--dir', '.'], capture_output=True)
        self.assertNotEqual(result.returncode, 0)


@unittest.skipIf(sys.platform == 'win32', 'POSIX helper fixture; Windows gets a cross-target syntax check')
class ProcessProbeTests(unittest.TestCase):
    def test_environment_is_child_only_and_closed_pipe_times_out(self):
        with tempfile.TemporaryDirectory() as scratch:
            folder = Path(scratch)
            source = folder / 'probe.cpp'
            source.write_text(r'''#include "ProcessCapture.h"
#include <cstdlib>
#include <cstring>
#include <unistd.h>
int main(int argc, char** argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "closed") == 0) {
            close(1);
            close(2);
            sleep(60);
        }
        return getenv("TRUSSC_DELEGATED_ROOT") ? 9 : 0;
    }
    setenv("TRUSSC_DELEGATED_ROOT", "parent value", 1);
    if (runCaptured({argv[0], "--version"}, [](const char*, size_t) {}, nullptr, nullptr,
                    1000, {"TRUSSC_DELEGATED_ROOT"}) != 0) return 1;
    if (!getenv("TRUSSC_DELEGATED_ROOT")) return 2;
    if (runCaptured({argv[0], "closed"}, [](const char*, size_t) {}, nullptr, nullptr, 100) != -2) return 3;
    return 0;
}
''', encoding='utf-8')
            binary = folder / 'probe'
            subprocess.run(['c++', '-std=c++20', '-I', str(ROOT / 'tools/src'), str(source),
                            str(ROOT / 'tools/src/ProcessCapture.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


class WorkflowTests(unittest.TestCase):
    def test_triggers_permissions_platforms_and_packaging(self):
        self.assertEqual(WORKFLOW['on']['release']['types'], ['published'])
        self.assertEqual(WORKFLOW['on']['workflow_dispatch']['inputs']['tag']['required'], 'true')
        jobs = WORKFLOW['jobs']
        for name, job in jobs.items():
            self.assertEqual(job['permissions'], {'contents': 'write' if name == 'upload' else 'read'})
        for os_name, runner in [('linux', 'ubuntu-22.04'), ('macos', 'macos-latest'), ('windows', 'windows-latest')]:
            job = jobs[os_name]
            self.assertEqual(job['runs-on'], runner)
            self.assertIn(os_name, jobs['upload']['needs'])
            checkout = next(s for s in job['steps'] if s.get('uses') == 'actions/checkout@v5')
            self.assertEqual(checkout['with']['ref'], 'refs/tags/${{ env.RELEASE_TAG }}')
            runs = [s.get('run', '') for s in job['steps']]
            check = next(i for i, run in enumerate(runs) if '--version' in run)
            package = next(i for i, run in enumerate(runs) if 'package_trusscli_release.py package' in run)
            self.assertLess(check, package)
            self.assertIn(f'--os {os_name}', runs[package])
            self.assertIn('--tag "$RELEASE_TAG"', runs[package])
        self.assertEqual(jobs['linux']['steps'][1]['uses'], './.github/actions/apt-deps')
        self.assertTrue(any(s.get('uses') == 'ilammy/msvc-dev-cmd@v1' for s in jobs['windows']['steps']))

    def test_builds_preserve_clean_checkout(self):
        for job in WORKFLOW['jobs'].values():
            for step in job['steps']:
                self.assertNotRegex(step.get('run', ''), r'\bsed\s+-i\b')
        for name in ('linux', 'macos'):
            build = next(s for s in WORKFLOW['jobs'][name]['steps'] if s.get('name') == 'Build trusscli')
            self.assertIn('cmake -S tools -B tools/build\n', build['run'])
            self.assertIn('cmake --build tools/build --config MinSizeRel --parallel', build['run'])
        for name in ('linux', 'macos', 'windows'):
            check = next(s for s in WORKFLOW['jobs'][name]['steps'] if s.get('name') == 'Check executable before packaging')
            self.assertIn('git diff --quiet', check['run'])
            self.assertIn('*-dirty*) echo', check['run'])
        mac_build = next(s for s in WORKFLOW['jobs']['macos']['steps'] if s.get('name') == 'Build trusscli')
        self.assertEqual(mac_build['env']['MACOSX_DEPLOYMENT_TARGET'], '14.0')

    def test_upload_gate_and_dispatch_only_clobber(self):
        steps = WORKFLOW['jobs']['upload']['steps']
        gate = next(i for i, s in enumerate(steps) if s.get('id') == 'protocol')
        self.assertIn('[ ! -f tools/trusscli-protocol ]', steps[gate]['run'])
        self.assertIn('exit 0', steps[gate]['run'])
        upload = next(i for i, s in enumerate(steps) if 'gh release upload' in s.get('run', ''))
        self.assertLess(gate, upload)
        for step in steps[gate + 1:]:
            self.assertEqual(step['if'], "steps.protocol.outputs.supported == 'true'")
        script = steps[upload]['run']
        self.assertIn('package_trusscli_release.py checksums', script)
        self.assertIn('if [ "$EVENT_NAME" = workflow_dispatch ]; then\n  options+=(--clobber)\nfi', script)
        self.assertEqual(script.count('--clobber'), 1)
        self.assertLess(script.index('checksums'), script.index('gh release upload'))

    @unittest.skipIf(sys.platform == 'win32', 'bash workflow syntax is checked on Linux')
    def test_bash_syntax(self):
        for job in WORKFLOW['jobs'].values():
            for step in job['steps']:
                if 'run' in step:
                    with self.subTest(step=step):
                        subprocess.run(['bash', '-n'], input=step['run'], text=True, encoding='utf-8', check=True)


if __name__ == '__main__':
    unittest.main()
