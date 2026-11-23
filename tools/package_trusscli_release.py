#!/usr/bin/env python3
"""Package the payload extracted directly into a cached version's tools/bin/."""
import argparse
import hashlib
from pathlib import Path
import tarfile


def safe_tag(value):
    if not value or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-' for c in value) or value in ('.', '..'):
        raise argparse.ArgumentTypeError('tag must be a single safe filename component')
    return value


def package(args):
    name = {'linux': 'trusscli', 'windows': 'trusscli.exe', 'macos': 'trusscli.app'}[args.os]
    source = args.bin_dir / name
    binary = source / 'Contents/MacOS/trusscli' if args.os == 'macos' else source
    if not binary.is_file():
        raise ValueError(f'missing executable: {binary}')
    members = [source, *sorted(source.rglob('*'))] if source.is_dir() else [source]
    args.out.mkdir(parents=True, exist_ok=True)
    archive = args.out / f'trusscli-{args.tag}-{args.os}-{args.arch}.tar.gz'
    with tarfile.open(archive, 'w:gz') as output:
        for member in members:
            relative = member.relative_to(args.bin_dir).as_posix()
            if member.is_symlink():
                # Match the downloader: only relative links without traversal.
                target = member.readlink().as_posix()
                if target.startswith('/') or '..' in target.split('/') or ':' in target or '\\' in target:
                    raise ValueError(f'unsafe symbolic link: {relative}')
            elif not (member.is_file() or member.is_dir()):
                raise ValueError(f'unsupported member: {relative}')
            output.add(member, arcname=relative, recursive=False)
    return archive


def checksums(args):
    output = args.dir / f'trusscli-{args.tag}-SHA256SUMS.txt'
    lines = []
    for archive in sorted(args.dir.glob(f'trusscli-{args.tag}-*.tar.gz')):
        digest = hashlib.sha256()
        with archive.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        lines.append(f'{digest.hexdigest()}  {archive.name}\n')
    if not lines:
        raise ValueError('no release archives found')
    output.write_text(''.join(lines), encoding='utf-8')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    pack = commands.add_parser('package')
    pack.add_argument('--tag', required=True, type=safe_tag)
    pack.add_argument('--os', required=True, choices=('linux', 'macos', 'windows'))
    pack.add_argument('--arch', required=True, choices=('x86_64', 'arm64'))
    pack.add_argument('--bin-dir', required=True, type=Path)
    pack.add_argument('--out', required=True, type=Path)
    sums = commands.add_parser('checksums')
    sums.add_argument('--tag', required=True, type=safe_tag)
    sums.add_argument('--dir', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(package(args) if args.command == 'package' else checksums(args))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
