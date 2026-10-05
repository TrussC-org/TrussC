"""Create a truncated or bad-packet copy of the bundled HAP test movie."""
from pathlib import Path
import struct
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("destination", type=Path)
mode = parser.add_mutually_exclusive_group()
mode.add_argument("--bad-packet", action="store_true")
mode.add_argument("--bad-packets", action="store_true",
                  help="corrupt two consecutive video packets for warning throttling")
args = parser.parse_args()
source, destination = args.source, args.destination
data = bytearray(source.read_bytes())


def u32(offset):
    return struct.unpack_from(">I", data, offset)[0]


def children(start, end):
    while start + 8 <= end:
        size = u32(start)
        if size < 8 or start + size > end:
            break
        yield start, bytes(data[start + 4:start + 8])
        start += size


def walk(start, end):
    for offset, kind in children(start, end):
        yield offset, kind
        if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl"):
            yield from walk(offset + 8, offset + u32(offset))


atoms = list(walk(0, len(data)))
mdat = next(offset for offset, kind in atoms if kind == b"mdat")
moov = next(offset for offset, kind in atoms if kind == b"moov")
# The bundled fixture's first track is video, with constant-size samples.
stco = next(offset for offset, kind in atoms if kind == b"stco")
offsets = [u32(stco + 16 + 4 * i) for i in range(u32(stco + 12))]
stsz = next(offset for offset, kind in atoms if kind == b"stsz")
size = u32(stsz + 12)
assert size and len(offsets) >= 3
if args.bad_packet or args.bad_packets:
    # Preserve container/sample lengths and the other packets. An invalid
    # compressor with a valid texture format reaches HapDecode/FFmpeg decode.
    count = 2 if args.bad_packets else 1
    assert len(offsets) > 2 + count  # Keep valid frames after the corrupt burst.
    for offset in offsets[2:2 + count]:
        data[offset + 3] &= 0x0F
    destination.write_bytes(data)
    raise SystemExit(0)

cut = offsets[2] + size // 2
moov_size = u32(moov)
assert moov + moov_size == len(data)
for offset, kind in atoms:
    if kind == b"stco":
        for i in range(u32(offset + 12)):
            entry = offset + 16 + 4 * i
            struct.pack_into(">I", data, entry, u32(entry) + moov_size)
destination.write_bytes(data[:mdat] + data[moov:] + data[mdat:cut])
