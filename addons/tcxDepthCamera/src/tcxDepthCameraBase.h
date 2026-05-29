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
//   1. open / close the device   (openDevice / closeDevice)
//   2. fill a DepthFrame          (captureInto)
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
#include <mutex>
#include <utility>

namespace tcx {

using namespace tc;

class DepthCamera : protected tc::Thread {
public:
    DepthCamera()
        : front_(&buffers_[0]), back_(&buffers_[1]), capture_(&buffers_[2]) {}

    ~DepthCamera() override {
        if (isThreadRunning()) {
            stopThread();
            waitForThread(false);
        }
    }

    DepthCamera(const DepthCamera&) = delete;
    DepthCamera& operator=(const DepthCamera&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    // Open the device and (if threaded) start the grabber. Returns false on
    // failure. Call setThreaded() beforehand to choose the mode.
    bool setup() {
        if (!openDevice()) return false;
        if (threaded_) startThread();
        return true;
    }

    // Stop the grabber and release the device.
    void close() {
        if (isThreadRunning()) {
            stopThread();
            waitForThread(false);
        }
        closeDevice();
    }

    // Publish the latest frame. Call once per app frame.
    void update() {
        if (isThreadRunning()) {
            std::lock_guard<std::mutex> lk(mutex_);
            if (backReady_) {
                std::swap(back_, front_);
                freshness_ = pending_;
                pending_.clear();
                backReady_ = false;
            } else {
                freshness_.clear();  // nothing new this tick
            }
        } else {
            StreamFreshness f = captureInto(*back_);
            if (f.any()) std::swap(back_, front_);
            freshness_ = f;
        }
    }

    // -------------------------------------------------------------------------
    // Freshness (per stream, since streams may arrive at different rates)
    // -------------------------------------------------------------------------
    bool isFrameNew()         const { return freshness_.depth; }
    bool isColorFrameNew()    const { return freshness_.color; }
    bool isInfraredFrameNew() const { return freshness_.infrared; }
    bool isStreamNew(Stream s) const { return freshness_.get(s); }

    // -------------------------------------------------------------------------
    // Threading
    // -------------------------------------------------------------------------
    void setThreaded(bool threaded) { threaded_ = threaded; }  // call before setup()
    bool isThreaded() const { return threaded_; }

    // -------------------------------------------------------------------------
    // Sensor info (a device property, not frame data)
    // -------------------------------------------------------------------------
    virtual DepthSensorType getSensorType() const { return DepthSensorType::Unknown; }

    // -------------------------------------------------------------------------
    // Frame access
    // -------------------------------------------------------------------------

    // The current readable frame - this is the thing you use a depth camera for.
    // Read freely on the thread that calls update(); no locking required.
    const DepthFrame& currentFrame() const { return *front_; }

    int    getWidth()  const { return front_->w; }
    int    getHeight() const { return front_->h; }
    double getTimestamp() const { return front_->timestamp; }
    DepthIntrinsics getDepthIntrinsics() const { return front_->intrinsics; }

    // Distance along the optical axis at (x, y) in meters. 0 = invalid / no data.
    float getDistanceAt(int x, int y) const {
        const DepthFrame& f = *front_;
        if (x < 0 || y < 0 || x >= f.w || y >= f.h) return 0.0f;
        const size_t i = static_cast<size_t>(y) * f.w + x;
        return i < f.depth.size() ? f.depth[i] * f.depthScale : 0.0f;
    }

    // 3D camera-space coordinate at (x, y) in meters (+X right, +Y down, +Z
    // away). Uses the frame's precomputed world cloud when present (e.g. an
    // SDK's accurate, distortion-aware transform), else the pinhole formula.
    // Returns the origin for invalid depth.
    Vec3 getWorldCoordinateAt(int x, int y) const {
        const DepthFrame& f = *front_;
        if (x < 0 || y < 0 || x >= f.w || y >= f.h) return Vec3{0, 0, 0};
        const size_t i = static_cast<size_t>(y) * f.w + x;
        if (i < f.world.size()) return f.world[i];
        const float d = (i < f.depth.size()) ? f.depth[i] * f.depthScale : 0.0f;
        if (d <= 0.0f) return Vec3{0, 0, 0};
        const DepthIntrinsics& in = f.intrinsics;
        if (in.fx == 0.0f || in.fy == 0.0f) return Vec3{0, 0, d};
        return Vec3{
            (static_cast<float>(x) - in.cx) * d / in.fx,
            (static_cast<float>(y) - in.cy) * d / in.fy,
            d
        };
    }

    // -------------------------------------------------------------------------
    // Color / IR (optional; registered to the depth resolution)
    // -------------------------------------------------------------------------
    bool          hasColor()       const { return front_->hasColor(); }
    const Pixels& getColorPixels() const { return front_->color; }

    // Normalized UV into the color image for depth pixel (x, y). Color is
    // registered to depth, so it is simply the normalized depth coordinate.
    Vec2 getColorTexCoordAt(int x, int y) const {
        const DepthFrame& f = *front_;
        if (f.w <= 0 || f.h <= 0) return Vec2{0, 0};
        return Vec2{(x + 0.5f) / f.w, (y + 0.5f) / f.h};
    }
    Color getColorAt(int x, int y) const {
        const Pixels& c = front_->color;
        if (!c.isAllocated()) return Color{0, 0, 0, 1};
        if (x < 0 || y < 0 || x >= c.getWidth() || y >= c.getHeight()) {
            return Color{0, 0, 0, 1};
        }
        return c.getColor(x, y);
    }

    bool          hasInfrared()       const { return front_->hasInfrared(); }
    const Pixels& getInfraredPixels() const { return front_->ir; }

    // -------------------------------------------------------------------------
    // Mesh / point cloud
    // -------------------------------------------------------------------------

    // Build a point-cloud Mesh (Points) from the current frame in one pass:
    // invalid points (depth 0) are skipped, and the optional attributes stay
    // aligned with the kept vertices. Reads the frame directly (no per-pixel
    // virtual dispatch). Virtual so an implementation can override with a bulk /
    // GPU path; the default is a correct, portable reference.
    virtual Mesh toMesh(DepthMeshOptions opt = {}) const {
        const DepthFrame& f = *front_;
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

    // Open / close the underlying device or source (file, network, ...).
    virtual bool openDevice() = 0;
    virtual void closeDevice() = 0;

    // Fill dst with one frame and report which streams it refreshed. In threaded
    // mode this runs on the background grabber; otherwise inline in update().
    //
    // IMPORTANT: produce a COMPLETE frame. If a stream did not refresh this tick
    // (e.g. color runs slower than depth, or vice versa), carry its latest data
    // forward into dst - the buffers rotate, so dst will NOT retain last tick's
    // contents. Keep the latest of each stream as a member if your SDK delivers
    // streams independently; SDKs configured for synchronized capture give both
    // every time, so this is automatic. The returned StreamFreshness reports
    // what actually changed (drives isFrameNew / isColorFrameNew).
    virtual StreamFreshness captureInto(DepthFrame& dst) = 0;

private:
    void threadedFunction() override {
        while (isThreadRunning()) {
            StreamFreshness f = captureInto(*capture_);
            std::lock_guard<std::mutex> lk(mutex_);
            std::swap(capture_, back_);
            pending_ |= f;
            backReady_ = true;
        }
    }

    DepthFrame buffers_[3];
    DepthFrame* front_;
    DepthFrame* back_;
    DepthFrame* capture_;

    std::mutex mutex_;
    StreamFreshness pending_{};    // accumulated by worker, guarded by mutex_
    StreamFreshness freshness_{};  // current frame's flags, main thread only
    bool backReady_ = false;       // guarded by mutex_
    bool threaded_ = false;
};

} // namespace tcx
