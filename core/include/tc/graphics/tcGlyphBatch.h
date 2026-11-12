#pragma once

#include "tcFont.h"
#include "../gpu/tcShaderStream.h"
#include <limits>

namespace trussc {
namespace internal {
sg_shader glyphBatchShader();
sg_buffer glyphBatchIndices(size_t quads);
}

// Retained glyph quads. Record model transforms; draw applies the active camera
// view/projection once. The current model is not applied again. Consecutive
// glyphs sharing an atlas page form one draw, preserving add order.
class GlyphBatch {
public:
    GlyphBatch() = default;
    GlyphBatch(const GlyphBatch&) = delete;
    GlyphBatch& operator=(const GlyphBatch&) = delete;
    GlyphBatch(GlyphBatch&&) = default;
    GlyphBatch& operator=(GlyphBatch&&) = default;

    // Copy settings and share the current atlas, as Font copies do. Later source
    // option changes replace its atlas; call setFont again to adopt them.
    void setFont(const Font& font) {
        clear();
        font_ = font;
    }
    void setAlign(Direction h, Direction v) { alignH_ = h; alignV_ = v; }
    void setAlignToInk(bool enabled) { alignToInk_ = enabled; }
    void setWritingMode(WritingMode mode) { font_.setWritingMode(mode); }

    void add(uint32_t codepoint, const Color& color) {
        if (font_.atlasManager_) add(codepoint, getMatrix(), color);
    }
    void add(uint32_t codepoint, const Mat4& matrix, const Color& color) {
        if (!font_.atlasManager_ || codepoint == '\n' || codepoint == '\t') return;
        auto* glyph = font_.atlasManager_->getOrLoadGlyph(codepoint);
        if (!glyph || !glyph->isValid()) return;
        const float s = 1.0f / font_.dpiScale_;
        float x = 0, baseline = font_.getAscent();
        if (alignToInk_) {
            const auto center = font_.getGlyphInkBox(codepoint).getCenter();
            x = -center.x;
            baseline = -center.y;
        } else {
            if (alignH_ == Direction::Center) x = -glyph->getAdvance() * s / 2;
            if (alignH_ == Direction::Right) x = -glyph->getAdvance() * s;
            if (alignV_ == Direction::Baseline) baseline = 0;
            if (alignV_ == Direction::Center) baseline -= font_.getLineHeight() / 2;
            if (alignV_ == Direction::Bottom) baseline -= font_.getLineHeight();
            // Match Font's grid-fit rule using the supplied model matrix.
            const auto& m = matrix.m;
            const float sx = std::sqrt(m[0]*m[0] + m[4]*m[4] + m[8]*m[8]);
            const float sy = std::sqrt(m[1]*m[1] + m[5]*m[5] + m[9]*m[9]);
            const float scale = std::max(sx, sy);
            if (font_.gridFit_ && std::fabs((scale > 0 ? scale : 1) * internal::targetPixelScale() - 1) < 0.01f)
                baseline = std::round(baseline);
        }
        append({codepoint, x, baseline, 0, 0, 0, 1}, matrix, color);
    }
    // String layout follows Font; ink pivot selection applies to independent glyphs.
    void add(const std::string& text, const Color& color) {
        if (!font_.atlasManager_) return;
        const Mat4 matrix = getMatrix();
        font_.forEachGlyph(text, 0, 0, alignH_, alignV_, [&](const Font::PlacedGlyph& placed) {
            append(placed, matrix, color);
        });
    }

    void draw() { draw(Mat4::identity()); }
    void draw(const Mat4& transform) {
        if (!activeRuns_ || !sg_isvalid()) return;
        auto* target = internal::drawingApiTarget();
        if (!target || !font_.atlasManager_) return;
        font_.initResources();
        // Request mips before recording; dirty pages are published once at the
        // target flush, before any recorded draw replays.
        font_.atlasManager_->requestMipmaps();
        const auto blend = target->currentBlendMode;
        auto recipe = internal::pipeDesc2D(blend, target->depthTestEnabled);
        recipe.shader = internal::glyphBatchShader();
        recipe.layout = {};
        recipe.layout.buffers[0].stride = sizeof(Vertex);
        recipe.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT4;
        recipe.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        recipe.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
        recipe.index_type = SG_INDEXTYPE_UINT32;
        const auto pipeline = target->pipeline(internal::PipelineKey::raw(internal::PipelineRole::GlyphBatch, recipe));
        if (!pipeline.raw.id) return;
        const Mat4 mvp = (target->camera.currentProjectionMatrix * target->camera.currentViewMatrix * transform).transposed();
        // Upload every dirty run before queuing any of this batch's draws.
        // Buffer copies suspend/flush the existing target prefix, like StorageBuffer.
        size_t quads = 0;
        for (size_t i = 0; i < activeRuns_; ++i) {
            if (runs_[i].upload()) quads = std::max(quads, runs_[i].vertices.size() / 4);
        }
        if (!quads) return;
        const auto indices = internal::glyphBatchIndices(quads);
        if (sg_query_buffer_state(indices) != SG_RESOURCESTATE_VALID) return;
        for (size_t i = 0; i < activeRuns_; ++i) {
            auto& page = runs_[i];
            if (page.dirty || !page.gpu) continue;
            if (page.atlasIndex >= font_.atlasManager_->getAtlasCount()) continue;
            const sg_view view = font_.atlasManager_->recordAtlas(page.atlasIndex);
            if (!view.id) continue;
            const auto& atlas = font_.atlasManager_->getAtlas(page.atlasIndex);
            struct Params { float mvp[16]; float atlas[4]; } params{};
            std::memcpy(params.mvp, mvp.m, sizeof(params.mvp));
            params.atlas[0] = 1.0f / atlas.getWidth();
            params.atlas[1] = 1.0f / atlas.getHeight();
            params.atlas[2] = (blend == BlendMode::Screen || blend == BlendMode::Multiply) ? 1.0f : 0.0f;
            internal::DeferredShaderDraw command;
            const auto& clip = target->currentScissor;
            command.scissor = {clip.active ? clip.x : 0, clip.active ? clip.y : 0,
                               clip.active ? clip.w : float(target->width),
                               clip.active ? clip.h : float(target->height)};
            command.pipeline = pipeline.raw;
            command.pipelineGeneration = pipeline.generation;
            command.vertexCount = static_cast<int>(page.vertices.size() / 4 * 6); // indexed sg_draw count
            command.bindings.vertex_buffers[0] = page.gpu->vertices;
            command.bindings.index_buffer = indices;
            command.bindings.views[0] = view;
            command.bindings.samplers[0] = internal::fontSamplers().mipped;
            command.uniforms.emplace_back(0, std::vector<uint8_t>(sizeof(params)));
            std::memcpy(command.uniforms.back().second.data(), &params, sizeof(params));
            command.layerId = target->sglLayerNext;
            if (internal::appendTargetRecording(*target, target->deferredShaderDraws, std::move(command)))
                target->advanceLayer();
        }
    }
    void clear() {
        activeRuns_ = 0;
        for (auto& run : runs_) { run.vertices.clear(); run.dirty = true; }
    }

private:
    struct Vertex { Vec4 position; Vec2 texcoord; Color color; };
    struct Buffers {
        sg_buffer vertices{}, staging{};
        uint32_t frame = 0;
        ~Buffers() {
            internal::deferGpuDestroy(vertices);
            internal::deferGpuDestroy(staging);
        }
    };
    struct Run {
        size_t atlasIndex = 0;
        std::vector<Vertex> vertices;
        std::vector<std::unique_ptr<Buffers>> uploads;
        Buffers* gpu = nullptr;
        bool dirty = true;
        bool upload() {
            if (!dirty) return gpu != nullptr;
            // Bound (abi): sg_draw counts and buffer byte sizes use signed int.
            const size_t limit = static_cast<size_t>((std::numeric_limits<int>::max)());
            if (vertices.size() > limit / sizeof(Vertex) || vertices.size() / 4 > limit / (6 * sizeof(uint32_t))) {
                internal::reportImportError("GlyphBatch", "glyph buffer size cannot be represented");
                return false;
            }
            const auto frame = internal::shaderStreamFrame();
            size_t slot = 0;
            // A changed batch drawn again in this frame uses another snapshot.
            // Keep these slots for reuse next frame; never overwrite a queued draw.
            while (slot < uploads.size() && uploads[slot]->frame == frame) ++slot;
            if (slot == uploads.size()) uploads.push_back(std::make_unique<Buffers>());
            auto& buffers = uploads[slot];
            const size_t bytes = vertices.size() * sizeof(Vertex);
            if (!internal::shaderStreamFits(buffers->vertices, bytes)) {
                const size_t capacity = internal::ShaderStreamState::capacityFor(buffers->vertices, bytes);
                auto replacement = std::make_unique<Buffers>();
                sg_buffer_desc desc{};
                desc.size = capacity;
                desc.usage.vertex_buffer = true;
                desc.usage.copy_dst = true;
                desc.label = "tc_glyph_batch_vertices";
                replacement->vertices = sg_make_buffer(&desc);
                // This sokol revision replaces sg_update_buffer with transient
                // writes + copies. The destination persists on clean frames.
                // A GPU copy source preserves WebGPU command-encoder ordering.
                desc.usage.copy_dst = false;
                desc.usage.copy_src = true;
                desc.usage.write_transient = true;
                desc.label = "tc_glyph_batch_upload";
                replacement->staging = sg_make_buffer(&desc);
                if (sg_query_buffer_state(replacement->vertices) != SG_RESOURCESTATE_VALID ||
                    sg_query_buffer_state(replacement->staging) != SG_RESOURCESTATE_VALID) return false;
                buffers = std::move(replacement);
            }
            internal::suspendForImageUpload();
            sg_write_buffer_desc write{};
            write.src.data = {vertices.data(), bytes};
            write.dst.buffer = buffers->staging;
            sg_write_buffer_transient(&write);
            sg_copy_buffer_to_buffer_desc copy{};
            copy.src.buffer = buffers->staging;
            copy.dst.buffer = buffers->vertices;
            copy.size = bytes;
            sg_copy_buffer_to_buffer(&copy);
            buffers->frame = frame;
            gpu = buffers.get();
            dirty = false;
            return true;
        }
    };
    void append(const Font::PlacedGlyph& placed, const Mat4& matrix, const Color& color) {
        const auto* glyph = font_.atlasManager_->getOrLoadGlyph(placed.codepoint);
        if (!glyph || !glyph->isValid() || glyph->getWidth() <= 0 || glyph->getHeight() <= 0) return;
        const size_t index = glyph->getAtlasIndex();
        if (!activeRuns_ || runs_[activeRuns_ - 1].atlasIndex != index) {
            if (activeRuns_ == runs_.size()) runs_.emplace_back();
            runs_[activeRuns_++].atlasIndex = index;
        }
        auto& page = runs_[activeRuns_ - 1];
        const auto& atlas = font_.atlasManager_->getAtlas(index);
        const float s = 1.0f / font_.dpiScale_;
        const float x = placed.drawX + glyph->getXoff() * s * placed.scaleX;
        const float y = placed.baselineY + glyph->getYoff() * s;
        const float w = glyph->getWidth() * s * placed.scaleX, h = glyph->getHeight() * s;
        const Vec2 corners[] = {{x,y}, {x+w,y}, {x+w,y+h}, {x,y+h}};
        // Texel coordinates survive atlas expansion; normalize in the shader
        // using the current page dimensions at draw time.
        const float u0 = glyph->getU0() * atlas.getWidth(), u1 = glyph->getU1() * atlas.getWidth();
        const float v0 = glyph->getV0() * atlas.getHeight(), v1 = glyph->getV1() * atlas.getHeight();
        const Vec2 uv[] = {{u0,v0}, {u1,v0}, {u1,v1}, {u0,v1}};
        const float c = std::cos(placed.rotationCw), si = std::sin(placed.rotationCw);
        for (int i = 0; i < 4; ++i) {
            const float dx = corners[i].x - placed.pivotX, dy = corners[i].y - placed.pivotY;
            const Vec4 pos(placed.pivotX + c*dx - si*dy, placed.pivotY + si*dx + c*dy, 0, 1);
            page.vertices.push_back({matrix * pos, uv[i], color});
        }
        page.dirty = true;
    }
    Font font_;
    std::vector<Run> runs_;
    size_t activeRuns_ = 0;
    Direction alignH_ = Direction::Left;
    Direction alignV_ = Direction::Top;
    bool alignToInk_ = false;
};
} // namespace trussc
