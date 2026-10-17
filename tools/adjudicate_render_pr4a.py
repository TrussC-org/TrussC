#!/usr/bin/env python3
"""Adjudicate #821 PR 4a's explicit three-capture exception, without changing comparison tolerances."""
import argparse
import json
from pathlib import Path
import struct
import zlib

ALLOW = {'premultImage.fbo', 'pointMeshTransform.screen', 'pointMeshTransform.fbo'}


class RGBAImage:
    def __init__(self, width, height, pixels):
        self.width, self.height = width, height
        self.size = (width, height)
        self.pixels = pixels

    def getpixel(self, xy):
        x, y = xy
        offset = (y * self.width + x) * 4
        return tuple(self.pixels[offset:offset + 4])

    def getdata(self):
        return zip(*(self.pixels[channel::4] for channel in range(4)))


def read_png(path):
    """Decode renderRegression's 8-bit RGBA, non-interlaced PNG captures."""
    def reject(reason):
        raise ValueError(f'{path}: {reason}')

    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        reject('invalid PNG signature')
    offset, header, ended = 8, None, False
    compressed = bytearray()
    while offset < len(data):
        if offset + 12 > len(data):
            reject('truncated PNG chunk')
        length, kind = struct.unpack_from('>I4s', data, offset)
        end = offset + 8 + length
        if end + 4 > len(data):
            reject('truncated PNG chunk data')
        payload = data[offset + 8:end]
        if zlib.crc32(data[offset + 4:end]) != struct.unpack_from('>I', data, end)[0]:
            reject(f'invalid PNG {kind!r} checksum')
        if header is None and kind != b'IHDR':
            reject('PNG must start with IHDR')
        if kind == b'IHDR':
            if header is not None or length != 13:
                reject('invalid PNG IHDR')
            header = struct.unpack('>IIBBBBB', payload)
            width, height, depth, colour, compression, filtering, interlace = header
            if depth != 8 or colour != 6 or interlace != 0:
                reject(f'unsupported PNG: bit depth {depth}, colour type {colour}, '
                       f'interlace {interlace}; expected 8-bit RGBA (colour type 6), non-interlaced')
            if not width or not height or compression != 0 or filtering != 0:
                reject('invalid dimensions or unsupported PNG compression/filter method')
        elif kind == b'IDAT':
            compressed.extend(payload)
        elif kind == b'IEND':
            if length:
                reject('invalid PNG IEND')
            ended = True
            break
        elif kind != b'PLTE' and not kind[0] & 32:
            reject(f'unsupported critical PNG chunk {kind!r}')
        offset = end + 4
    if header is None or not ended or not compressed:
        reject('PNG missing IHDR, IDAT or IEND')
    try:
        raw = zlib.decompress(compressed)
    except zlib.error as error:
        reject(f'invalid PNG compressed data: {error}')
    stride = width * 4
    if len(raw) != height * (stride + 1):
        reject('invalid PNG decompressed scanline length')
    pixels = bytearray(height * stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        if filter_type > 4:
            reject(f'unsupported PNG scanline filter {filter_type}')
        row = bytearray(raw[start + 1:start + 1 + stride])
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            up = pixels[(y - 1) * stride + x] if y else 0
            upper_left = pixels[(y - 1) * stride + x - 4] if y and x >= 4 else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = up
            elif filter_type == 3:
                predictor = (left + up) // 2
            else:
                p = left + up - upper_left
                distances = (abs(p - left), abs(p - up), abs(p - upper_left))
                predictor = (left, up, upper_left)[distances.index(min(distances))]
            row[x] = (row[x] + predictor) & 255
        pixels[y * stride:(y + 1) * stride] = row
    return RGBAImage(width, height, pixels)


def adjudicate(base, head):
    metadata = json.loads((base / 'samples.json').read_text(encoding='utf-8'))
    after_metadata = json.loads((head / 'samples.json').read_text(encoding='utf-8'))
    assert len(metadata) == 38 and metadata.keys() == after_metadata.keys(), '38-capture manifest changed'
    rows, changed = [], set()
    for name, info in metadata.items():
        assert info['tolerance'] == after_metadata[name]['tolerance'], 'tolerance changed'
        before, after = read_png(base / (name+'.png')), read_png(head / (name+'.png'))
        assert before.size == after.size
        # Fbos use attachment coordinates even at Retina DPI; screen probes
        # scale with the recorded framebuffer. The in-binary mask also runs
        # independently of any baseline on the Air.
        density = 1 if name.endswith('.fbo') else before.width / 256
        differences, all_differences = [], []
        for y in range(before.height):
            for x in range(before.width):
                delta = max(abs(a-b) for a,b in zip(before.getpixel((x,y)), after.getpixel((x,y))))
                if delta: all_differences.append((x,y))
                if delta > info['tolerance']: differences.append((x,y))
        if differences:
            changed.add(name)
            assert name in ALLOW, f'unexpected red: {name}'
            if name == 'premultImage.fbo':
                assert all(before.getpixel((x,y)) == (64,0,127,255) and after.getpixel((x,y)) == (128,0,127,255)
                           for y in range(16,80) for x in range(16,80))
                assert before.getpixel((32,32)) == (64,0,127,255)
                assert after.getpixel((32,32)) == (128,0,127,255)
                assert all(16 <= x < 80 and 16 <= y < 80 for x,y in all_differences), 'premult diff outside texture'
            else:
                # Old/new point footprints, with the proposal's one-pixel edge
                # allowance. A model scale moves centers; it never scales size.
                centers = [(24,24),(80,64),(72.380,68.777),(133.398,134.203)]
                old_extent = 12 * before.width / 256
                new_extent = 12 * density
                footprints = [(cx,cy,old_extent if index < 2 else new_extent) for index,(cx,cy) in enumerate(centers)]
                assert all(any(abs(x+.5-cx*density) <= extent/2+1 and
                               abs(y+.5-cy*density) <= extent/2+1 for cx,cy,extent in footprints)
                           for x,y in all_differences), f'point diff outside footprints: {name}'
                for position, expected in [((24,24),(0,0,0)),((67,69),(0,255,0)),((133,134),(255,0,255))]:
                    xy = tuple(int(v*density) for v in position)
                    assert after.getpixel(xy)[:3] == expected, (name,xy)
                for channel in [0,1]:
                    old_mask = [(x,y) for y in range(before.height) for x in range(before.width) if before.getpixel((x,y))[channel] > 250]
                    assert len(old_mask) == int(old_extent)**2
                    mask = [(x,y) for y in range(after.height) for x in range(after.width) if after.getpixel((x,y))[channel] > 250]
                    extent = int(12*density)
                    assert len(mask) == extent*extent
                    assert max(x for x,y in mask)-min(x for x,y in mask)+1 == extent
                    assert max(y for x,y in mask)-min(y for x,y in mask)+1 == extent
                if name.endswith('.fbo'):
                    assert all(pixel[3] == 255 for pixel in after.getdata())
        rows.append({'capture':name,'red':len(differences),'diff':name+'.diff.png'})
    assert changed == ALLOW, f'three-name allowlist mismatch: {changed}'
    assert read_png(head/'premultImage.screen.png').getpixel((32,32))[:3] == (128,0,127)
    return {'verdict':'PASS','captures':rows,'changed':sorted(changed),'unchanged':len(rows)-len(changed),
            'checks':['manifest/tolerances','three-name allowlist','premult samples/region','point samples/masks/regions','Fbo alpha']}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base',type=Path); parser.add_argument('head',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    report=adjudicate(args.base,args.head)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    for row in report['captures']: print(row['capture'],row['red'])
    print('PASS: exactly three approved changes; 35 unchanged captures')
if __name__=='__main__': main()
