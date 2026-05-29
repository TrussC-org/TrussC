# tcxDepthRecord

Record and replay depth recordings (`.tcdc`) for any
[`tcxDepthCamera`](../tcxDepthCamera).

> 🚧 Pre-1.0 (the `.tcdc` format may still change).

Because every depth camera produces the same canonical `DepthFrame`, recording
is camera-agnostic and a recording replays through the **same `DepthCamera`
interface** as live hardware — swap the constructor and the rest of your app is
unchanged.

```cpp
#include <tcxDepthRecord.h>
using namespace tcx;

// --- record ---
DepthRecorder rec;
rec.start("clip.tcdc");
cam->update();
if (cam->isFrameNew()) rec.record(*cam);   // appends cam.currentFrame()
rec.stop();                                 // writes the seek index + finalizes

// --- replay (same interface as a live camera) ---
shared_ptr<DepthCamera> p = make_shared<PlaybackDepthCamera>("clip.tcdc");
p->enableDepth(); p->enableColor();
p->setup();
p->update();
if (p->isFrameNew()) p->toMesh({.colors = true}).draw();
```

## The `.tcdc` format

```
[FileHeader]   magic/version, resolution, depthScale, intrinsics, stream flags,
               codec ids, frameCount, indexOffset (patched on stop)
[Frame] x N    timestamp + present-stream blocks, each independently compressed
[Index]        { timestamp, fileOffset } x N   (for seek / scrub)
```

- **Intra-only**: every frame is independently (de)compressed, so any frame is a
  seek target (scrubbing is just an index lookup). No inter-frame deltas.
- **Depth**: hi/lo byte-plane split + LZ4 (`HiloLZ4`) — lossless, fast. The high
  bytes are nearly constant across a depth image so LZ4 crushes them; invalid
  (0) pixels vanish in both planes.
- **Color**: LZ4 over the (depth-registered) RGBA image — lossless.
- **World** is *not* stored: `getWorldCoordinateAt()` recomputes it from the
  stored intrinsics (distortion-aware), keeping files small.
- Codecs are tagged in the header, so QOI / JPEG-XS / zstd can be added later
  without changing the format.

Compression goes through TrussC core's `tc::compress`, so this addon links no
third-party libraries (and shares the single vendored lz4 — no duplicate
symbols).

## Notes / roadmap

- Playback loops by default (`setLoop(false)` to stop at the end). Frames are
  served one-per-`update()`; real-time pacing from timestamps is a future option.
- Infrared and the SDK-precomputed `world` image are not yet serialized (depth +
  color in v1).
- Endianness: little-endian host assumed (no byte-swap in v1).

See `example-record/` (records the SyntheticDepthCamera, then plays it back).

## License

MIT. Header-only; see `LICENSES.md`.
