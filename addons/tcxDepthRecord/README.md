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

`.tcdc` = **TrussC Depth Container** — a TLV container (depth, color, and anything
an addon adds).

```
[Header]    magic/version, resolution, intrinsics, depth->color extrinsic,
            codec ids, STREAM MANIFEST (which block types the file contains),
            frameCount, indexOffset (patched on stop)
[Frame] x N timestamp + a sequence of TLV blocks { type:u8, length:u32, value },
            read until the next frame's offset
[Index]     { timestamp, fileOffset } x N   (for seek / scrub)
```

- **TLV blocks + skip-unknown**: each block is Type/Length/Value, so a reader
  parses the types it knows (depth/color) and *skips any it doesn't* by length.
  Addons can add block types (`>= 0x80`, e.g. body / hand tracking) and an
  official player still plays depth/color, ignoring them — forward-compatible.
- **Stream manifest** in the header lists every block type present, so a reader
  knows what's inside up front: `hasBlockType()`, `getBlockTypes()`,
  `hasUnknownBlocks()` ("playable, but contains streams this build can't decode").
- **Intra-only**: every frame is an independent seek target (scrub = index lookup).
- **Depth** = hi/lo byte-plane split + LZ4 (lossless); **color** = LZ4 over the
  native-resolution RGBA (lossless). Via core `tc::compress` (no third-party libs;
  shares the one vendored lz4). Codecs are tagged → QOI / JPEG-XS / zstd later.
- **World** is not stored: recomputed from intrinsics on playback.
- **Record selection**: `start(path, REC_DEPTH)` records depth only; default
  `REC_ALL` (depth + color).

### Extending with custom streams (addons)

Bones / hand tracking / etc. are NOT in the official format. An addon subclasses
the recorder/player and uses the extension hooks:

```cpp
struct BodyRecorder : DepthRecorder {
    void writeExtraBlocks(const DepthCamera& cam) override {
        // serialize as<IBodyTracking>(cam)->getBodies() ...
        writeBlock(0x80, bytes.data(), bytes.size());   // custom block type
    }
};
struct BodyPlayback : PlaybackDepthCamera {  // also implements the addon's IBodyTracking
    bool decodesBlockType(uint8_t t) const override { return t == 0x80; }
    bool readExtraBlock(uint8_t t, const uint8_t* d, uint32_t n, double ts) override { ... }
};
```

The official player still plays such a file's depth/color (the `0x80` block is
skipped); only the addon's player decodes the extra stream.

## Notes / roadmap

- Playback loops by default (`setLoop(false)` to stop at the end). Frames are
  served one-per-`update()`; real-time pacing from timestamps is a future option.
- Playback checks each block's sizes against the frame before decoding. A block
  that doesn't match is skipped (its stream isn't marked new for that frame),
  and the first one is logged as a warning.
- Infrared is not yet serialized (depth + color in v1; IR/custom via blocks later).
- Endianness: little-endian host assumed (no byte-swap in v1).

See `example-record/` (records the SyntheticDepthCamera, then plays it back).

## License

MIT. Header-only; see `LICENSES.md`.
