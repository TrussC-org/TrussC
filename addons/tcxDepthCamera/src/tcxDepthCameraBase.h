#pragma once

// =============================================================================
// tcxDepthCameraBase.h - Unified, thread-safe base for depth / point cloud cameras
// =============================================================================
//
// DepthCamera is the one common type for every depth sensor (structured light,
// stereo, ToF, LiDAR). It is a CONCRETE base that owns a canonical, triple-
// buffered DepthFrame and provides everything built on top of it - distance,
// world coordinates, color, meshing, threading. An implementation only has to:
//
//   1. open a shared Device      (openDevice)
//   2. fill a DepthFrame          (Device::captureInto)
// The Device owns the source and releases it in its destructor; it never
// retains the backend handle. Capture may outlive that handle on its own thread.
//
// It never writes getters, a mutex, or buffer juggling. Drive any camera:
//
//   shared_ptr<DepthCamera> cam = make_shared<Orbbec>(serial);
//   cam->setThreaded(true);
//   cam->setup();
//   ...
//   cam->update();
//   if (cam->isFrameNew()) cam->toMesh({.colors = true}).draw();
//
// Threading model (triple buffer): the worker fills the CAPTURE frame, then
// briefly locks to promote it to BACK; update() briefly locks to swap BACK ->
// FRONT; getters read FRONT (mutated only inside update() on the calling
// thread). So getters need no locking - the mutex is held only for two O(1)
// pointer swaps, never during device I/O or per-pixel reads. setThreaded(false)
// captures inline.
//
// Units: getDistanceAt() / getWorldCoordinateAt() return meters. Internally
// depth is uint16 * depthScale (see DepthFrame).
//
// =============================================================================

#include "tcxDepthTypes.h"
#include "tcxDepthImage.h"
#include <mutex>
#include <memory>
#include <atomic>
#include "tc/utils/tcKeptThreads.h"
#include <utility>

namespace tcx::depthcamera {

using namespace tc;

class DepthCamera {
public:
    struct Device {
        // Produce a COMPLETE frame: carry forward streams that did not refresh,
        // since the destination buffers rotate. Runs off-thread when requested.
        virtual StreamFreshness captureInto(DepthFrame& dst) = 0;
        virtual ~Device() = default;
    };

    DepthCamera() = default;
    virtual ~DepthCamera() { close(); }

    DepthCamera(const DepthCamera&) = delete;
    DepthCamera& operator=(const DepthCamera&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    // Open the device and (if threaded) start the grabber. Returns false on
    // failure. Call setThreaded() beforehand to choose the mode.
    bool setup() {
        close();
        auto run = std::make_shared<State>();
        state_ = run;
        run->device = openDevice();
        if (!run->device) return false;
        if (threaded_) {
            run->running = true;
            run->threads.start(run->thread, [run] { captureLoop(run); });
        }
        return true;
    }

    // Stop the grabber and release the device.
    void close() {
        auto run = state_;
        run->running = false;
        const bool fromCapture = run->thread.joinable() &&
            run->thread.get_id() == std::this_thread::get_id();
        run->threads.release(run->thread);
        run->threads.joinOthers();
        if (fromCapture) {
            // The worker still owns this run and its device until it returns.
            state_ = std::make_shared<State>();
        } else {
            run->device.reset();
        }
    }

    // Publish the latest frame. Call once per app frame.
    void update() {
        if (state_->running) {
            std::lock_guard<std::mutex> lk(state_->mutex);
            if (state_->backReady) {
                std::swap(state_->back, state_->front);
                state_->freshness = state_->pending;
                state_->pending.clear();
                state_->backReady = false;
            } else {
                state_->freshness.clear();  // nothing new this tick
            }
        } else {
            StreamFreshness f = state_->device ? state_->device->captureInto(*state_->back) : StreamFreshness{};
            if (f.any()) std::swap(state_->back, state_->front);
            state_->freshness = f;
        }
        // Mark preview Images for re-upload on the next getXxxImage() call.
        if (state_->freshness.color)    colorImageDirty_    = true;
        if (state_->freshness.depth)    depthImageDirty_    = true;
        if (state_->freshness.infrared) irImageDirty_       = true;
    }

    // -------------------------------------------------------------------------
    // Freshness (per stream, since streams may arrive at different rates)
    // -------------------------------------------------------------------------
    bool isFrameNew()         const { return state_->freshness.depth; }
    bool isColorFrameNew()    const { return state_->freshness.color; }
    bool isInfraredFrameNew() const { return state_->freshness.infrared; }
    bool isStreamNew(Stream s) const { return state_->freshness.get(s); }

    // -------------------------------------------------------------------------
    // Threading
    // -------------------------------------------------------------------------
    void setThreaded(bool threaded) { threaded_ = threaded; }  // call before setup()
    bool isThreaded() const { return threaded_; }

    // -------------------------------------------------------------------------
    // Stream enable / disable
    // -------------------------------------------------------------------------
    // Which streams to request from the device. ALL OFF by default - enable the
    // ones you use. Disabling a stream means the device need not transfer or
    // capture it at all (saving USB bandwidth / power), which is stronger than
    // just not reading it.
    //
    // Call before setup(). Whether a backend can toggle live depends on its SDK
    // (some need a reconfigure / restart): override these to honor live toggling
    // or to warn + no-op when the hardware can't. A backend fills only the
    // enabled streams in captureInto().
    virtual void enableDepth(bool on = true)    { depthEnabled_ = on; }
    virtual void enableColor(bool on = true)    { colorEnabled_ = on; }
    virtual void enableInfrared(bool on = true) { infraredEnabled_ = on; }
    bool isDepthEnabled()    const { return depthEnabled_; }
    bool isColorEnabled()    const { return colorEnabled_; }
    bool isInfraredEnabled() const { return infraredEnabled_; }

    // -------------------------------------------------------------------------
    // Sensor info (a device property, not frame data)
    // -------------------------------------------------------------------------
    virtual DepthSensorType getSensorType() const { return DepthSensorType::Unknown; }

    // -------------------------------------------------------------------------
    // Frame access
    // -------------------------------------------------------------------------

    // The current readable frame - this is the thing you use a depth camera for.
    // Read freely on the thread that calls update(); no locking required.
    const DepthFrame& currentFrame() const { return *state_->front; }

    int    getWidth()  const { return state_->front->w; }
    int    getHeight() const { return state_->front->h; }
    double getTimestamp() const { return state_->front->timestamp; }
    DepthIntrinsics getDepthIntrinsics() const { return state_->front->intrinsics; }

    // Distance along the optical axis at (x, y) in meters. 0 = invalid / no data.
    float getDistanceAt(int x, int y) const {
        if (!depthEnabled_) { warnOnce(warnedDepth_, "depth", "enableDepth"); return 0.0f; }
        const DepthFrame& f = *state_->front;
        if (x < 0 || y < 0 || x >= f.w || y >= f.h) return 0.0f;
        const size_t i = static_cast<size_t>(y) * f.w + x;
        return i < f.depth.size() ? f.depth[i] * f.depthScale : 0.0f;
    }

    // 3D camera-space coordinate at (x, y) in meters (+X right, +Y down, +Z
    // away). Uses the frame's precomputed world cloud when present (e.g. an
    // SDK's accurate transform); otherwise deprojects from intrinsics. When the
    // intrinsics carry Brown-Conrady distortion coefficients they are removed by
    // iterative undistortion, so a recording can store just the intrinsics and
    // recompute world on playback (rather than storing the whole world image).
    // Returns the origin for invalid depth.
    Vec3 getWorldCoordinateAt(int x, int y) const {
        const DepthFrame& f = *state_->front;
        if (x < 0 || y < 0 || x >= f.w || y >= f.h) return Vec3{0, 0, 0};
        const size_t i = static_cast<size_t>(y) * f.w + x;
        if (i < f.world.size()) return f.world[i];
        const float d = (i < f.depth.size()) ? f.depth[i] * f.depthScale : 0.0f;
        if (d <= 0.0f) return Vec3{0, 0, 0};
        const DepthIntrinsics& in = f.intrinsics;
        if (in.fx == 0.0f || in.fy == 0.0f) return Vec3{0, 0, d};

        // Observed (possibly distorted) normalized image coordinates.
        float xn = (static_cast<float>(x) - in.cx) / in.fx;
        float yn = (static_cast<float>(y) - in.cy) / in.fy;

        // Undistort (Brown-Conrady) only when coefficients are present - a plain
        // pinhole camera (all coeffs 0, e.g. SyntheticDepthCamera) skips this.
        if (in.k1 != 0.0f || in.k2 != 0.0f || in.k3 != 0.0f ||
            in.p1 != 0.0f || in.p2 != 0.0f) {
            float ux = xn, uy = yn;
            for (int it = 0; it < 5; ++it) {  // fixed-point iteration
                const float r2 = ux * ux + uy * uy;
                const float radial = 1.0f + in.k1 * r2 + in.k2 * r2 * r2 +
                                            in.k3 * r2 * r2 * r2;
                const float dx = 2.0f * in.p1 * ux * uy + in.p2 * (r2 + 2.0f * ux * ux);
                const float dy = in.p1 * (r2 + 2.0f * uy * uy) + 2.0f * in.p2 * ux * uy;
                ux = (xn - dx) / radial;
                uy = (yn - dy) / radial;
            }
            xn = ux;
            yn = uy;
        }
        return Vec3{xn * d, yn * d, d};
    }

    // -------------------------------------------------------------------------
    // Color / IR (color is NATIVE full resolution; mapping is computed on demand)
    // -------------------------------------------------------------------------
    bool          hasColor()       const { return colorEnabled_ && state_->front->hasColor(); }
    const Pixels& getColorPixels() const {   // native full-resolution color
        if (!colorEnabled_) warnOnce(warnedColor_, "color", "enableColor");
        return state_->front->color;
    }

    // Normalized UV (0-1) into the (full-resolution) color image for depth pixel
    // (x, y). Uses a precomputed colorUV map if present, else projects: deproject
    // the depth pixel to 3D, transform into color-camera space (depthToColor),
    // and project with colorIntrinsics. Computed lazily - only when you ask.
    Vec2 getColorTexCoordAt(int x, int y) const {
        const DepthFrame& f = *state_->front;
        if (f.w <= 0 || f.h <= 0) return Vec2{0, 0};
        const size_t i = static_cast<size_t>(y) * f.w + x;
        if (i < f.colorUV.size()) return f.colorUV[i];     // precomputed map

        const Vec3 pd = getWorldCoordinateAt(x, y);        // depth-camera space (m)
        if (pd.z <= 0.0f) return Vec2{0, 0};               // invalid depth
        const Vec3 pc = f.depthToColor * pd;               // color-camera space
        if (pc.z <= 0.0f) return Vec2{0, 0};

        const DepthIntrinsics& ci = f.colorIntrinsics;
        const float cw = ci.width  > 0 ? static_cast<float>(ci.width)  : static_cast<float>(f.color.getWidth());
        const float ch = ci.height > 0 ? static_cast<float>(ci.height) : static_cast<float>(f.color.getHeight());
        if (ci.fx == 0.0f || ci.fy == 0.0f || cw <= 0.0f || ch <= 0.0f) return Vec2{0, 0};
        const float u = (pc.x / pc.z) * ci.fx + ci.cx;
        const float v = (pc.y / pc.z) * ci.fy + ci.cy;
        return Vec2{u / cw, v / ch};
    }

    // Sampled color for depth pixel (x, y): the full-res color sampled at its
    // projected UV.
    Color getColorAt(int x, int y) const {
        const Pixels& c = state_->front->color;
        if (!c.isAllocated()) return Color{0, 0, 0, 1};
        const Vec2 uv = getColorTexCoordAt(x, y);
        const int cx = static_cast<int>(uv.x * c.getWidth());
        const int cy = static_cast<int>(uv.y * c.getHeight());
        if (cx < 0 || cy < 0 || cx >= c.getWidth() || cy >= c.getHeight()) {
            return Color{0, 0, 0, 1};
        }
        return c.getColor(cx, cy);
    }

    // Opt-in: build a depth-resolution color image by sampling the full-res
    // color through the depth->color mapping (the old "registered" image). Not
    // done automatically - call it only if you actually want it.
    Pixels registerColorToDepth() const {
        const DepthFrame& f = *state_->front;
        Pixels out;
        if (!f.color.isAllocated() || f.w <= 0 || f.h <= 0) return out;
        out.allocate(f.w, f.h, 4);
        for (int y = 0; y < f.h; ++y) {
            for (int x = 0; x < f.w; ++x) {
                out.setColor(x, y, getColorAt(x, y));
            }
        }
        return out;
    }

    bool          hasInfrared()       const { return infraredEnabled_ && state_->front->hasInfrared(); }
    const Pixels& getInfraredPixels() const {
        if (!infraredEnabled_) warnOnce(warnedInfrared_, "infrared", "enableInfrared");
        return state_->front->ir;
    }

    // -------------------------------------------------------------------------
    // Preview Images (cached, uploaded once per new frame)
    // -------------------------------------------------------------------------
    // Convenience wrappers over the tcxDepthImage.h converters: a ready-to-draw
    // Image of each stream, cached and re-uploaded only when that stream gets a
    // new frame. For the common "just show it" case - no per-app boilerplate:
    //
    //   if (cam->hasColor()) cam->getColorImage().draw(0, 0);
    //   cam->getDepthImage({.nearM = 0.3f, .farM = 4.0f}).draw(0, 0);
    //
    // The returned Image is non-const (draw() needs the texture); it stays valid
    // until the next call. If you own the Image yourself (e.g. a DepthFrame from
    // playback), use the free colorToImage()/depthToImage()/irToImage() instead.
    Image& getColorImage() {
        if (colorImageDirty_) { colorToImage(state_->front->color, colorImage_); colorImageDirty_ = false; }
        return colorImage_;
    }
    Image& getDepthImage(const DepthImageView& view = {}) {
        if (depthImageDirty_ || view != depthImageLastView_) {
            depthToImage(*state_->front, depthImage_, view);
            depthImageLastView_ = view;
            depthImageDirty_ = false;
        }
        return depthImage_;
    }
    Image& getInfraredImage() {
        if (irImageDirty_) { irToImage(state_->front->ir, irImage_); irImageDirty_ = false; }
        return irImage_;
    }

    // -------------------------------------------------------------------------
    // Mesh / point cloud
    // -------------------------------------------------------------------------

    // Build a point-cloud Mesh (Points) from the current frame in one pass:
    // invalid points (depth 0) are skipped, and the optional attributes stay
    // aligned with the kept vertices. Reads the frame directly (no per-pixel
    // virtual dispatch). Virtual so an implementation can override with a bulk /
    // GPU path; the default is a correct, portable reference.
    virtual Mesh toMesh(DepthMeshOptions opt = {}) const {
        const DepthFrame& f = *state_->front;
        Mesh m;
        m.setMode(PrimitiveMode::Points);

        const int step = (opt.step < 1) ? 1 : opt.step;
        const bool useColor = (opt.colors || opt.texCoords) && f.hasColor();

        for (int y = 0; y < f.h; y += step) {
            for (int x = 0; x < f.w; x += step) {
                const size_t i = static_cast<size_t>(y) * f.w + x;
                if (i >= f.depth.size() || f.depth[i] == 0) continue;  // invalid
                m.addVertex(getWorldCoordinateAt(x, y));
                if (useColor && opt.texCoords) m.addTexCoord(getColorTexCoordAt(x, y));
                if (useColor && opt.colors)    m.addColor(getColorAt(x, y));
            }
        }
        return m;
    }

protected:
    // -------------------------------------------------------------------------
    // Implementation hooks
    // -------------------------------------------------------------------------

    // Return a source owning every resource capture touches, or nullptr on
    // failure. The worker holds only this Device and its run, never the backend.
    virtual std::shared_ptr<Device> openDevice() = 0;

private:
    struct State {
        DepthFrame buffers[3];
        DepthFrame* front = &buffers[0];
        DepthFrame* back = &buffers[1];
        DepthFrame* capture = &buffers[2];
        std::mutex mutex;
        StreamFreshness pending{};
        StreamFreshness freshness{};
        bool backReady = false;
        std::atomic<bool> running{false};
        std::shared_ptr<Device> device;
        std::thread thread;
        tc::internal::KeptThreads threads;
    };

    static void captureLoop(const std::shared_ptr<State>& run) {
        while (run->running) {
            StreamFreshness f = run->device->captureInto(*run->capture);
            if (!f.any()) continue;
            std::lock_guard<std::mutex> lk(run->mutex);
            std::swap(run->capture, run->back);
            run->pending |= f;
            run->backReady = true;
        }
    }

    // One-time warning when a disabled stream is read.
    void warnOnce(bool& warned, const char* stream, const char* enableFn) const {
        if (warned) return;
        warned = true;
        logWarning("tcxDepthCamera")
            << stream << " stream is disabled; call " << enableFn
            << "() before setup() to use it.";
    }

    std::shared_ptr<State> state_ = std::make_shared<State>();
    bool threaded_ = false;

    // Stream enables (all off by default; enable what you use before setup()).
    bool depthEnabled_ = false;
    bool colorEnabled_ = false;
    bool infraredEnabled_ = false;
    mutable bool warnedDepth_ = false;
    mutable bool warnedColor_ = false;
    mutable bool warnedInfrared_ = false;

    // Cached preview Images (uploaded lazily, once per new frame; see getXxxImage()).
    Image colorImage_, depthImage_, irImage_;
    bool colorImageDirty_ = false, depthImageDirty_ = false, irImageDirty_ = false;
    DepthImageView depthImageLastView_{};
};

} // namespace tcx::depthcamera

// Backward compatibility: canonical is now tcx::depthcamera. Flat tcx:: alias
// kept until v1.0.0.
namespace tcx { using depthcamera::DepthCamera; } // deprecated: remove at v1.0.0
