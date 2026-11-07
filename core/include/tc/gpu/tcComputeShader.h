#pragma once

#include "../utils/tcAnnotations.h"
#include "../utils/tcOnceGate.h"
#include <cstring>
#include <utility>
#include <unordered_map>
#include <vector>

namespace trussc {

// A compute program owns its pipeline; dispatch runs immediately between passes.
// Like Shader, all GPU operations belong on the main thread.
class TC_LUA_SKIP ComputeShader {
public:
    ComputeShader() = default;
    ~ComputeShader() { clear(); }
    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;
    ComputeShader(ComputeShader&& other) noexcept { moveFrom(std::move(other)); }
    ComputeShader& operator=(ComputeShader&& other) noexcept {
        if (this != &other) { clear(); moveFrom(std::move(other)); }
        return *this;
    }

    bool load(const sg_shader_desc* (*descFn)(sg_backend)) {
        clear();
        if (!available()) return false;
        const auto* desc = descFn ? descFn(sg_query_backend()) : nullptr;
        if (!desc || (!desc->compute_func.source && !desc->compute_func.bytecode.ptr)) {
            logWarning("ComputeShader") << "load requires a compute shader descriptor";
            return false;
        }
        shader_ = sg_make_shader(desc);
        if (sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID) {
            logWarning("ComputeShader") << "Failed to create compute shader";
            clear();
            return false;
        }
        sg_pipeline_desc pipeline = {};
        pipeline.compute = true;
        pipeline.shader = shader_;
        pipeline_ = sg_make_pipeline(&pipeline);
        if (!isLoaded()) {
            logWarning("ComputeShader") << "Failed to create compute pipeline";
            clear();
            return false;
        }
        return true;
    }

    void clear() {
        internal::deferGpuDestroy(pipeline_);
        internal::deferGpuDestroy(shader_);
        pipeline_ = {};
        shader_ = {};
        bindings_ = {};
        uniforms_.clear();
    }

    bool isLoaded() const { return pipeline_.id && sg_isvalid() &&
        sg_query_pipeline_state(pipeline_) == SG_RESOURCESTATE_VALID; }

    // Binding numbers and uniform packing follow Shader. Texture owns the views.
    bool setStorageImage(int slot, const Texture& texture) {
        if (!validSlot(slot)) return false;
        if (!texture.isAllocated() || !texture.getStorageView().id) {
            warn("setStorageImage requires an allocated Storage texture");
            return false;
        }
        bindings_.views[slot] = texture.getStorageView();
        return true;
    }

    bool setTexture(int slot, const Texture& texture) {
        return setTexture(slot, texture.getView(), texture.getSampler());
    }
    bool setTexture(int slot, sg_view view, sg_sampler sampler) {
        if (!validSlot(slot)) return false;
        bindings_.views[slot] = view;
        if (slot < SG_MAX_SAMPLER_BINDSLOTS) bindings_.samplers[slot] = sampler;
        else if (sampler.id) warn("Sampler slot is outside sokol's sampler bindings; sampler ignored");
        return true;
    }

    void setUniform(int slot, float value) { setUniform(slot, Vec4(value, 0, 0, 0)); }
    void setUniform(int slot, const Vec2& value) { setUniform(slot, Vec4(value.x, value.y, 0, 0)); }
    void setUniform(int slot, const Vec3& value) { setUniform(slot, Vec4(value.x, value.y, value.z, 0)); }
    void setUniform(int slot, const Vec4& value) { setUniform(slot, &value, sizeof(value)); }
    void setUniform(int slot, const Color& value) { setUniform(slot, Vec4(value.r, value.g, value.b, value.a)); }
    void setUniform(int slot, const std::vector<float>& values) { setUniform(slot, values.data(), values.size() * sizeof(float)); }
    void setUniform(int slot, const std::vector<Vec2>& values) { setUniform(slot, values.data(), values.size() * sizeof(Vec2)); }
    void setUniform(int slot, const std::vector<Vec3>& values) {
        std::vector<Vec4> padded;
        padded.reserve(values.size());
        for (const auto& value : values) padded.emplace_back(value.x, value.y, value.z, 0);
        setUniform(slot, padded.data(), padded.size() * sizeof(Vec4));
    }
    void setUniform(int slot, const std::vector<Vec4>& values) { setUniform(slot, values.data(), values.size() * sizeof(Vec4)); }
    void setUniform(int slot, const void* data, size_t size) {
        if (slot < 0 || slot >= SG_MAX_UNIFORMBLOCK_BINDSLOTS || (size && !data)) {
            warn("Invalid uniform block slot or data");
            return;
        }
        auto& bytes = uniforms_[slot];
        bytes.resize(size);
        if (size) std::memcpy(bytes.data(), data, size);
    }

    // Counts are workgroups, not pixels. Local size belongs in the shader.
    bool dispatch(int groupsX, int groupsY = 1, int groupsZ = 1) {
        if (!available()) return false;
        if (!isLoaded()) { warn("dispatch requires a loaded compute shader"); return false; }
        // Bound (sokol): each dispatch axis is in [0, 65535]. Zero is a no-op.
        if (groupsX < 0 || groupsY < 0 || groupsZ < 0 ||
            groupsX > 65535 || groupsY > 65535 || groupsZ > 65535) {
            warn("Dispatch group counts must be in [0, 65535]");
            return false;
        }
        if (!groupsX || !groupsY || !groupsZ) return true;
        if (!validateBindings()) return false;
        // Consume pending drawing on every active target before compute writes.
        internal::suspendForImageUpload();
        sg_pass pass = {};
        pass.compute = true;
        sg_begin_pass(&pass);
        sg_apply_pipeline(pipeline_);
        sg_apply_bindings(&bindings_);
        for (const auto& [slot, bytes] : uniforms_) {
            const sg_range range = {bytes.data(), bytes.size()};
            sg_apply_uniforms(slot, &range);
        }
        sg_dispatch(groupsX, groupsY, groupsZ);
        sg_end_pass();
        // The existing continuation reopens the render pass on the next draw.
        return true;
    }

private:
    sg_shader shader_ = {};
    sg_pipeline pipeline_ = {};
    sg_bindings bindings_ = {};
    std::unordered_map<int, std::vector<uint8_t>> uniforms_;
    OnceGate warnings_{5.0};

    void warn(const char* message) {
        if (warnings_.isFirstTime()) logWarning("ComputeShader") << message;
    }
    bool available() {
        if (!sg_isvalid()) { warn("Compute is unavailable without an initialized GPU (including headless apps)"); return false; }
        if (!sg_query_features().compute) { warn("Compute is unsupported by this backend or device"); return false; }
        return true;
    }
    bool validSlot(int slot) {
        if (slot >= 0 && slot < SG_MAX_VIEW_BINDSLOTS) return true;
        warn("View slot is outside sokol's view bindings");
        return false;
    }
    bool validateBindings() {
        const auto desc = sg_query_shader_desc(shader_);
        for (int slot = 0; slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++slot) {
            const auto& block = desc.uniform_blocks[slot];
            const auto found = uniforms_.find(slot);
            if (block.stage == SG_SHADERSTAGE_COMPUTE &&
                (found == uniforms_.end() || found->second.size() != block.size)) {
                warn("Missing uniform block or size does not match shader");
                return false;
            }
            if (found != uniforms_.end() && block.stage != SG_SHADERSTAGE_COMPUTE) {
                warn("Uniform block is not declared by the compute shader");
                return false;
            }
        }
        for (int slot = 0; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) {
            const auto& required = desc.views[slot];
            if (required.texture.stage != SG_SHADERSTAGE_COMPUTE &&
                required.storage_image.stage != SG_SHADERSTAGE_COMPUTE) continue;
            if (sg_query_view_state(bindings_.views[slot]) != SG_RESOURCESTATE_VALID) {
                warn("Missing or invalid compute image binding"); return false;
            }
            const auto view = sg_query_view_desc(bindings_.views[slot]);
            const bool storage = required.storage_image.stage == SG_SHADERSTAGE_COMPUTE;
            const sg_image image = storage ? view.storage_image.image : view.texture.image;
            if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
                warn("Compute image binding has the wrong view type or a retired image"); return false;
            }
            const auto imageDesc = sg_query_image_desc(image);
            const auto caps = sg_query_pixelformat(imageDesc.pixel_format);
            const auto imageType = storage ? required.storage_image.image_type : required.texture.image_type;
            if (imageDesc.type != imageType) {
                warn("Compute image type does not match shader"); return false;
            }
            if (storage) {
                if (imageDesc.pixel_format != required.storage_image.access_format ||
                    !caps.write || (!required.storage_image.writeonly && !caps.read)) {
                    warn("Storage image format or access is unsupported"); return false;
                }
                for (int input = 0; input < SG_MAX_VIEW_BINDSLOTS; ++input) {
                    if (desc.views[input].texture.stage != SG_SHADERSTAGE_COMPUTE) continue;
                    if (sg_query_view_state(bindings_.views[input]) != SG_RESOURCESTATE_VALID) continue;
                    if (sg_query_view_desc(bindings_.views[input]).texture.image.id == image.id) {
                        warn("Compute storage output and sampled input must use separate images"); return false;
                    }
                }
            } else {
                if ((imageDesc.sample_count > 1) != required.texture.multisampled) {
                    warn("Compute texture multisampling does not match shader"); return false;
                }
                if ((required.texture.sample_type == SG_IMAGESAMPLETYPE_FLOAT && !caps.filter) ||
                    (required.texture.sample_type == SG_IMAGESAMPLETYPE_DEPTH && !caps.depth)) {
                    warn("Compute texture sample type is unsupported by its format"); return false;
                }
            }
        }
        for (int slot = 0; slot < SG_MAX_SAMPLER_BINDSLOTS; ++slot) {
            if (desc.samplers[slot].stage == SG_SHADERSTAGE_COMPUTE &&
                sg_query_sampler_state(bindings_.samplers[slot]) != SG_RESOURCESTATE_VALID) {
                warn("Missing or invalid compute sampler"); return false;
            }
        }
        // Sokol retains validation of sampler/texture pair compatibility,
        // integer sample types, view subresources and backend-specific limits.
        return true;
    }
    void moveFrom(ComputeShader&& other) {
        shader_ = std::exchange(other.shader_, {});
        pipeline_ = std::exchange(other.pipeline_, {});
        bindings_ = std::exchange(other.bindings_, {});
        uniforms_ = std::move(other.uniforms_);
    }
};

} // namespace trussc
