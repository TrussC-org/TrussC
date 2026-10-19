"""Supply bundled/generated video fixtures to the display mode, then clean up."""
from pathlib import Path
import subprocess
import sys
import tempfile

test_dir = Path(__file__).resolve().parent
source = test_dir.parents[2] / 'addons/tcxHap/tests/bin/data/sine_sowt.mov'
# The marker supplies the executable, optional combined-test name, and mode.
command = sys.argv[1:]
variant = command.pop()
with tempfile.TemporaryDirectory(prefix='tc-display-video-') as scratch:
    fixture = source
    if variant != 'normal':
        fixture = Path(scratch) / 'fixture.mov'
        prepare = [sys.executable, str(test_dir / 'make-fixture.py')]
        if variant != 'error':
            prepare.append('--' + variant)
        subprocess.run(prepare + [str(source), str(fixture)], check=True)
    command.append(str(fixture))
    if variant != 'normal':
        command.append(variant)
    sys.exit(subprocess.run(command).returncode)
