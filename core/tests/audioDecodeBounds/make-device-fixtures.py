#!/usr/bin/env python3
"""Generate local audio decoder fixtures using an already installed ffmpeg."""

import argparse
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess

MIB = 1024 * 1024


def moov_size(path):
    """Read top-level ISO BMFF boxes without assuming where moov is stored."""
    with path.open("rb") as source:
        end = path.stat().st_size
        while source.tell() < end:
            start = source.tell()
            size, kind = struct.unpack(">I4s", source.read(8))
            if size == 1:
                size = struct.unpack(">Q", source.read(8))[0]
            elif size == 0:
                size = end - start
            if size < source.tell() - start or start + size > end:
                raise ValueError(f"Invalid box in {path}")
            if kind == b"moov":
                return size
            source.seek(start + size)
    raise ValueError(f"No moov box in {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="fixture output directory")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="existing ffmpeg executable")
    args = parser.parse_args()
    ffmpeg = shutil.which(args.ffmpeg)
    if not ffmpeg:
        parser.error("ffmpeg was not found; supply --ffmpeg with an existing executable")
    args.output.mkdir(parents=True, exist_ok=True)
    fixtures = []

    def encode(name, codec_args, duration=1):
        path = args.output / name
        command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                   "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
                   "-t", str(duration), *codec_args, str(path)]
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        if not 0 < path.stat().st_size < 64 * MIB:
            raise ValueError(f"Unexpected encoded size: {path}")
        return path

    def record(path, expect, **extra):
        # Expectations come from the input parameters, never from the decoder under test.
        duration = extra.pop("duration", 1)
        rate = extra.pop("sample_rate", 48000)
        frames = extra.pop("frames", duration * rate)
        # Native decoders differ in whether they trim encoder priming/end padding.
        tolerance = 4096 if path.suffix in (".m4a", ".mp3") else 0
        fixtures.append(dict(file=path.name, expect=expect, encoded_bytes=path.stat().st_size,
                             channels=2, sample_rate=rate, frames=frames,
                             frame_tolerance=tolerance, **extra))
        if path.suffix == ".m4a":
            # The Linux GStreamer AAC path explicitly resamples to 44.1 kHz.
            fixtures[-1]["linux_pcm"] = dict(sample_rate=44100, frames=duration * 44100)

    record(encode("small.m4a", ["-c:a", "aac", "-b:a", "32k"]), "load")
    # 18,750 AAC packets give a real sample-size table larger than 64 KiB.
    large = encode("large-moov.m4a", ["-c:a", "aac", "-b:a", "32k"], 400)
    size = moov_size(large)
    if size <= 64 * 1024:
        raise ValueError(f"moov must exceed 64 KiB, got {size}")
    record(large, "load", duration=400, moov_bytes=size)
    # Some ffmpeg builds (e.g. Homebrew) ship only the native, experimental
    # `vorbis` encoder; prefer libvorbis and record which one was used.
    encoders = subprocess.run([ffmpeg, "-hide_banner", "-encoders"], check=True,
                              capture_output=True, text=True).stdout
    if any(line.split()[1:2] == ["libvorbis"] for line in encoders.splitlines()):
        vorbis, vorbis_args = "libvorbis", ["-c:a", "libvorbis", "-q:a", "2"]
    else:
        vorbis, vorbis_args = "vorbis", ["-c:a", "vorbis", "-strict", "experimental"]
    print(f"ogg encoder: {vorbis}", flush=True)
    for name, codec, extra in [("small.mp3", ["-c:a", "libmp3lame", "-b:a", "64k"], {}),
                               ("small.flac", ["-c:a", "flac"], {}),
                               ("small.wav", ["-c:a", "pcm_s16le"], {}),
                               ("small.ogg", vorbis_args, {"encoder": vorbis})]:
        record(encode(name, codec), "load", **extra)
    # Preserve a valid M4A prefix, extending locally without another encode.
    oversized = args.output / "over-encoded.m4a"
    shutil.copyfile(args.output / "small.m4a", oversized)
    with oversized.open("r+b") as output:
        output.truncate(64 * MIB + 1)
    record(oversized, "reject-encoded")
    # A complete PCM WAV with a declared rate just above the admitted range.
    rate, channels, frames = 384001, 2, 32
    data = bytes(frames * channels * 2)
    invalid = args.output / "over-format.wav"
    invalid.write_bytes(struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data),
                                   b"WAVE", b"fmt ", 16, 1, channels, rate,
                                   rate * channels * 2, channels * 2, 16,
                                   b"data", len(data)) + data)
    record(invalid, "reject-format", sample_rate=rate, frames=frames)
    # Float PCM is 900 * 48000 * 2 * 4 = 345,600,000 bytes (>256 MiB).
    long = encode("long-notice.m4a", ["-c:a", "aac", "-b:a", "32k"], 900)
    record(long, "notice", duration=900, decoded_pcm_bytes=900 * 48000 * 2 * 4)
    (args.output / "manifest.json").write_text(json.dumps(fixtures, indent=2) + "\n", encoding="utf-8")
    for fixture in fixtures:
        print(f"{fixture['file']}: {fixture['expect']} ({fixture['encoded_bytes']} encoded bytes)")
    print(f"large-moov.m4a: moov={size} bytes (>65536)")


if __name__ == "__main__":
    main()
