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

namespace tcx::depthcamera {

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

    // Continuous scene depth (meters) at normalized image coords (u, v in
    // [-0.5, 0.5]) - shared by the depth and (higher-res) color passes.
    static float sceneDepth(float u, float v, float phase,
                            float cxs, float cys, float sphereR) {
        float d = 2.5f + 0.15f * std::sin(u * 12.0f + phase) *
                                  std::sin(v * 12.0f + phase * 0.8f);
        const float dd = std::sqrt((u - cxs) * (u - cxs) + (v - cys) * (v - cys));
        if (dd < sphereR) d = 1.3f - std::sqrt(sphereR * sphereR - dd * dd);
        return d;
    }

    StreamFreshness captureInto(DepthFrame& dst) override {
        const int dw = width_;
        const int dh = height_;
        const int cw = dw * colorScale_;   // color at higher NATIVE resolution
        const int ch = dh * colorScale_;
        const bool wantDepth = isDepthEnabled();
        const bool wantColor = isColorEnabled();

        dst.w = dw;
        dst.h = dh;
        dst.depthScale = 0.001f;  // mm

        // Depth intrinsics (pinhole, ~moderate FOV).
        dst.intrinsics.width = dw; dst.intrinsics.height = dh;
        dst.intrinsics.fx = dst.intrinsics.fy = static_cast<float>(dw);
        dst.intrinsics.cx = dw * 0.5f; dst.intrinsics.cy = dh * 0.5f;
        // Synthetic color shares the depth optics, just at higher resolution, so
        // depthToColor is identity and the color intrinsics are the depth ones
        // scaled by colorScale_.
        dst.colorIntrinsics.width = cw; dst.colorIntrinsics.height = ch;
        dst.colorIntrinsics.fx = dst.colorIntrinsics.fy = static_cast<float>(dw * colorScale_);
        dst.colorIntrinsics.cx = cw * 0.5f; dst.colorIntrinsics.cy = ch * 0.5f;
        dst.depthToColor = Mat4::identity();
        dst.colorUV.clear();

        const float phase = frame_ * 0.08f;
        ++frame_;
        const float sphereR = 0.18f;
        const float cxs = 0.25f * std::sin(phase);
        const float cys = 0.15f * std::cos(phase * 1.3f);

        if (wantDepth) {
            dst.depth.resize(static_cast<size_t>(dw) * dh);
            for (int y = 0; y < dh; ++y) {
                for (int x = 0; x < dw; ++x) {
                    const float u = (x + 0.5f) / dw - 0.5f;
                    const float v = (y + 0.5f) / dh - 0.5f;
                    const float d = sceneDepth(u, v, phase, cxs, cys, sphereR);
                    dst.depth[static_cast<size_t>(y) * dw + x] =
                        static_cast<uint16_t>(d * 1000.0f);
                }
            }
        } else {
            dst.depth.clear();
        }

        if (wantColor) {
            if (!dst.color.isAllocated() ||
                dst.color.getWidth() != cw || dst.color.getHeight() != ch) {
                dst.color.allocate(cw, ch, 4);
            }
            uint8_t* col = dst.color.getData();
            for (int y = 0; y < ch; ++y) {
                for (int x = 0; x < cw; ++x) {
                    const float u = (x + 0.5f) / cw - 0.5f;
                    const float v = (y + 0.5f) / ch - 0.5f;
                    const float d = sceneDepth(u, v, phase, cxs, cys, sphereR);
                    float t = (d - 1.0f) / 1.8f;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    const size_t i = static_cast<size_t>(y) * cw + x;
                    col[i * 4 + 0] = static_cast<uint8_t>((1.0f - t) * 255.0f);
                    col[i * 4 + 1] = static_cast<uint8_t>((0.3f + 0.4f * t) * 255.0f);
                    col[i * 4 + 2] = static_cast<uint8_t>(t * 255.0f);
                    col[i * 4 + 3] = 255;
                }
            }
        } else if (dst.color.isAllocated()) {
            dst.color = Pixels{};
        }

        if (isThreaded()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        StreamFreshness fresh;
        fresh.depth = wantDepth;
        fresh.color = wantColor;
        return fresh;
    }

private:
    int width_;
    int height_;
    int colorScale_ = 2;       // color native res = depth res * colorScale_
    uint64_t frame_ = 0;
};

} // namespace tcx::depthcamera

// Backward compatibility: canonical is now tcx::depthcamera. Flat tcx:: alias
// kept until v1.0.0.
namespace tcx { using depthcamera::SyntheticDepthCamera; } // deprecated: remove at v1.0.0
