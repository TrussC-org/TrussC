#!/usr/bin/env python3
"""Generate native-SDK companion fixtures locally; requires ffmpeg with libx264.

The Linux harness itself uses libavcodec/libavformat, not the CLI. Its exact
MPEG-2/Matroska oversized-packet fixture is exported by --write-fixtures.
"""
import argparse
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    def run(*command):
        command = [args.ffmpeg, '-hide_banner', '-loglevel', 'warning', '-y', *map(str, command)]
        print(shlex.join(command), flush=True)
        subprocess.run(command, cwd=args.output, check=True)

    def video(name, size, frames, rate=25, raw=False):
        run('-f', 'lavfi', '-i', f'color=c=red:s={size}:r={rate}', '-frames:v', frames,
            '-c:v', 'libx264', '-preset', 'ultrafast', '-threads', 1, '-g', 1,
            '-pix_fmt', 'yuv420p', '-level:v', '6.2',
            *(['-x264-params', 'repeat-headers=1', '-f', 'h264'] if raw else []), name)

    video('small.mp4', '32x16', 8)
    video('limit.mp4', '8192x4096', 3)
    video('8k.mp4', '7680x4320', 3)
    video('oversized-header.mp4', '8194x2', 1)
    video('long.mp4', '32x16', 4000, 1000)
    video('first.h264', '32x16', 2, raw=True)
    video('second.h264', '64x32', 2, raw=True)
    (args.output / 'changed.h264').write_bytes(
        (args.output / 'first.h264').read_bytes() + (args.output / 'second.h264').read_bytes())
    run('-r', 25, '-i', 'changed.h264', '-c:v', 'copy', '-tag:v', 'avc3', 'changed.mp4')
    run('-f', 'lavfi', '-i', 'sine=frequency=997:sample_rate=48000', '-t', 2,
        '-c:a', 'aac', '-b:a', '192k', '-ac', 2, 'audio-seed.aac')
    for label, size in [('below', 63 * 1024 * 1024), ('above', 65 * 1024 * 1024)]:
        run('-stream_loop', -1, '-i', 'audio-seed.aac', '-c:a', 'copy', '-fs', size, f'audio-{label}.aac')
        track = args.output / f'audio-{label}.aac'
        # ADTS is also the output representation of AAC extraction. Leave
        # ample room for -fs to finish its current whole packet.
        assert (track.stat().st_size < 64 * 1024 * 1024) == (label == 'below')
        run('-i', 'small.mp4', '-i', track.name, '-map', '0:v:0', '-map', '1:a:0',
            '-c', 'copy', f'audio-{label}.mp4')
    # A padded-width, asymmetric image makes row orientation easy to inspect.
    run('-f', 'lavfi', '-i', 'testsrc2=s=34x18:r=25', '-frames:v', 8,
        '-c:v', 'rawvideo', '-pix_fmt', 'bgr24', '-threads', 1, 'rows.avi')


if __name__ == '__main__':
    main()
