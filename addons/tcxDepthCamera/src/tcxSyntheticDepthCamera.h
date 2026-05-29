#pragma once

// =============================================================================
// tcxSyntheticDepthCamera.h - Software-generated depth camera (no hardware)
// =============================================================================
//
// A DepthCamera that fills the canonical DepthFrame with a procedurally
// generated, animated scene instead of reading a physical sensor. It is the
// "non-hardware source" stand-in:
//
//   - develop / test the interface, threading, toMesh() and point-cloud
//     rendering with zero hardware, on any platform (incl. macOS)
//   - run point-cloud examples & demos on machines without a depth camera
//   - exercise the API in CI (headless)
//   - serve as a reference implementation of a DepthCamera
//
// It generates a rippling back wall with a bobbing sphere in front plus a
// depth-keyed color image, and animates by an internal frame counter (so it is
// deterministic). Being a minimal implementation, it shows how little a backend
// has to do: fill the depth plane + color + intrinsics in captureInto().
//
//   #include <tcxDepthCamera.h>
//   using namespace tcx;
//   auto cam = make_shared<SyntheticDepthCamera>(640, 480);
//   cam->setup();
//   cam->update();
//   cam->toMesh({.colors = true}).draw();
//
// =============================================================================

#include "tcxDepthCameraBase.h"
#include <chrono>
#include <cmath>
#include <thread>

namespace tcx {

using namespace tc;

class SyntheticDepthCamera : public DepthCamera {
public:
    explicit SyntheticDepthCamera(int width = 640, int height = 480)
        : width_(width), height_(height) {}

    SyntheticDepthCamera& setResolution(int w, int h) {  // call before setup()
        width_ = w;
        height_ = h;
        return *this;
    }

    DepthSensorType getSensorType() const override { return DepthSensorType::Unknown; }

protected:
    bool openDevice() override {
        frame_ = 0;
        return width_ > 0 && height_ > 0;
    }
    void closeDevice() override {}

    StreamFreshness captureInto(DepthFrame& dst) override {
        const int w = width_;
        const int h = height_;
        dst.w = w;
        dst.h = h;
        dst.depthScale = 0.001f;  // store as mm
        dst.depth.resize(static_cast<size_t>(w) * h);
        if (!dst.color.isAllocated() ||
            dst.color.getWidth() != w || dst.color.getHeight() != h) {
            dst.color.allocate(w, h, 4);
        }

        // Intrinsics for a ~moderate FOV pinhole (drives default deprojection).
        dst.intrinsics.width  = w;
        dst.intrinsics.height = h;
        dst.intrinsics.fx = dst.intrinsics.fy = static_cast<float>(w);
        dst.intrinsics.cx = w * 0.5f;
        dst.intrinsics.cy = h * 0.5f;

        const float phase = frame_ * 0.08f;
        ++frame_;

        const float sphereR = 0.18f;
        const float cxs = 0.25f * std::sin(phase);
        const float cys = 0.15f * std::cos(phase * 1.3f);

        uint8_t* col = dst.color.getData();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / w - 0.5f;
                const float v = static_cast<float>(y) / h - 0.5f;

                float d = 2.5f + 0.15f * std::sin(u * 12.0f + phase) *
                                          std::sin(v * 12.0f + phase * 0.8f);
                const float dd = std::sqrt((u - cxs) * (u - cxs) +
                                           (v - cys) * (v - cys));
                if (dd < sphereR) {
                    d = 1.3f - std::sqrt(sphereR * sphereR - dd * dd);
                }

                const size_t i = static_cast<size_t>(y) * w + x;
                dst.depth[i] = static_cast<uint16_t>(d * 1000.0f);  // mm

                float t = (d - 1.0f) / 1.8f;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                col[i * 4 + 0] = static_cast<uint8_t>((1.0f - t) * 255.0f);
                col[i * 4 + 1] = static_cast<uint8_t>((0.3f + 0.4f * t) * 255.0f);
                col[i * 4 + 2] = static_cast<uint8_t>(t * 255.0f);
                col[i * 4 + 3] = 255;
            }
        }

        // Pace the background grabber so it does not spin (inline mode is paced
        // by the app's own update() loop).
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
