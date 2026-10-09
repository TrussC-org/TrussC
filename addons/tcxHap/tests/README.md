# tcxHap tests

Snappy integration checks encode repeated BC1 blocks as single-chunk and
four-chunk HAP frames, verify compression actually reduced their size, and
compare both `HapDecoder` decode paths with the original texture bytes.
These checks run headlessly as part of the default test mode.

Video regressions (#283) check BC1, BC3 (Hap Alpha and HAP-Q), and BC7
decoding at 6x5, 6x8, 8x5, 8x8, and 1x1. Hand-built blocks have known pixel
values; both a 64-byte guard region and an exact-sized allocation are checked.
Run these tests under AddressSanitizer with the BC decoder implementation
instrumented as well.

BC1, BC3, BC7, and BC4 (Hap Q Alpha) also decode known blocks at every input
byte offset from 0 through 7, covering bcdec 0.985's unaligned-input fix.
Run with AddressSanitizer and UndefinedBehaviorSanitizer to check input
bounds and alignment, including the decoder implementation.

HAP-Q draw setup checks transformed quad corners, UVs, tint and alpha,
column-major MVP uniforms, an Fbo-sized projection, and a perspective camera.
These are headless setup checks; real HAP-Q playback in a Node, an Fbo, and a
3D camera still needs a manual check on each platform.

Headless console test (no window, no GPU, no audio device). Checks that PCM
audio in HAP movies decodes in the right byte order (#419): each movie is
parsed with `MovParser` and decoded with `loadPcmTrack()`, the path
`HapPlayer` uses for PCM audio.

- `sine_sowt.mov` (`sowt`, 16-bit little-endian) is the reference: 1 ch,
  48000 Hz, 24000 frames, a -6 dB sine (peak ~0.5, rms ~0.35).
- `sine_twos.mov` (`twos`, 16-bit big-endian) matches it exactly.
- `sine_fl32be.mov` (`fl32`, big-endian; ffmpeg writes an `enda` atom set to 0)
  and `sine_fl32le_enda.mov` (`fl32` with an `enda` atom set to 1,
  little-endian) match it within 1/32768.
- A copy of `sine_fl32be.mov` with its `enda` atom renamed (made at run time in
  the temp folder) has no `enda` at all, is read as big-endian (the QuickTime
  default for `fl32`), and matches too.
- A copy of `sine_fl32be.mov` whose `enda` (set to 1) sits in a `wave` nested
  in the `wave` extension (made at run time) is read as big-endian, since
  `wave` is read one level deep, and matches too.

It also checks how `MovParser` handles sample tables (#343). Copies of
`sine_sowt.mov` are made at run time in the temp folder, each with one field
changed, and parsed with `MovParser::open()`:

- The unchanged file has 5 video samples and 24000 audio frames.
- A copy whose video `stsz` is written as a variable-size table parses to the
  same samples; the `stsz` cases below use it.
- Video `stsz` entry count `0xFFFFFFF0`: the video track is skipped, the audio
  track is kept.
- Video `stts` entry count `0xFFFFFFF0`: `stts` is ignored, the video track is
  unchanged.
- Audio constant-size `stsz` count `0xFFFFFFF0`: the audio track has the 24000
  frames that `stsc` / `stco` place.
- Video `mdia` larger than its `trak`: the video track is skipped.
- One video `stsz` entry `0xFFFFFFF0`: `readSample()` returns false for that
  sample without sizing the buffer, and the next sample reads.
- `moov` moved in front of `mdat` and the file cut inside `mdat`: the video
  sample and audio frame counts are unchanged, a sample past the cut fails to
  read, and samples 0 and 1 read after that.

Each `open()` is checked by its resulting tracks, counts and sample data.
Pass/fail does not depend on wall-clock execution time.

PCM sample tables, sound descriptions and the playback clock (#291).
Audio-only movies are built in memory (`ftyp`, `mdat`, `moov` with one `soun`
track) and written to the temp folder:

- `sine_sowt.mov` (v0 `sowt`): the constant-size PCM track is stored as 5
  chunk entries holding 24000 frames, and decodes as above.
- v0 `sowt`, 2 ch, 16 bit, 48000 Hz, 60 s (`stsz` size 4, count 2880000,
  48000 frames per chunk, 60 chunks, zero-filled `mdat`): 60 entries,
  2880000 frames; `loadPcmTrack()` decodes all frames as stereo silence.
- v2 `lpcm` at 96000.0 Hz, 2 ch: 16-bit little-endian, 16-bit big-endian and
  32-bit float (both byte orders) give 96000 Hz, 2 ch and the right bit depth and byte order,
  and decode to known sample values.
- v2 `lpcm` 24-bit, 64-bit float, non-interleaved and unsigned: reported as
  not supported; `loadPcmTrack()` returns false with a warning (`HapPlayer`
  loads without audio).
- v1 `sowt` and `twos` with `stsz` size 1: the PCM byte count is frames * 4
  and the samples decode to known values.
- Variable-size `sowt` keeps one entry per sample and decodes known values.
- v1/v2 descriptions with padded packets: the audio track is skipped with
  a warning because the decoder requires packed frames.
- A v2 sample rate rounding past the decoder's `int` range is unsupported.
- v0 rate 22254.5454 Hz (16.16): read as 22255 Hz.
- `stepPlaybackClock()`: with the audio clock 1% faster than the wall clock
  and the audio position moving in 512-frame blocks, 10000 steps of a looping
  10 s video stay within 20 ms of audio without hard re-sync; irregular
  frame deltas also stay below the re-sync threshold. A continuous 1% faster
  audio clock converges to a stable lag below 3 ms. Wall-clock `dt * speed`
  slews toward audio with a
  0.25 s time constant; larger differences trigger hard re-sync according
  to the threshold. A nonpositive threshold disables only hard re-sync.
  Forward speed 2 follows audio; without audio, and in reverse, time moves
  by `dt * speed` and wraps at the ends.
- Coarse audio positions update every 2048/48000 s (42.7 ms) or 480/48000 s
  (10 ms), including one buffer of mixed-audio lead. Over 10000 supplied
  `dt = 1/60` steps, video time increases every step by less than 20 ms,
  without hard re-sync. The 2048-frame case includes repeated positions.
- Synthetic 2 s audio in a 5 s video: when audio stops at 2 s, supplied
  0.5 s deltas carry video to its end. With looping, video wraps to 0 s,
  restarts and resyncs the synthetic audio, then slews toward it again.
  No wall-clock bounds or audio device are used.

To check `MovParser` against ffmpeg's own output (not part of the test), for
example:

```sh
ffmpeg -f lavfi -i testsrc=size=320x240:rate=30 -f lavfi -i sine=frequency=440:sample_rate=96000 -ac 2 -t 5 -c:v hap -c:a pcm_s16le hap_96k.mov
ffmpeg -f lavfi -i testsrc=size=640x360:rate=30 -f lavfi -i sine=frequency=440:sample_rate=48000 -ac 2 -t 600 -c:v hap -c:a pcm_s16le long_pcm.mov
```

The first has a v2 `lpcm` description (96000 Hz, flags 0xc); the second a v0
`sowt` track of 28800000 frames in 18000 chunk entries.

## Test files

The files in `bin/data/` were made with ffmpeg 6.1.1 (with the `hap`
encoder): a 0.5 s, 440 Hz sine at -6 dBFS, mono 48 kHz, with a 64x64 gray
HAP video track at 10 fps. To regenerate them, from `bin/data/`:

```sh
ffmpeg -y -f lavfi -i "color=c=gray:s=64x64:r=10:d=0.5" -f lavfi -i "aevalsrc=pow(10\,-6/20)*sin(2*PI*440*t):s=48000:c=mono:d=0.5" -c:v hap -c:a pcm_s16le -map_metadata -1 -fflags +bitexact -t 0.5 sine_sowt.mov
ffmpeg -y -f lavfi -i "color=c=gray:s=64x64:r=10:d=0.5" -f lavfi -i "aevalsrc=pow(10\,-6/20)*sin(2*PI*440*t):s=48000:c=mono:d=0.5" -c:v hap -c:a pcm_s16be -map_metadata -1 -fflags +bitexact -t 0.5 sine_twos.mov
ffmpeg -y -f lavfi -i "color=c=gray:s=64x64:r=10:d=0.5" -f lavfi -i "aevalsrc=pow(10\,-6/20)*sin(2*PI*440*t):s=48000:c=mono:d=0.5" -c:v hap -c:a pcm_f32be -map_metadata -1 -fflags +bitexact -t 0.5 sine_fl32be.mov
ffmpeg -y -f lavfi -i "color=c=gray:s=64x64:r=10:d=0.5" -f lavfi -i "aevalsrc=pow(10\,-6/20)*sin(2*PI*440*t):s=48000:c=mono:d=0.5" -c:v hap -c:a pcm_f32le -map_metadata -1 -fflags +bitexact -t 0.5 sine_fl32le_enda.mov
```

Check the audio fourcc with
`ffprobe -v error -show_entries stream=codec_name,codec_tag_string -of compact <file>`
(`sowt`, `twos`, `fl32`, `fl32`). ffprobe does not show `enda`; the `fl32`
files carry it in the sound description's `wave` extension (`00 00 00 0a 65
6e 64 61 00 01` for little-endian, `... 00 00` for big-endian).

## Running

CI (`examples/build_all.py --addon-tests-only`) builds and runs this; a
non-zero exit fails the job. Locally:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```

The optional `tests --playback-errors` mode needs a graphics context (Xvfb is
sufficient on Linux). It checks fatal truncated-sample reads and
`HapResult_Buffer_Too_Small`: one event and one error log, stopped state and
retained texture. A separate single `HapResult_Bad_Frame` fixture checks a
warning, continued playback and decoding of the following frame.

The optional `tests --shader-failure` mode also runs under Xvfb. It builds a
4x4 HAP-Q movie locally and reserves the remaining shader slots to force a
real shader load failure. Over 64 draws it checks one load attempt, one
HapPlayer error explaining the fallback, no separate fallback warning, and
raw texture pixels read back from an Fbo. It also checks independent players,
one new attempt after `load()`, no retry merely because shader slots become
available, successful recovery on the next `load()`, and preservation of
shader state when moving a player. The existing lower-level Shader/Sokol
failure diagnostics are left intact.
