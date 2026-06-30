#pragma once

// =============================================================================
// tcxDepthImage.h - turn raw depth-camera streams into displayable Images
// =============================================================================
//
// Stateless converters that fill a caller-owned Image from a DepthFrame's raw
// streams (color / depth / IR). They operate on frame data, so they work on a
// live camera frame OR one obtained from playback. The camera offers cached
// getColorImage()/getDepthImage()/getInfraredImage() wrappers (see
// tcxDepthCameraBase.h) for the common "just show it" case; reach for these
// free functions when you own the Image yourself (e.g. a DepthFrame in hand).
//
// Each (re)allocates the Image only when the source size/shape changes, fills
// its pixels, then setDirty() + update() to upload. update() touches the GPU,
// so call these on the main thread (inside update()/draw()), not on a grabber.
//
// =============================================================================

#include "tcxDepthTypes.h"

#include <cmath>
#include <cstring>

namespace tcx::depthcamera {

using namespace tc;

// How depthToImage() maps depth to grayscale.
struct DepthImageView {
    float nearM  = 0.3f;   // distance mapped to bright
    float farM   = 4.0f;   // distance mapped to dark
    bool  repeat = false;  // wrap the near..far band repeatedly (contour-like)

    bool operator==(const DepthImageView& o) const {
        return nearM == o.nearM && farM == o.farM && repeat == o.repeat;
    }
    bool operator!=(const DepthImageView& o) const { return !(*this == o); }
};

namespace tcd_detail {
inline void ensureRGBA(Image& img, int w, int h) {
    if (!img.isAllocated() || img.getWidth() != w || img.getHeight() != h) {
        img.allocate(w, h, 4);
    }
}
} // namespace tcd_detail

// Color is already native RGBA (8-bit); copy it straight across. No-op unless
// the source is allocated 4-channel U8.
inline void colorToImage(const Pixels& c, Image& out) {
    if (!c.isAllocated() || c.getChannels() != 4 || c.isFloat()) return;
    tcd_detail::ensureRGBA(out, c.getWidth(), c.getHeight());
    std::memcpy(out.getPixelsData(), c.getData(),
                static_cast<size_t>(c.getWidth()) * c.getHeight() * 4);
    out.setDirty();
    out.update();
}

// Depth (uint16 * depthScale, meters) -> grayscale. near = bright, invalid (0)
// = black. With view.repeat the near..far band repeats (each band fades bright
// ->dark again), which reads like depth contours.
inline void depthToImage(const DepthFrame& f, Image& out, const DepthImageView& view = {}) {
    if (f.w <= 0 || f.h <= 0 || f.depth.empty()) return;
    tcd_detail::ensureRGBA(out, f.w, f.h);
    unsigned char* d = out.getPixelsData();
    const float span = (view.farM > view.nearM) ? (view.farM - view.nearM) : 1.0f;
    const int n = f.w * f.h;
    for (int i = 0; i < n; ++i) {
        unsigned char g = 0;
        if (f.depth[i] != 0) {
            float rel = f.depth[i] * f.depthScale - view.nearM;
            float t;
            if (view.repeat) {
                rel = std::fmod(rel, span);
                if (rel < 0.0f) rel += span;   // handle samples nearer than nearM
                t = rel / span;
            } else {
                t = rel / span;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            }
            g = static_cast<unsigned char>((1.0f - t) * 255.0f);
        }
        d[i * 4 + 0] = g; d[i * 4 + 1] = g; d[i * 4 + 2] = g; d[i * 4 + 3] = 255;
    }
    out.setDirty();
    out.update();
}

// IR is single-channel F32 active brightness; auto-normalize by the frame max
// and sqrt-gamma so low returns stay visible.
inline void irToImage(const Pixels& ir, Image& out) {
    if (!ir.isAllocated() || !ir.isFloat()) return;
    const int w = ir.getWidth(), h = ir.getHeight(), n = w * h;
    tcd_detail::ensureRGBA(out, w, h);
    const float* s = ir.getDataF32();
    float mx = 1.0f;
    for (int i = 0; i < n; ++i) if (s[i] > mx) mx = s[i];
    const float inv = 1.0f / mx;
    unsigned char* d = out.getPixelsData();
    for (int i = 0; i < n; ++i) {
        float t = s[i] * inv;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const unsigned char g = static_cast<unsigned char>(std::sqrt(t) * 255.0f);
        d[i * 4 + 0] = g; d[i * 4 + 1] = g; d[i * 4 + 2] = g; d[i * 4 + 3] = 255;
    }
    out.setDirty();
    out.update();
}

} // namespace tcx::depthcamera

// Backward compatibility: canonical is now tcx::depthcamera. Flat tcx:: alias
// kept until v1.0.0.
namespace tcx { // deprecated: remove at v1.0.0
    using depthcamera::DepthImageView;
    using depthcamera::colorToImage;
    using depthcamera::depthToImage;
    using depthcamera::irToImage;
}
