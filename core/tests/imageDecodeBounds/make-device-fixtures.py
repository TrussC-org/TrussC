#!/usr/bin/env python3
"""Portable image device fixtures; only Python's standard library is required."""
import argparse
import base64
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import zlib

# Embedded baseline JPEG: 8x8 opaque white, with no runtime encoder dependency.
JPEG = "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAAIAAgDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD9U6KKKAP/2Q=="


def chunk(name, data):
    return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data))


def png(width, height=1, cgbi=False, white=False):
    # Same CgBI chunk order and raw-DEFLATE convention as zeroPng in main.cpp.
    raw = (b'\0' + (b'\xff' * (width * 4) if white else bytes(width * 4))) * height
    compressor = zlib.compressobj(wbits=-15 if cgbi else 15)
    data = compressor.compress(raw) + compressor.flush()
    return (b'\x89PNG\r\n\x1a\n' + (chunk(b'CgBI', b'\x40\0\0\0') if cgbi else b'') +
            chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', data) + chunk(b'IEND', b''))


def main():
    print('+ ' + shlex.join(['python3', *sys.argv]), flush=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--axis', type=int, help='recorded device max_image_size_2d')
    parser.add_argument('--heic', action='store_true', help='use macOS sips for HEIC')
    args = parser.parse_args()
    # Bound (test): sokol image sizes are int and real GPU limits are far below 65535; outside 1..65535 the generator stops with a usage error.
    if args.axis is not None and not 1 <= args.axis <= 65535:
        parser.error('--axis must be in 1..65535')
    root = args.directory
    root.mkdir(parents=True, exist_ok=True)
    manifest = {}

    def write(name, data, width=0, height=0, sample=None, **extra):
        print(f'write {root / name} ({len(data)} bytes)', flush=True)
        (root / name).write_bytes(data)
        manifest[name] = dict(width=width, height=height, channels=4,
                              sample=sample or [0, 0, 0, 0], **extra)

    write('small.png', png(8), 8, 1)
    write('small.jpg', base64.b64decode(JPEG), 8, 8, [255, 255, 255, 255], tolerance=1)
    write('cgbi.png', png(8, cgbi=True), 8, 1)
    write('over-axis.png', png(16385), 16385, 1)
    write('corrupt.png', b'\x89PNG\r\n\x1a\n')
    # Sparse file keeps generation cheap; read mode still probes cap + 1 bytes.
    print(f'write {root / "encoded-plus-1.bin"} (67108865 bytes, sparse)', flush=True)
    with (root / 'encoded-plus-1.bin').open('wb') as output:
        output.seek(64 * 1024 * 1024)
        output.write(b'\0')
    manifest['encoded-plus-1.bin'] = dict(width=0, height=0, channels=4, sample=[0]*4)
    if args.axis is not None:
        for suffix, size in [('', args.axis), ('-plus-1', args.axis + 1)]:
            write(f'axis{suffix}.png', png(size), size, 1)
            write(f'axis-y{suffix}.png', png(1, size), 1, size)
    if sys.platform == 'darwin':
        for name, width in [('small.tiff', 8), ('over-axis.tiff', 16385)]:
            entries = [(256, 4, width), (257, 4, 1), (258, 3, 8), (259, 3, 1),
                       (262, 3, 1), (273, 4, 122), (277, 3, 1), (278, 4, 1), (279, 4, width)]
            tiff = b'II' + struct.pack('<HIH', 42, 8, len(entries))
            for tag, kind, value in entries:
                tiff += struct.pack('<HHII', tag, kind, 1, value)
            tiff += bytes(4) + b'\xff' * width
            write(name, tiff, width, 1, [255]*4, memory='decode-failed')
    if args.heic and sys.platform == 'darwin':
        # Opaque white avoids alpha/color conversion ambiguity in the pixel sample.
        write('heic-source.png', png(8, 8, white=True), 8, 8, [255]*4)
        command = ['sips', '-s', 'format', 'heic', str(root / 'heic-source.png'),
                   '--out', str(root / 'small.heic')]
        print('+ ' + shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        manifest['small.heic'] = dict(width=8, height=8, channels=4,
                                      sample=[255]*4, tolerance=3, memory='decode-failed')
    else:
        print('HEIC skipped: use --heic on macOS with sips.', flush=True)
    print(f'write {root / "manifest.json"}', flush=True)
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
