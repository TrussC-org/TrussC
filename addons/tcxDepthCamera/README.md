# tcxDepthCamera

A **unified interface** for depth / point-cloud cameras in TrussC — Orbbec,
Kinect, Xtion, RealSense, LiDAR, and so on.

> 🚧 **Pre-1.0.** Official addon, but the interface may still change until it has
> been validated against a real backend (`tcxOrbbec`). Pin a commit if you depend
> on it before then.

The thing you actually use a depth camera for is its **depth frame**. So that is
the centre of this addon: a canonical, device-independent `DepthFrame` that the
`DepthCamera` base owns and hands you. Once you can touch that frame, everything
else — deprojection, point clouds, color mapping, threading, (later) recording —
is the part you wanted abstracted away, and the base does it for you.

```cpp
#include <tcxDepthCamera.h>
using namespace tcx;

shared_ptr<DepthCamera> cam = make_shared<Orbbec>(serial);   // from tcxOrbbec
cam->setThreaded(true);
cam->setup();
// ...
cam->update();
if (cam->isFrameNew()) {
    const DepthFrame& f = cam->currentFrame();   // the raw frame, if you want it
    Mesh cloud = cam->toMesh({.colors = true});  // ...or the convenience layer
    cloud.draw();
}
```

Swap `Orbbec` for any other backend and the rest of your code is unchanged.

## The canonical `DepthFrame`

Every backend, whatever its underlying tech, produces the same frame:

```cpp
struct DepthFrame {
    int   w, h;
    float depthScale;            // meters per depth unit (RGBD: 0.001 = mm)
    vector<uint16_t> depth;      // w*h, 0 = invalid
    vector<Vec3>     world;      // optional precomputed world (m); empty -> deproject
    Pixels color;                // optional RGBA, registered to depth
    Pixels ir;                   // optional active brightness
    DepthIntrinsics intrinsics;
    double timestamp;
};
```

- **Depth is `uint16` + `depthScale`.** RGBD cameras are natively uint16 mm
  (`depthScale = 0.001`), which keeps frames compact (good for recording). The
  meter-based API is unchanged: `getDistanceAt()` returns `depth[i] * depthScale`.
  Long-range LiDAR can use a coarser scale (e.g. `0.01` = cm, ~655 m).
- **Color and IR live in the frame** (registered to the depth resolution), since
  essentially every RGBD camera has them. Query with `hasColor()` /
  `hasInfrared()`. Truly device-specific data (raw stereo pair, confidence) stays
  on capability interfaces.
- **`world` is optional.** If a backend has an accurate, distortion-aware
  point-cloud transform (e.g. Azure Kinect's), it fills `world` and the base uses
  it; otherwise the base deprojects from `intrinsics`.

## Architecture

```
DepthCamera (concrete base)
  ├─ owns a triple-buffered DepthFrame
  ├─ setup / update / close / isFrameNew / isColorFrameNew / setThreaded
  ├─ getDistanceAt / getWorldCoordinateAt / getColorPixels / getColorAt
  ├─ currentFrame()        <- the raw canonical frame
  └─ toMesh(DepthMeshOptions)

Backend implements only:
  bool openDevice();  void closeDevice();  StreamFreshness captureInto(DepthFrame&);

Capability interfaces (optional, device-specific, composed via inheritance):
  IStereoRaw       raw left/right IR pair + baseline
  IConfidenceMap   per-pixel confidence / amplitude (ToF)
```

### Threading is built in

The base owns a background grabber thread, a mutex, and the triple-buffer
hand-off, so a backend never writes any of that. The worker fills the *capture*
frame, `update()` promotes it to *front*, and the getters read *front* — which is
mutated only inside `update()` on the calling thread, so **getters need no
locking**; the mutex is held only for two O(1) pointer swaps, never during device
I/O or per-pixel reads. `setThreaded(false)` captures inline. Call before `setup()`.

### Design decisions

- **The base owns the frame.** A depth map is the native primitive of every RGBD
  camera, so the base holds a canonical one rather than making each backend
  reimplement getters. This makes `toMesh()` read the frame directly (no
  per-pixel virtual dispatch), keeps backends tiny, and makes recording trivial.
- **Units: float meters** at the API surface (`getDistanceAt` /
  `getWorldCoordinateAt`), uint16 + scale in storage.
- **Capabilities don't inherit `DepthCamera`** → composed via multiple
  inheritance with no diamond. Discover with `as<>()` / `is<>()`.
- **Sensor kind (`getSensorType()`) is informational**, never a branch for "which
  data exists" — use `hasColor()` / `as<Cap>()` for that.
- **Per-stream freshness**: `isFrameNew()` (depth) and `isColorFrameNew()` can
  differ, since streams may arrive at different rates.

## Point clouds: `toMesh()`

```cpp
Mesh a = cam->toMesh();                              // positions only
Mesh b = cam->toMesh({.texCoords = true});           // + UVs into the color image
Mesh c = cam->toMesh({.colors = true});              // + baked per-vertex RGB
Mesh d = cam->toMesh({.colors = true, .step = 2});   // decimated
```

`Points` mode, invalid points skipped, attributes built in one pass so they stay
aligned. `texCoords`/`colors` need `hasColor()`. Override `toMesh()` in a backend
for a bulk / GPU path if needed.

## No hardware? `SyntheticDepthCamera`

Ships a software-only `DepthCamera` that fills the frame with an animated scene
(a rippling wall + bobbing sphere) and a depth-keyed color image — no device:

```cpp
auto cam = make_shared<SyntheticDepthCamera>(640, 480);
cam->setup();
cam->update();
cam->toMesh({.colors = true}).draw();
```

Use it to develop/test on any platform (it powers the examples and CI), run
point-cloud demos on camera-less machines, and as a reference backend. See
`example-synthetic/`.

## Implementing a backend

A backend depends on `tcxDepthCamera`, inherits `DepthCamera`, and fills the
frame. That's it — no getters to write:

```cpp
class Orbbec : public DepthCamera {
protected:
    bool openDevice() override;   // open the SDK device
    void closeDevice() override;
    StreamFreshness captureInto(DepthFrame& f) override {
        // f.w/h, f.depthScale, f.depth (mm), f.color (registered), f.intrinsics, f.timestamp
        // return which streams refreshed
    }
    DepthSensorType getSensorType() const override { return DepthSensorType::Stereo; }
};
```

Add capability interfaces for device-specific extras:

```cpp
class Gemini : public DepthCamera, public IStereoRaw { ... };
```

**Produce a complete frame:** if a stream did not refresh this tick (e.g. color
runs slower than depth), carry its latest data forward into the frame — the
buffers rotate, so the destination does not retain last tick's contents. SDKs
configured for synchronized capture deliver both every time, so this is
automatic; otherwise keep the latest of each stream as a member.

## Capability discovery

```cpp
if (auto* s = as<IStereoRaw>(*cam)) {
    auto& left = s->getLeftInfrared();
    float b    = s->getBaseline();
}
if (isStereoCam(*cam)) { ... }
if (isToF(*cam))       { ... }
```

`as<>()` / `is<>()` accept a reference, raw pointer, or `shared_ptr`. (Common
streams — color, IR — are not capabilities; use `hasColor()` / `hasInfrared()`.)

## Roadmap

- **Virtual source family.** Because the frame is the contract, the source is
  swappable. `SyntheticDepthCamera` is the first non-hardware source; the same
  shape gives **playback** (a recording replayed *as* a `DepthCamera` — its
  `captureInto` deserializes frames) and **network** (a remote camera received as
  one), plus the sink side — a recorder/sender that consumes any `DepthCamera`
  via `currentFrame()`. Since the base already owns a serialization-ready
  `DepthFrame`, the recorder is camera-agnostic; the win over vendor recorders is
  our own codec (RVL / zstd for depth, jpeg/h264 for color) and storing only
  depth+color+intrinsics. Needs a serialized `DepthFrame` format; deferred until
  live capture is proven.
- **Device registry / factory.** Once there are ≥2 backends, a
  `DepthCameraRegistry` (backends self-register an enumerator + factory) for
  cross-backend `listDevices()` and `create(info)`.
- **GPU point-cloud path.** For high-res live clouds, deproject in a shader from
  a depth texture (no CPU mesh). The endgame for `toMesh`-level performance.

## License

MIT. Interface-only (plus the hardware-free `SyntheticDepthCamera`); bundles no
third-party code. See `LICENSES.md`.
