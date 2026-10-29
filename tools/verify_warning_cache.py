#!/usr/bin/env python3
"""Check that the CI compiler cache replays stderr on an actual cache hit."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

MARKER = 'TrussC warning cache replay probe'


def hits(launcher):
    if launcher == 'ccache':
        result = subprocess.check_output([launcher, '--print-stats'], text=True, encoding='utf-8')
        counters = dict(line.split() for line in result.splitlines())
        return sum(int(counters.get(key, 0)) for key in ('direct_cache_hit', 'preprocessed_cache_hit'))
    result = json.loads(subprocess.check_output([launcher, '--show-stats', '--stats-format=json'], text=True, encoding='utf-8'))

    def total(value):
        return sum(total(v) for v in value.values()) if isinstance(value, dict) else int(value)
    return total(result['stats']['cache_hits'])


def verify(launcher):
    compiler = 'cl' if os.name == 'nt' else os.environ.get('CXX', 'c++')
    if not shutil.which(compiler):
        raise RuntimeError(f'compiler not found: {compiler}')
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'warning.cpp'
        output = Path(directory) / 'warning.o'
        source.write_text(f'[[deprecated("{MARKER}")]] int oldApi() {{ return 0; }}\n'
                          'int useApi() { return oldApi(); }\n', encoding='utf-8')
        command = [launcher, compiler]
        if os.name == 'nt':
            command += ['/nologo', '/std:c++20', '/W3', '/c', str(source), '/Fo' + str(output)]
        else:
            command += ['-std=c++20', '-Wall', '-c', str(source), '-o', str(output)]
        for attempt in range(2):
            before = hits(launcher)
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, encoding='utf-8', errors='replace')
            if result.returncode or MARKER not in result.stdout:
                raise RuntimeError(f'compile {attempt + 1} did not replay the warning:\n{result.stdout}')
            after = hits(launcher)
            print(f'{launcher} compile {attempt + 1}: warning visible; cache hit delta {after - before}')
            if attempt == 1 and after <= before:
                raise RuntimeError('second compile was not a cache hit')
            output.unlink()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('launcher', choices=['ccache', 'sccache'])
    args = parser.parse_args()
    try:
        return verify(args.launcher)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
