# tcxDepthCamera

A **unified interface** for depth / point-cloud cameras in TrussC — Orbbec,
Kinect, Xtion, RealSense, LiDAR, and so on.

> 🚧 **Pre-1.0.** This is an official addon, but the interface may still change
> until it has been validated against a real backend (`tcxOrbbec`). Pin a commit
> if you depend on it before then.

> ⚠️ **This addon is interface-only.** It ships *no* device drivers and links
> *no* vendor SDKs. On its own it does nothing. Its job is to define one common
> contract so that application code can drive any depth camera through a single
> base type, and so that concrete camera addons (e.g. `tcxOrbbec`) only have to
> fill in the device-specific bits.

## Why

Depth cameras are wildly different under the hood — structured light, stereo,
time-of-flight, LiDAR — and expose different extras (RGB, IR, raw stereo pair,
confidence maps, different optics). But the thing you usually *want* is the
same: `update()` and get a point cloud. `tcxDepthCamera` captures exactly that
common part as an interface, and pushes the differences out to small optional
"capability" interfaces, so device variety doesn't leak into your app loop.

```cpp
#include <tcxDepthCamera.h>
using namespace tcx;

shared_ptr<DepthCamera> cam = make_shared<Orbbec>(serial);   // from tcxOrbbec
cam->setup();
// ...
cam->update();
if (cam->isFrameNew()) {
    Mesh cloud = cam->toMesh({.colors = true});
    cloud.draw();
}
```

Swap `Orbbec` for any other implementation and the rest of your code is
unchanged.

## Architecture

```
DepthCamera (base contract)          <- everyone implements this
  ├─ setup / update / close / isFrameNew
  ├─ getDistanceAt(x,y)        -> float meters
  ├─ getWorldCoordinateAt(x,y) -> Vec3 meters   (default: pinhole deprojection)
  ├─ getDepthIntrinsics()      -> DepthIntrinsics
  ├─ toMesh(DepthMeshOptions)  -> Mesh           (default: single-pass builder)
  └─ setThreaded / isThreaded

Capability interfaces (optional, composed via multiple inheritance):
  IColorStream      RGB(A) color registered to the depth (UVs / per-vertex color)
  IInfraredStream   a single IR / amplitude image
  IStereoRaw        raw left/right IR pair + baseline
  IConfidenceMap    per-pixel confidence / amplitude (typical of ToF)
```

### Design decisions

- **Units are float meters** (`1.0 == 1m`, like glTF / Unity) across the whole
  abstract API. A raw `uint16`-mm depth buffer, when a device exposes one, is a
  device-specific fast path and is deliberately **not** in this interface — that
  keeps it future-proof for LiDAR ranges that would overflow `uint16` mm.
- **Depth is just distance.** Turning it into 3D points or color is a separate,
  on-demand calculation. So the per-pixel getters are the primitives, and
  `toMesh()` is the convenience layer that runs the full conversion in one pass.
- **`toMesh()` builds in a single pass.** Invalid points are skipped *and* the
  optional attributes (texCoords / colors) are attached in the same loop, so
  they stay aligned with the kept vertices. (Decorating a mesh *after* building
  it would lose the depth-pixel mapping once invalid points are dropped.)
- **Capabilities don't inherit `DepthCamera`.** A device multiply-inherits the
  one stateful base plus the pure-interface capabilities it supports, so there
  is no diamond — it behaves like `implements` of several interfaces.
- **Sensor kind is informational only** (`getSensorType()`), never a branch for
  "which methods exist" — use capability queries for that.

## Point clouds: `toMesh()`

`toMesh()` returns a `Mesh` in `PrimitiveMode::Points`.

```cpp
Mesh a = cam->toMesh();                              // positions only (cheap)
Mesh b = cam->toMesh({.texCoords = true});           // + UVs into the color image
Mesh c = cam->toMesh({.colors = true});              // + baked per-vertex RGB
Mesh d = cam->toMesh({.colors = true, .step = 2});   // decimated (every 2nd px)
```

- `texCoords` is cheap (the device already knows the depth→color mapping); draw
  the mesh with the color texture bound. It does **not** bake color in.
- `colors` samples and bakes an RGB per vertex — heavier, but self-contained
  (no texture needed), handy for obj/gltf export.
- `step` thins the cloud for preview / performance.

`texCoords` / `colors` require the camera to also implement `IColorStream`; if
it doesn't, the geometry is still built (a one-time warning is logged).

## Capability discovery

Optional features are queried at runtime, not encoded in the static type:

```cpp
// as<>() does the check AND hands back the usable pointer in one cast:
if (auto* s = as<IStereoRaw>(*cam)) {
    auto& left = s->getLeftInfrared();
    float b    = s->getBaseline();
}

// bool-only forms + readable named wrappers:
if (isStereoCam(*cam)) { ... }
if (hasColor(*cam))    { ... }
if (isToF(*cam))       { ... }   // via getSensorType()
```

`as<>()` / `is<>()` accept a reference, a raw pointer, or a `shared_ptr`.

## Implementing a concrete camera

A new device addon depends on `tcxDepthCamera`, inherits `DepthCamera`, and adds
whatever capability interfaces it supports. At minimum you implement the
lifecycle, the depth accessors and the intrinsics — `getWorldCoordinateAt()` and
`toMesh()` then work for free (override them if the SDK has a faster bulk path):

```cpp
class Orbbec : public DepthCamera, public IColorStream {
public:
    bool  setup() override;
    void  update() override;
    void  close() override;
    bool  isFrameNew() const override;
    int   getWidth()  const override;
    int   getHeight() const override;
    float getDistanceAt(int x, int y) const override;        // meters
    DepthIntrinsics getDepthIntrinsics() const override;
    DepthSensorType getSensorType() const override { return DepthSensorType::Stereo; }

    // IColorStream
    bool isColorFrameNew() const override;   // color is async from depth
    int getColorWidth()  const override;
    int getColorHeight() const override;
    const Pixels& getColorPixels() const override;
    Vec2 getColorTexCoordAt(int dx, int dy) const override;  // 0-1 UV
    // getColorAt() has a default (samples getColorPixels at the UV); override
    // it if the SDK exposes an already-registered color frame.
};
```

## No hardware? `SyntheticDepthCamera`

The addon ships a software-only `DepthCamera` that generates an animated scene
(a rippling wall with a bobbing sphere) plus a depth-keyed color image — no
device required:

```cpp
shared_ptr<DepthCamera> cam = make_shared<SyntheticDepthCamera>(640, 480);
cam->setup();
cam->update();
cam->toMesh({.colors = true}).draw();
```

Use it to develop and test against the interface on any platform (it powers the
examples and CI), to run point-cloud demos on machines without a camera, and as
a reference implementation. It animates by an internal frame counter (so it's
deterministic) and relies on the base's default deprojection / `toMesh()`.

## Threaded, thread-safe implementations

For the common pull-based SDK, inherit `ThreadedDepthCameraBase<Frame>` instead
of `DepthCamera`. It implements the background grabber thread, the mutex, the
triple-buffer hand-off, and the frame-new flags **once**, so an implementation
never writes that (forgettable, race-prone) code:

```cpp
struct OrbbecFrame { std::vector<uint16_t> depth; Pixels color; /* ... */ };

class Orbbec : public ThreadedDepthCameraBase<OrbbecFrame>, public IColorStream {
protected:
    bool openDevice() override;                       // open the SDK device
    void closeDevice() override;
    StreamFreshness captureInto(OrbbecFrame& f) override;  // grab one frame
public:
    // getters just read front() — no locking needed:
    float getDistanceAt(int x, int y) const override {
        return front().depth[y * getWidth() + x] * 0.001f;
    }
    bool isColorFrameNew() const override { return isStreamNew(Stream::Color); }
    // ...
};
```

`setup()` / `update()` / `close()` / `isFrameNew()` are provided (`final`).
Getters read `front()`, which is only mutated inside `update()` on the calling
thread, so they need no locking — the mutex is held only for two O(1) pointer
swaps, never during device I/O or per-pixel reads. Call `setThreaded()` before
`setup()`; `setThreaded(false)` captures inline (no thread). The
callback-driven-SDK case can still implement raw `DepthCamera` directly.

## Roadmap

- **Virtual source family.** Because `DepthCamera` is a narrow per-frame
  contract (depth + intrinsics + optional color/IR), the data source is
  swappable. `SyntheticDepthCamera` is the first non-hardware source; the same
  idea gives, for free, **playback** (a recording replayed *as* a `DepthCamera`)
  and **network** (a remote camera received as a `DepthCamera`), plus the sink
  side — a recorder / sender that consumes *any* `DepthCamera`. Doing this well
  means defining a common serialized `DepthFrame` format (with compression —
  depth+color+IR is heavy, ~150 MB/s uncompressed at 30 fps), so it's deferred
  until the live-capture path is proven.
- **Device registry / factory.** Once there are ≥2 backends, a
  `DepthCameraRegistry` (each implementation self-registers an enumerator +
  factory) will provide cross-backend `listDevices()` and a `create(info)`
  factory — so you can list every connected camera and construct one without
  knowing its concrete type. Kept out for now: with a single backend it adds
  coupling for no benefit, and a per-implementation `Camera::listDevices()` is
  enough.

## License

MIT. Interface-only — bundles no third-party code. See `LICENSES.md`.
