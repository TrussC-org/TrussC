# tcxHap tests

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
