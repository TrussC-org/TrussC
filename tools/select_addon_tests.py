#!/usr/bin/env python3
"""Select bundled addons for PR tests; emit a JSON array for the CI matrix.

Missing diff bases and non-diff events select all addons conservatively.
Input path files use one UTF-8 path per line; git diffs use NUL delimiters.
"""
import argparse
import json
from pathlib import Path, PurePosixPath
import subprocess

ROOT = Path(__file__).resolve().parents[1]
NETWORK_ADDONS = {'tcxMidi', 'tcxOsc', 'tcxTls', 'tcxWebSocket'}
RUNNERS = {'examples/build_all.py', 'tools/select_addon_tests.py',
           '.github/workflows/build.yml'}


def select_addons(paths, root=ROOT):
    available = {p.name for p in (root / 'addons').iterdir() if p.is_dir()}
    selected = set()
    for path in paths:
        if path in RUNNERS:
            return sorted(available)
        if path.startswith('core/include/tc/network/'):
            selected.update(NETWORK_ADDONS)
        parts = PurePosixPath(path).parts
        if len(parts) >= 2 and parts[0] == 'addons':
            selected.add(parts[1])
    return sorted(selected & available)


def event_paths(event, event_name, root=ROOT):
    if event_name == 'pull_request':
        base, head = event['pull_request']['base']['sha'], event['pull_request']['head']['sha']
        comparison = '...'
    elif event_name == 'merge_group':
        base, head = event['merge_group']['base_sha'], event['merge_group']['head_sha']
        comparison = '..'  # Queue's actual base/head trees, including earlier queued PRs.
    elif event_name == 'push':
        base, head = event['before'], event['after']
        comparison = '..'
    else:
        return RUNNERS
    if base == '0' * 40 or subprocess.run(
            ['git', 'cat-file', '-e', f'{base}^{{commit}}'], cwd=root,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        return RUNNERS
    # Disable rename detection so moves between addons select both old and new.
    result = subprocess.check_output(
        ['git', 'diff', '--no-renames', '--name-only', '-z', f'{base}{comparison}{head}'],
        cwd=root)
    return result.decode('utf-8').rstrip('\0').split('\0')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--paths-file', type=Path)
    source.add_argument('--event-file', type=Path)
    parser.add_argument('--event-name')
    args = parser.parse_args()
    if args.paths_file:
        paths = args.paths_file.read_text(encoding='utf-8').splitlines()
    else:
        if not args.event_name:
            parser.error('--event-file requires --event-name')
        paths = event_paths(json.loads(args.event_file.read_text(encoding='utf-8')), args.event_name)
    print(json.dumps(select_addons(paths), separators=(',', ':')))


if __name__ == '__main__':
    main()
