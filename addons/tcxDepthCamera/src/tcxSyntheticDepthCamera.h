#pragma once

// =============================================================================
// tcxSyntheticDepthCamera.h - Software-generated depth camera (no hardware)
// =============================================================================
//
// A DepthCamera that produces a procedurally generated, animated scene instead
// of reading a physical sensor. It is the "non-hardware source" stand-in:
//
//   - develop / test the interface, ThreadedDepthCameraBase, toMesh() and
//     point-cloud rendering with zero hardware, on any platform (incl. macOS)
//   - run point-cloud examples & demos on machines without a depth camera
//   - exercise the API in CI (headless), catching binding / API drift
//   - serve as a reference implementation of a DepthCamera
//
// It generates a rippling back wall with a bobbing sphere in front, plus a
// depth-keyed color image, and animates by an internal frame counter (so it is
// deterministic and reproducible - good for CI). It implements IColorStream and
// deliberately relies on the DEFAULT pinhole deprojection / toMesh() in the
// base (a real device like Azure Kinect overrides those with an SDK path), so
// both code paths get exercised across the addon family.
//
//   #include <tcxDepthCamera.h>
//   using namespace tcx;
//
//   shared_ptr<DepthCamera> cam = make_shared<SyntheticDepthCamera>(640, 480);
//   cam->setup();
//   ...
//   cam->update();
//   cam->toMesh({.colors = true}).draw();
//
// =============================================================================

#include "tcxThreadedDepthCamera.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace tcx {

using namespace tc;

struct SyntheticFrame {
    int w = 0;
    int h = 0;
    std::vector<uint16_t> depth;  // mm
    Pixels color;                 // RGBA, w x h
};

class SyntheticDepthCamera : public ThreadedDepthCameraBase<SyntheticFrame>,
                             public IColorStream {
public:
    explicit SyntheticDepthCamera(int width = 640, int height = 480)
        : width_(width), height_(height) {}

    // Set before setup().
    SyntheticDepthCamera& setResolution(int w, int h) {
        width_ = w;
        height_ = h;
        return *this;
    }

    // -- DepthCamera ----------------------------------------------------------
    int getWidth()  const override { return front().w ? front().w : width_; }
    int getHeight() const override { return front().h ? front().h : height_; }

    float getDistanceAt(int x, int y) const override {
        const SyntheticFrame& f = front();
        if (x < 0 || y < 0 || x >= f.w || y >= f.h) return 0.0f;
        const size_t i = static_cast<size_t>(y) * f.w + x;
        return i < f.depth.size() ? f.depth[i] * 0.001f : 0.0f;
    }

    DepthIntrinsics getDepthIntrinsics() const override {
        DepthIntrinsics in;
        in.width  = width_;
        in.height = height_;
        in.fx = in.fy = static_cast<float>(width_);  // ~ moderate FOV
        in.cx = width_ * 0.5f;
        in.cy = height_ * 0.5f;
        return in;
    }

    DepthSensorType getSensorType() const override { return DepthSensorType::Unknown; }

    // -- IColorStream ---------------------------------------------------------
    bool isColorFrameNew() const override { return isStreamNew(Stream::Color); }
    int getColorWidth()  const override { return getWidth(); }
    int getColorHeight() const override { return getHeight(); }
    const Pixels& getColorPixels() const override { return front().color; }
    Vec2 getColorTexCoordAt(int dx, int dy) const override {
        return Vec2{(dx + 0.5f) / getWidth(), (dy + 0.5f) / getHeight()};
    }

protected:
    bool openDevice() override {
        frame_ = 0;
        return width_ > 0 && height_ > 0;
    }
    void closeDevice() override {}

    StreamFreshness captureInto(SyntheticFrame& dst) override {
        const int w = width_;
        const int h = height_;
        dst.w = w;
        dst.h = h;
        dst.depth.resize(static_cast<size_t>(w) * h);
        if (!dst.color.isAllocated() ||
            dst.color.getWidth() != w || dst.color.getHeight() != h) {
            dst.color.allocate(w, h, 4);
        }

        const float phase = frame_ * 0.08f;
        ++frame_;

        const float sphereR = 0.18f;
        const float cxs = 0.25f * std::sin(phase);          // sphere drifts
        const float cys = 0.15f * std::cos(phase * 1.3f);

        uint8_t* col = dst.color.getData();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / w - 0.5f;
                const float v = static_cast<float>(y) / h - 0.5f;

                // Rippling back wall ~2.5 m.
                float d = 2.5f + 0.15f * std::sin(u * 12.0f + phase) *
                                          std::sin(v * 12.0f + phase * 0.8f);

                // Bobbing sphere in front ~1.3 m.
                const float dd = std::sqrt((u - cxs) * (u - cxs) +
                                           (v - cys) * (v - cys));
                if (dd < sphereR) {
                    const float bump = std::sqrt(sphereR * sphereR - dd * dd);
                    d = 1.3f - bump;
                }

                const size_t i = static_cast<size_t>(y) * w + x;
                dst.depth[i] = static_cast<uint16_t>(d * 1000.0f);  // mm

                // Depth-keyed color (near = warm, far = cool).
                float t = (d - 1.0f) / 1.8f;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                col[i * 4 + 0] = static_cast<uint8_t>((1.0f - t) * 255.0f);
                col[i * 4 + 1] = static_cast<uint8_t>((0.3f + 0.4f * t) * 255.0f);
                col[i * 4 + 2] = static_cast<uint8_t>(t * 255.0f);
                col[i * 4 + 3] = 255;
            }
        }

        // In threaded mode, pace the background grabber to ~60 fps so it does
        // not spin. In inline mode the app's own loop sets the cadence.
        if (isThreaded()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        StreamFreshness fresh;
        fresh.depth = true;
        fresh.color = true;
        return fresh;
    }

private:
    int width_;
    int height_;
    uint64_t frame_ = 0;
};

} // namespace tcx
