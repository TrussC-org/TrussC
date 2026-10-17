#pragma once
#include <TrussC.h>

namespace tcx::hap {
// Uniform structs for YCoCg shader
struct YCoCgVsParams {
    float mvp[16];
};


namespace detail {

struct YCoCgDrawData {
    YCoCgVsParams uniforms;
    tc::ShaderVertex vertices[4];
};

inline YCoCgDrawData makeYCoCgDrawData(float x, float y, float w, float h,
                                      const tc::Mat4& projection, const tc::Mat4& view,
                                      const tc::Mat4& model, const tc::Color& color) {
    YCoCgDrawData data = {};
    // Mat4 is row-major; shader uniforms use column-major matrices.
    const tc::Mat4 mvp = (projection * view * model).transposed();
    std::memcpy(data.uniforms.mvp, mvp.m, sizeof(data.uniforms.mvp));
    data.vertices[0] = {x, y, 0, 0, 0, color.r, color.g, color.b, color.a};
    data.vertices[1] = {x + w, y, 0, 1, 0, color.r, color.g, color.b, color.a};
    data.vertices[2] = {x + w, y + h, 0, 1, 1, color.r, color.g, color.b, color.a};
    data.vertices[3] = {x, y + h, 0, 0, 1, color.r, color.g, color.b, color.a};
    return data;
}

} // namespace detail

} // namespace tcx::hap
