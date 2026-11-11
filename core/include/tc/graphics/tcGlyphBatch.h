#pragma once

#include "tcFont.h"
#include "../gpu/tcShaderStream.h"
#include <limits>

namespace trussc {
namespace internal {
sg_shader glyphBatchShader();
}

// Retained glyph quads. Record model transforms; draw applies the active camera
// view/projection once. The current model is not applied again. Pages are
// submitted in first-add order, with glyph order preserved within each page.
class GlyphBatch {
public:
    GlyphBatch() = default;
    GlyphBatch(const GlyphBatch&) = delete;
    GlyphBatch& operator=(const GlyphBatch&) = delete;
    GlyphBatch(GlyphBatch&&) = default;
    GlyphBatch& operator=(GlyphBatch&&) = default;

    // Retain the loaded font/settings. Changing fonts starts a new batch.
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
        if (pages_.empty() || !sg_isvalid()) return;
        auto* target = internal::drawingApiTarget();
        if (!target || !font_.atlasManager_) return;
        font_.initResources();
        // Request before replacing images and snapshotting their views. Creating
        // immutable images does not end/rebuild the active render pass.
        font_.atlasManager_->requestMipmaps();
        font_.atlasManager_->ensureTexturesUpdated();
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
        for (auto& page : pages_) {
            if (!page.upload()) continue;
            if (page.atlasIndex >= font_.atlasManager_->getAtlasCount()) continue;
            const auto& atlas = font_.atlasManager_->getAtlas(page.atlasIndex);
            struct Params { float mvp[16]; float atlas[4]; } params{};
            std::memcpy(params.mvp, mvp.m, sizeof(params.mvp));
            params.atlas[0] = 1.0f / atlas.getWidth();
            params.atlas[1] = 1.0f / atlas.getHeight();
            params.atlas[2] = (blend == BlendMode::Screen || blend == BlendMode::Multiply) ? 1.0f : 0.0f;
            internal::DeferredShaderDraw command;
            command.pipeline = pipeline.raw;
            command.pipelineGeneration = pipeline.generation;
            command.vertexCount = static_cast<int>(page.indices.size()); // indexed sg_draw count
            command.bindings.vertex_buffers[0] = page.gpu->vertices;
            command.bindings.index_buffer = page.gpu->indices;
            command.bindings.views[0] = atlas.getView();
            command.bindings.samplers[0] = internal::fontSamplers().mipped;
            command.uniforms.emplace_back(0, std::vector<uint8_t>(sizeof(params)));
            std::memcpy(command.uniforms.back().second.data(), &params, sizeof(params));
            command.layerId = target->sglLayerNext;
            if (internal::appendTargetRecording(*target, target->deferredShaderDraws, std::move(command)))
                target->advanceLayer();
        }
    }
    void clear() { pages_.clear(); }

private:
    struct Vertex { Vec4 position; Vec2 texcoord; Color color; };
    struct Buffers {
        sg_buffer vertices{}, indices{};
        ~Buffers() {
            internal::deferGpuDestroy(vertices);
            internal::deferGpuDestroy(indices);
        }
    };
    struct Page {
        size_t atlasIndex;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::shared_ptr<Buffers> gpu;
        bool dirty = true;
        bool upload() {
            if (!dirty) return gpu != nullptr;
            // Bound (abi): sg_draw counts and buffer byte sizes use signed int.
            const size_t limit = static_cast<size_t>((std::numeric_limits<int>::max)());
            if (vertices.size() > limit / sizeof(Vertex) || indices.size() > limit / sizeof(uint32_t)) {
                internal::reportImportError("GlyphBatch", "glyph buffer size cannot be represented");
                return false;
            }
            auto replacement = std::make_shared<Buffers>();
            sg_buffer_desc desc{};
            desc.data = {vertices.data(), vertices.size() * sizeof(Vertex)};
            desc.label = "tc_glyph_batch_vertices";
            replacement->vertices = sg_make_buffer(&desc);
            desc.data = {indices.data(), indices.size() * sizeof(uint32_t)};
            desc.usage.index_buffer = true;
            desc.label = "tc_glyph_batch_indices";
            replacement->indices = sg_make_buffer(&desc);
            if (sg_query_buffer_state(replacement->vertices) != SG_RESOURCESTATE_VALID ||
                sg_query_buffer_state(replacement->indices) != SG_RESOURCESTATE_VALID) return false;
            // Immutable replacements preserve earlier queued draws, even if add
            // and draw are called again before this frame's replay.
            gpu = std::move(replacement);
            dirty = false;
            return true;
        }
    };
    void append(const Font::PlacedGlyph& placed, const Mat4& matrix, const Color& color) {
        const auto* glyph = font_.atlasManager_->getOrLoadGlyph(placed.codepoint);
        if (!glyph || !glyph->isValid() || glyph->getWidth() <= 0 || glyph->getHeight() <= 0) return;
        const size_t index = glyph->getAtlasIndex();
        auto it = std::find_if(pages_.begin(), pages_.end(), [index](const Page& p) { return p.atlasIndex == index; });
        if (it == pages_.end()) { pages_.push_back(Page{index, {}, {}, {}, true}); it = pages_.end() - 1; }
        auto& page = *it;
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
        const auto base = static_cast<uint32_t>(page.vertices.size());
        const float c = std::cos(placed.rotationCw), si = std::sin(placed.rotationCw);
        for (int i = 0; i < 4; ++i) {
            const float dx = corners[i].x - placed.pivotX, dy = corners[i].y - placed.pivotY;
            const Vec4 pos(placed.pivotX + c*dx - si*dy, placed.pivotY + si*dx + c*dy, 0, 1);
            page.vertices.push_back({matrix * pos, uv[i], color});
        }
        page.indices.insert(page.indices.end(), {base, base+1, base+2, base, base+2, base+3});
        page.dirty = true;
    }
    Font font_;
    std::vector<Page> pages_;
    Direction alignH_ = Direction::Left;
    Direction alignV_ = Direction::Top;
    bool alignToInk_ = false;
};
} // namespace trussc
