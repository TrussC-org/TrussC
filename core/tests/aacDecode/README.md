Linux GStreamer regression for #453. A truncated M4A and deterministic random
bytes must each return `DecodeFailed` with one error naming the file and the
GStreamer error. A valid M4A must still decode, including after failed loads.
The fixture is mono; faad (used when gst-libav is absent, as on CI) decodes it
as stereo, so the test accepts one or two channels.
No display or audio output device is needed. Other platforms skip the test.

Run the binary under an external process timeout so a regression cannot hang:

```sh
timeout 60 core/tests/allCoreTests/bin/allCoreTests aacDecode
```

The core test runner also has an external subprocess timeout. The test has no
elapsed-time assertions and the decoder has no overall time limit.

`src/toneAac.h` embeds a generated 440 Hz mono tone (GStreamer 1.24.2, libav
plugin 1.24.1). No encoder or external fixture files are needed at test runtime.
The fixture was generated with:

```sh
gst-launch-1.0 -q audiotestsrc num-buffers=4 samplesperbuffer=1024 freq=440 volume=0.5 ! \
  'audio/x-raw,format=F32LE,rate=44100,channels=1' ! avenc_aac ! aacparse ! \
  mp4mux ! filesink location=tone.m4a
```

The first 128 bytes of this fixture form the truncated input: the file type and
start of the media data remain, but the rest and the movie metadata are absent.
