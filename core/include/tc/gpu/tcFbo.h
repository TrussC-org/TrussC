#pragma once
#include "tc/utils/tcDebugName.h"
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// tcFbo.h - Framebuffer Object (off-screen rendering)
// =============================================================================

// This file is included from TrussC.h
// Requires access to sokol and internal namespace variables
// Texture and HasTexture must be included first

#include "tc/gpu/shaders/fboMipDownsample.glsl.h"

namespace trussc {

// Forward declaration
class Fbo;

namespace internal {

// Exclusive leases per (sampleCount, format): sequential activations reuse a
// context/cache; concurrently active or parked targets each retain their own.
struct FboSharedResources {
    std::vector<std::unique_ptr<RenderTargetResources>> leases;
    std::shared_ptr<PipelineGeneration> generation;
    bool allocationFailed = false;
};

// Mip-downsample resources shared per color format (see Fbo::ensureSharedMip).
struct FboSharedMipResources {
    bool ready = false;   // both shaders usable (internalShaderReady)
    sg_shader shader = {};
    sg_shader blitShader = {};
    sg_buffer vbuf = {};
    sg_sampler sampler = {};
    bool initialized = false;
};

// Both caches are one per process, defined in tcGlobal.cpp. Nothing ever
// destroys the contexts, shaders and pipelines they hold (each Fbo context
// takes a slot in sokol_gl's context pool). Header-inline, each hot reload
// guest generation that drew into an Fbo made a new set in the host's pools,
// and a few reloads later FBO drawing silently stopped (#249).
std::unordered_map<uint64_t, FboSharedResources>& fboSharedMap();
std::unordered_map<uint64_t, FboSharedMipResources>& fboSharedMipMap();

} // namespace internal

// ---------------------------------------------------------------------------
// Fbo Class - inherits from HasTexture
// ---------------------------------------------------------------------------
class Fbo : public HasTexture {
public:
    Fbo() {
        target_.isFbo = true;
        target_.passKind = internal::TargetPassKind::PublicFbo;
        target_.colorFormat = SG_PIXELFORMAT_RGBA8;
        target_.sampleCount = 1;
        internal::fboCount()++;
    }
    ~Fbo() {
        if (target_.activation) {
            logError("Fbo") << "destroying an active Fbo is invalid; end() it first";
            std::terminate(); // rejecting destruction avoids dangling borrowed stack entries
        }
        clear(); internal::fboCount()--;
    }

    // Name this object for MCP inspection; empty names are listed by index.
    void setDebugName(const std::string& name) { debugName_.set(name); }
    const std::string& getDebugName() const { return debugName_.get(); }

    // Non-copyable
    Fbo(const Fbo&) = delete;
    Fbo& operator=(const Fbo&) = delete;

    // Lifetime token for observers that hold a raw pointer to this Fbo
    // (e.g. ScreenRecorder auto-stops when the recorded Fbo is destroyed).
    // Per-object: it deliberately does NOT transfer on move, so an observer
    // watching a moved-from Fbo expires when that shell is destroyed.
    std::shared_ptr<void> lifetimeToken() const { return aliveToken_; }

    // Move-enabled
    Fbo(Fbo&& other) noexcept : Fbo() {
        target_.isFbo = true;
        target_.passKind = internal::TargetPassKind::PublicFbo;
        if (other.target_.activation) {
            logWarning("Fbo") << "move rejected while source is active";
            return;
        }
        debugName_.moveFrom(other.debugName_);
        moveFrom(std::move(other));
    }

    Fbo& operator=(Fbo&& other) noexcept {
        if (this != &other) {
            if (target_.activation || other.target_.activation) {
                logWarning("Fbo") << "move rejected while a target is active";
                return *this;
            }
            clear();
            debugName_.moveFrom(other.debugName_);
            moveFrom(std::move(other));
        }
        return *this;
    }

    // Allocate FBO (MSAA supported, custom pixel format).
    // Size above the device limit or backend-reported resource creation failure
    // releases resources and leaves isAllocated() false. Driver-level out-of-memory
    // may not be reported on every backend.
    //
    // - `sampleCount`: 1, 2, 4, 8, etc. (1 = no MSAA)
    // - `format`: pixel format (default RGBA8 for backward compatibility)
    // - `mipmaps`: when true, the color texture is allocated with a full mip
    //   chain (floor(log2(max(w,h))) + 1 levels) and `end()` automatically
    //   downsamples mip 0 into the remaining levels with a 2x2 box filter.
    //   Use this when the FBO will be sampled at varying scales (e.g. mapped
    //   onto a 3D surface that moves toward/away from the camera) to avoid
    //   aliasing/moiré. When false the FBO behaves exactly as before.
    void allocate(int w, int h, int sampleCount = 1,
                  TextureFormat format = TextureFormat::RGBA8,
                  bool mipmaps = false) {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;

        if (target_.activation) {
            logWarning("Fbo") << "allocate rejected while active";
            return;
        }
        const bool resized = allocated_ && (target_.width != w || target_.height != h ||
            target_.sampleCount != sampleCount || format_ != format || mipmaps_ != mipmaps);
        if (resized) {
            internal::invalidateTargetBucket(target_.colorFormat, target_.sampleCount, target_.depthFormat);
            internal::invalidateTargetBucket(toSokolFormat(format), sampleCount, target_.depthFormat);
        }
        clear();

        if (!sg_isvalid()) return;
        const int limit = sg_query_limits().max_image_size_2d;
        auto failed = [&] {
            clear();
            warnAllocationFailure_(w, h);
        };
        // Bound (gpu): device texture limit bounds dimensions before resource creation and mip arithmetic.
        if (w <= 0 || h <= 0 || w > limit || h > limit) {
            clear();
            if (w <= 0 || h <= 0) {
                logWarning("Fbo") << "allocate failed: size " << w << "x" << h << " is not positive";
            } else {
                logWarning("Fbo") << "allocate failed: size " << w << "x" << h
                    << " exceeds the device texture limit " << limit;
            }
            return;
        }

        target_.width = w;
        target_.height = h;
        target_.camera = {}; // invalidate size-derived camera data
        target_.viewport = {0, 0, (float)w, (float)h, true};
        target_.sampleCount = sampleCount;
        format_ = format;
        target_.colorFormat = toSokolFormat(format);
        mipmaps_ = mipmaps;
        // Compute the full mip chain length when requested. floor(log2(max)) + 1
        // matches the standard convention (e.g. 512 → 10 levels, 1×1 the last).
        numMipLevels_ = mipmaps_
            ? 1 + (int)std::floor(std::log2((float)std::max(w, h)))
            : 1;
        sg_pixel_format sgFormat = toSokolFormat(format);

        // MSAA case
        if (target_.sampleCount > 1) {
            // MSAA color texture (render target)
            sg_image_desc msaa_desc = {};
            msaa_desc.usage.color_attachment = true;
            msaa_desc.width = w;
            msaa_desc.height = h;
            msaa_desc.pixel_format = sgFormat;
            msaa_desc.sample_count = target_.sampleCount;
            msaaColorImage_ = sg_make_image(&msaa_desc);

            if (sg_query_image_state(msaaColorImage_) != SG_RESOURCESTATE_VALID) {
                failed();
                return;
            }

            // MSAA color attachment view
            sg_view_desc msaa_att_desc = {};
            msaa_att_desc.color_attachment.image = msaaColorImage_;
            msaaColorAttView_ = sg_make_view(&msaa_att_desc);

            // Resolve color texture (non-MSAA, for reading/display)
            colorTexture_.allocate(w, h, format, TextureUsage::RenderTarget, 1, numMipLevels_);

            if (!textureReady_(colorTexture_)) { failed(); return; }

            // Resolve view (must be created as resolve_attachment)
            sg_view_desc resolve_view_desc = {};
            resolve_view_desc.resolve_attachment.image = colorTexture_.getImage();
            resolveAttView_ = sg_make_view(&resolve_view_desc);

            // MSAA depth buffer
            sg_image_desc depth_desc = {};
            depth_desc.usage.depth_stencil_attachment = true;
            depth_desc.width = w;
            depth_desc.height = h;
            depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            depth_desc.sample_count = target_.sampleCount;
            depthImage_ = sg_make_image(&depth_desc);
        } else {
            // Non-MSAA case
            colorTexture_.allocate(w, h, format, TextureUsage::RenderTarget, 1, numMipLevels_);

            if (!textureReady_(colorTexture_)) { failed(); return; }

            // Depth buffer
            sg_image_desc depth_desc = {};
            depth_desc.usage.depth_stencil_attachment = true;
            depth_desc.width = w;
            depth_desc.height = h;
            depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            depth_desc.sample_count = 1;
            depthImage_ = sg_make_image(&depth_desc);
        }

        // Mips always use resolved, single-sample scratch attachments.
        if (mipmaps_) mipScratchTexture_.allocate(w, h, format, TextureUsage::RenderTarget, 1, numMipLevels_);

        if (sg_query_image_state(depthImage_) != SG_RESOURCESTATE_VALID ||
            (mipmaps_ && !textureReady_(mipScratchTexture_)) ||
            (target_.sampleCount > 1 &&
             (sg_query_view_state(msaaColorAttView_) != SG_RESOURCESTATE_VALID ||
              sg_query_view_state(resolveAttView_) != SG_RESOURCESTATE_VALID))) {
            failed();
            return;
        }

        // Depth attachment view
        sg_view_desc depth_att_desc = {};
        depth_att_desc.depth_stencil_attachment.image = depthImage_;
        depthAttView_ = sg_make_view(&depth_att_desc);
        if (sg_query_view_state(depthAttView_) != SG_RESOURCESTATE_VALID) {
            failed();
            return;
        }

        // FBO color texture stores premultiplied alpha (due to alpha blending + MSAA resolve)
        colorTexture_.setPremultipliedAlpha(true);

        allocated_ = true;
    }

    // Release resources
    void clear() {
        if (target_.activation) {
            logWarning("Fbo") << "clear rejected while active";
            return;
        }
        // Also release partial allocations, before allocated_ is published.
        // Shared context/pipelines are NOT destroyed here (shared across FBOs)
        if (depthAttView_.id) sg_destroy_view(depthAttView_);
        if (depthImage_.id) sg_destroy_image(depthImage_);

        if (target_.sampleCount > 1) {
            if (resolveAttView_.id) sg_destroy_view(resolveAttView_);
            if (msaaColorAttView_.id) sg_destroy_view(msaaColorAttView_);
            if (msaaColorImage_.id) sg_destroy_image(msaaColorImage_);
        }

        // Published textures may still be referenced by draws recorded this frame.
        // Failed-attempt textures were never exposed and must free pool slots now.
        colorTexture_.clearResources_(allocated_);
        mipScratchTexture_.clearResources_(allocated_);

        // Version pool: destroy through the deferred queue (inside
        // Texture::clear / deferGpuDestroy) — draws recorded this frame
        // may still sample these textures.
        for (ColorVersion& v : versionPool_) {
            internal::deferGpuDestroy(v.resolveAttView);
            v.tex.clear();
        }
        versionPool_.clear();

        allocated_ = false;
        target_.width = 0;
        target_.height = 0;
        target_.viewport = {0, 0, 0, 0, false};
        target_.sampleCount = 1;
        format_ = TextureFormat::RGBA8;
        target_.colorFormat = SG_PIXELFORMAT_RGBA8;
        mipmaps_ = false;
        numMipLevels_ = 1;
        msaaColorImage_ = {};
        msaaColorAttView_ = {};
        resolveAttView_ = {};
        depthImage_ = {};
        depthAttView_ = {};
        currentVersion_ = 0;
        poolFrame_ = UINT64_MAX;
        lastDrawFrame_ = UINT64_MAX;
        drawnThisFrame_ = false;
        poolAllocFailed_ = false;
        versionNoticeShown_ = false;
    }

    // Begin rendering to FBO (preserves previous content)
    void begin() {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;
        beginInternal(0.0f, 0.0f, 0.0f, 0.0f, false);  // LOAD: keep previous content
    }

    // Begin with specified background color (clears FBO)
    void begin(float r, float g, float b, float a = 1.0f) {
        // Skip in headless mode (no graphics context)
        if (headless::isActive()) return;
        beginInternal(r, g, b, a, true);  // CLEAR: fill with specified color
    }

    // Change background color during FBO rendering (restart pass)
    // Called from tc::clear()
    void clearColor(float r, float g, float b, float a) {
        if (!target_.activation || target_.activeWindow != &internal::currentWindowContext() ||
            internal::currentWindowContext().currentTarget != &target_) return;
        target_.restartClear(r, g, b, a);
    }

    // End only this Fbo's top activation, in its originating window.
    void end() {
        if (!target_.activation) return;
        internal::popTarget(target_, target_.activation);
    }

    // Always read RGBA8, whatever the Fbo format: clamp to 0–1 and quantise
    // to 8 bits, round to nearest (CPU ties round up; native desktop GL may
    // differ by at most 1). Use readPixelsFloat() for full precision.
    // Buffer must hold width * height * 4 bytes. R/RG fill missing G/B with 0 and A with 1.
    // WebGL2 supports this API; WebGPU synchronous readback is unsupported.
    // Note: Call after rendering is complete (after end()); inside an Fbo pass it returns false
    // For MSAA, reads from resolved texture
    TC_PLATFORMS("macos,windows,linux,ios,android,web") bool readPixels(unsigned char* pixels) const {
        if (!allocated_ || !pixels) return false;
        if (internal::hasPublicFboActivation()) {
            logError("Fbo") << "readPixels() inside fbo.begin()/end() is not supported; call it after end()";
            return false;
        }

        // sokol_gfx doesn't have direct pixel reading API
        // Platform-specific implementation required
        // colorTexture_ is always non-MSAA (resolved) so read from there
        return readPixelsPlatform(pixels);
    }

    // Read pixel data as float (for float pixel formats: R16F, R32F, RGBA16F, RGBA32F, etc.)
    // Buffer must be large enough: width * height * channelCount(format) floats
    // Note: Call after rendering is complete (after end()); inside an Fbo pass it returns false
    TC_PLATFORMS("macos,ios,windows,linux,android,web") bool readPixelsFloat(float* pixels) const {
        if (!allocated_ || !pixels) return false;
        if (internal::hasPublicFboActivation()) {
            logError("Fbo") << "readPixels() inside fbo.begin()/end() is not supported; call it after end()";
            return false;
        }
        return readPixelsFloatPlatform(pixels);
    }

    // Copy to Image
    bool copyTo(Image& image) const {
        if (!allocated_) return false;

        image.allocate(target_.width, target_.height, 4);
        bool result = readPixels(image.getPixelsData());
        if (result) {
            image.setDirty();
            image.update();
        }
        return result;
    }

    // Size and state getters
    int getWidth() const { return target_.getWidth(); }
    int getHeight() const { return target_.getHeight(); }
    int getSampleCount() const { return target_.sampleCount; }
    TextureFormat getTextureFormat() const { return format_; }
    bool isAllocated() const { return allocated_; }
    bool isActive() const { return target_.activation != 0; }

    // === HasTexture implementation ===

    // getTexture() always returns the non-MSAA texture (for drawing/reading)
    // of the CURRENT version: when this Fbo was re-rendered after being drawn
    // earlier in the same frame, that is the version-pool texture holding the
    // latest content — draws recorded before the re-render keep sampling the
    // older version they captured (see version pool contract below).
    Texture& getTexture() override { diagnoseActiveSampling_(); return curColorTex_(); }
    const Texture& getTexture() const override { diagnoseActiveSampling_(); return curColorTex_(); }

    // GL backends store FBO color textures bottom-to-top in memory
    // (unlike Metal/D3D11 which are top-to-bottom). Override draw to
    // flip V on GL so the displayed image matches user-space top-left coords.
    // Version-pool textures take the exact same path (Texture::drawFlippedY /
    // Texture::draw), so the GL Y-flip applies to every version equally.
    void draw(float x, float y) const override {
        if (!hasTexture()) return;
        markDrawnThisFrame_();
        const Texture& tex = curColorTex_();
        if (needsGlYFlip()) {
            tex.drawFlippedY(x, y, (float)target_.width, (float)target_.height);
        } else {
            tex.draw(x, y);
        }
    }

    void draw(float x, float y, float w, float h) const override {
        if (!hasTexture()) return;
        markDrawnThisFrame_();
        const Texture& tex = curColorTex_();
        if (needsGlYFlip()) {
            tex.drawFlippedY(x, y, w, h);
        } else {
            tex.draw(x, y, w, h);
        }
    }

    // save() override - Save FBO contents to file
    SaveResult save(const fs::path& path) const override {
        Image img;
        if (copyTo(img)) {
            return img.save(path);
        }
        return SaveResult::fail(SaveError::WriteFailed, "Cannot read Fbo pixels");
    }

    // Access to internal resources (for advanced users).
    // Like getTexture(), these refer to the CURRENT version's texture.
    sg_image getColorImage() const { return curColorTex_().getImage(); }
    sg_view getTextureView() const { return curColorTex_().getView(); }
    sg_sampler getSampler() const { return curColorTex_().getSampler(); }

private:
    internal::DebugName debugName_{internal::DebugObjectKind::Fbo, this};
    static bool needsGlYFlip() {
        sg_backend be = sg_query_backend();
        return be == SG_BACKEND_GLCORE || be == SG_BACKEND_GLES3;
    }

    internal::RenderTarget target_;
    mutable bool warnedActiveSampling_ = false;
    TextureFormat format_ = TextureFormat::RGBA8;
    bool allocated_ = false;
    bool mipmaps_ = false;
    int  numMipLevels_ = 1;

    // Non-MSAA texture (always used, resolve target for MSAA)
    Texture colorTexture_;
    // Scratch texture for mipmap generation (avoids sokol validation error
    // when sampling and writing to different mips of the same image).
    Texture mipScratchTexture_;

    // MSAA resources (only used when sampleCount > 1)
    sg_image msaaColorImage_ = {};
    sg_view msaaColorAttView_ = {};
    sg_view resolveAttView_ = {};

    // Common resources
    sg_image depthImage_ = {};
    sg_view depthAttView_ = {};

    // =========================================================================
    // Same-frame revision version pool (issue #189)
    //
    // TrussC records draws and replays them at end-of-frame flush. Without
    // versioning, an Fbo that is drawn, re-rendered, and drawn again within
    // one frame shows the FINAL content in both draws — every recorded quad
    // samples the same image. The pool gives each revision its own texture so
    // each recorded draw shows the content that existed at submission time.
    //
    // Contract:
    // - Version 0 is `colorTexture_` (+ `resolveAttView_` when MSAA).
    //   Versions 1..N live in `versionPool_`, allocated lazily and EXACTLY
    //   matching the primary color texture: size, format, mip levels,
    //   filter/wrap. For MSAA they are non-MSAA RESOLVE targets only — the
    //   MSAA color render target and the depth buffer are consumed inside the
    //   pass and shared across ALL versions (never duplicated).
    // - begin() advances to the next version iff this Fbo was drawn earlier
    //   in the same frame. draw()/getTexture() always use the CURRENT version.
    // - The pool persists across frames (no realloc churn). On the first
    //   frame sync of a new frame, the previous frame's commands have flushed,
    //   so all versions are reusable: the latest version's handles are swapped
    //   into the primary slot (handle swap only — no pixel copies) and the
    //   cursor resets to 0. This keeps content rendered once (e.g. in setup())
    //   visible on all later frames.
    // - On version allocation failure (GPU out of memory) versioning is
    //   disabled for this Fbo and rendering continues into the current
    //   texture — the legacy both-draws-show-final behavior — instead of
    //   crashing. Texture::createResources already logs the error.
    // =========================================================================

    struct ColorVersion {
        Texture tex;                  // matches colorTexture_ exactly
        sg_view resolveAttView = {};  // only used when target_.sampleCount > 1
    };
    std::vector<ColorVersion> versionPool_;        // versions 1..N (0 = colorTexture_)
    int currentVersion_ = 0;                       // cursor; 0 = primary colorTexture_
    uint64_t poolFrame_ = UINT64_MAX;              // frame the cursor was last synced to
    mutable uint64_t lastDrawFrame_ = UINT64_MAX;  // frame of the last fbo draw
    mutable bool drawnThisFrame_ = false;          // drawn since last begin()/frame sync
    bool poolAllocFailed_ = false;                 // sticky: versioning disabled after OOM
    bool versionNoticeShown_ = false;              // one-time 10-version notice emitted

    Texture& curColorTex_() {
        return currentVersion_ == 0 ? colorTexture_ : versionPool_[currentVersion_ - 1].tex;
    }
    const Texture& curColorTex_() const {
        return currentVersion_ == 0 ? colorTexture_ : versionPool_[currentVersion_ - 1].tex;
    }
    sg_view curResolveAttView_() const {
        return currentVersion_ == 0 ? resolveAttView_ : versionPool_[currentVersion_ - 1].resolveAttView;
    }

    // Called from the draw() overloads: the current version's content is now
    // referenced by a recorded command, so a subsequent begin() in the same
    // frame must switch to a fresh version.
    void diagnoseActiveSampling_() const {
        if (target_.activation) {
            if (!warnedActiveSampling_) {
                warnedActiveSampling_ = true;
                logWarning("Fbo") << "sampling an active Fbo attachment is invalid; end() it first";
            }
        }
    }
    void markDrawnThisFrame_() const {
        diagnoseActiveSampling_();
        drawnThisFrame_ = true;
        lastDrawFrame_ = internal::currentWindowContext().recordingCount;
    }

    // Recycle after this context commits its recording, including offscreen
    // work and rejected presentation. Foreign commits leave pending versions
    // intact. Keep one Fbo to one context per recording: cross-window sampling
    // and rendering of the same Fbo still has no version ownership protocol.
    void syncVersionFrame_() {
        uint64_t fc = internal::currentWindowContext().recordingCount;
        if (fc == poolFrame_) return;
        poolFrame_ = fc;
        // Promote the latest content into the primary slot by swapping
        // handles (moves only, no pixel copies) so persistent content — e.g.
        // an Fbo rendered once in setup() — survives the cursor reset.
        if (currentVersion_ > 0) {
            ColorVersion& v = versionPool_[currentVersion_ - 1];
            std::swap(colorTexture_, v.tex);
            std::swap(resolveAttView_, v.resolveAttView);
            currentVersion_ = 0;
        }
        // A draw already recorded THIS frame (before the first begin()) still
        // counts as drawn-this-frame.
        drawnThisFrame_ = (lastDrawFrame_ == fc);
    }

    // Backend failures within the size limit (pool or resource creation).
    static void warnAllocationFailure_(int w, int h) {
        logWarning("Fbo") << "allocate failed: the GPU rejected the " << w << "x" << h << " attachments";
    }

    // Texture::isAllocated alone does not validate its views or sampler.
    static bool textureReady_(const Texture& tex) {
        if (!tex.isAllocated() ||
            sg_query_image_state(tex.getImage()) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(tex.getView()) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(tex.getAttachmentView()) != SG_RESOURCESTATE_VALID ||
            sg_query_sampler_state(tex.getSampler()) != SG_RESOURCESTATE_VALID) return false;
        for (int level = 0; level < tex.getNumMipLevels(); ++level) {
            if (sg_query_view_state(tex.getAttachmentViewForMip(level)) != SG_RESOURCESTATE_VALID ||
                sg_query_view_state(tex.getViewForMip(level)) != SG_RESOURCESTATE_VALID) return false;
        }
        return true;
    }

    // Advance the cursor to the next version, allocating it lazily.
    // Returns false (cursor unchanged) when versioning is degraded by an
    // earlier allocation failure or when the new allocation fails now.
    bool advanceVersion_() {
        if (poolAllocFailed_) return false;
        int next = currentVersion_ + 1;
        if ((int)versionPool_.size() < next) {
            ColorVersion v;
            // Match the primary color texture exactly. Filters/wrap are set
            // before allocate() so the sampler is created once with the right
            // settings (Texture::allocate keeps them across its clear()).
            v.tex.setMinFilter(colorTexture_.getMinFilter());
            v.tex.setMagFilter(colorTexture_.getMagFilter());
            v.tex.setWrapU(colorTexture_.getWrapU());
            v.tex.setWrapV(colorTexture_.getWrapV());
            v.tex.allocate(target_.width, target_.height, format_,
                           TextureUsage::RenderTarget, 1, numMipLevels_);
            if (!textureReady_(v.tex)) {
                warnAllocationFailure_(target_.width, target_.height);
                // Degrade
                // gracefully: keep rendering into the current texture
                // (accepting the legacy both-draws-show-final behavior).
                poolAllocFailed_ = true;
                v.tex.clearResources_(false);
                return false;
            }
            v.tex.setPremultipliedAlpha(true);
            if (target_.sampleCount > 1) {
                sg_view_desc rd = {};
                rd.resolve_attachment.image = v.tex.getImage();
                v.resolveAttView = sg_make_view(&rd);
                if (sg_query_view_state(v.resolveAttView) != SG_RESOURCESTATE_VALID) {
                    if (v.resolveAttView.id) sg_destroy_view(v.resolveAttView);
                    v.tex.clearResources_(false);
                    warnAllocationFailure_(target_.width, target_.height);
                    poolAllocFailed_ = true;
                    return false;
                }
            }
            versionPool_.push_back(std::move(v));
        }
        currentVersion_ = next;
        if (!versionNoticeShown_ && next >= 10) {
            versionNoticeShown_ = true;
            logNotice("Fbo") << "this Fbo was re-rendered " << next
                << " times after being drawn this frame - each revision needs "
                   "its own texture; check your draw loop if unintended";
        }
        return true;
    }

    // GL without native texture views binds the entire image: its shader LOD
    // must remain relative to the image rather than to the requested view.
    static void applyMipSource_(int level) {
        const auto backend = sg_query_backend();
        const bool wholeImage = (backend == SG_BACKEND_GLCORE || backend == SG_BACKEND_GLES3)
            && !sg_query_features().gl_texture_views;
        tc_fbomip_mip_params_t params = {};
        params.sourceMip[0] = wholeImage ? float(level) : 0.0f;
        sg_apply_uniforms(UB_tc_fbomip_mip_params, SG_RANGE(params));
    }

    // Copy src's mip 0 into dst's mip 0 with the shared 1:1 blit pipeline.
    // Used only for begin() WITHOUT clear on a non-MSAA Fbo right after a
    // version switch: LOAD semantics require the previous content to be
    // present in the (undefined) fresh version texture. The MSAA path needs
    // no copy — its LOAD happens on the shared MSAA color render target.
    void blitColorInto_(const Texture& src, Texture& dst) {
        ensureSharedMip(format_);
        auto& s = getSharedMip(format_);
        if (!s.ready) return;

        sg_pass pass = {};
        pass.attachments.colors[0] = dst.getAttachmentView();
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        internal::TargetPassScope scope(pass, dst.getWidth(), dst.getHeight(), target_.colorFormat);
        if (!scope.active()) return;

        if (!applyMipPipeline_(s.blitShader, internal::PipelineRole::Blit)) return;
        sg_bindings bind = {};
        bind.vertex_buffers[0] = s.vbuf;
        bind.views[VIEW_tc_fbomip_srcTex] = src.getViewForMip(0);
        bind.samplers[SMP_tc_fbomip_srcSmp] = s.sampler;
        sg_apply_bindings(&bind);
        applyMipSource_(0);
        sg_draw(0, 4, 1);
    }

    // -------------------------------------------------------------------------
    // Shared mip-downsample resources (keyed by color format).
    // Independent of the sgl_context / sampleCount of SharedResources because
    // mip generation always runs on the resolved (non-MSAA) color texture
    // with a tiny custom pipeline, not through sokol_gl. The cache is
    // internal::fboSharedMipMap(), one per process.
    // -------------------------------------------------------------------------
    using SharedMipResources = internal::FboSharedMipResources;

    static SharedMipResources& getSharedMip(TextureFormat format) {
        return internal::fboSharedMipMap()[(uint64_t)format];
    }

    static void ensureSharedMip(TextureFormat format) {
        auto& s = getSharedMip(format);
        if (s.initialized) return;



        // Fullscreen quad (triangle strip). UV 0..1 across the quad so a
        // bilinear sample of the previous mip is enough to produce a 2x2
        // box-filtered destination level. textureLod() in the shader pins
        // the source level, so a single pipeline handles every level.
        static const float quadVerts[] = {  // immutable
            // x,     y,    u,    v
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
        };
        sg_buffer_desc vbuf_desc = {};
        vbuf_desc.usage.vertex_buffer = true;
        vbuf_desc.data.ptr = quadVerts;
        vbuf_desc.data.size = sizeof(quadVerts);
        s.vbuf = sg_make_buffer(&vbuf_desc);

        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        s.sampler = sg_make_sampler(&smp_desc);

        s.shader = sg_make_shader(tc_fbomip_downsample_shader_desc(sg_query_backend()));
        s.blitShader = sg_make_shader(tc_fbomip_blit_shader_desc(sg_query_backend()));
        s.ready = internal::internalShaderReady(s.shader, "Fbo mipmap")
               && internal::internalShaderReady(s.blitShader, "Fbo mipmap blit");
        if (!s.ready) {
            s.initialized = true;   // not retried; mipmaps/blits are skipped
            return;
        }

        s.initialized = true;
    }

    static bool applyMipPipeline_(sg_shader shader, internal::PipelineRole role) {
        sg_pipeline_desc desc = {};
        desc.shader = shader;
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        const auto selected = internal::currentTarget().pipeline(internal::PipelineKey::raw(role, desc));
        if (!selected.raw.id) return false;
        sg_apply_pipeline(selected.raw);
        return true;
    }

    // Downsample mip 0 into mips 1..N-1. Called from end() when the FBO was
    // allocated with mipmaps = true.
    //
    // sokol forbids sampling from an image that has a mip level bound as a
    // color attachment in the same pass (even if different mip levels are
    // involved). We work around this with a two-pass-per-level approach:
    //   Pass 1 (downsample): main[level-1] → scratch[level]
    //   Pass 2 (blit):       scratch[level] → main[level]
    void generateMipmaps_() {
        if (!mipmaps_ || numMipLevels_ <= 1) return;
        ensureSharedMip(format_);
        auto& s = getSharedMip(format_);
        if (!s.ready) return;

        for (int level = 1; level < numMipLevels_; level++) {
            // Pass 1: downsample main[level-1] → scratch[level]
            {
                sg_pass pass = {};
                pass.attachments.colors[0] = mipScratchTexture_.getAttachmentViewForMip(level);
                pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
                internal::TargetPassScope scope(pass, std::max(1, target_.width >> level),
                    std::max(1, target_.height >> level), target_.colorFormat);
                if (!scope.active()) return;

                if (!applyMipPipeline_(s.shader, internal::PipelineRole::Mip)) return;

                sg_bindings bind = {};
                bind.vertex_buffers[0] = s.vbuf;
                bind.views[VIEW_tc_fbomip_srcTex] = curColorTex_().getViewForMip(level - 1);
                bind.samplers[SMP_tc_fbomip_srcSmp] = s.sampler;
                sg_apply_bindings(&bind);
                applyMipSource_(level - 1);
                sg_draw(0, 4, 1);
            }

            // Pass 2: blit scratch[level] → main[level]
            {
                sg_pass pass = {};
                pass.attachments.colors[0] = curColorTex_().getAttachmentViewForMip(level);
                pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
                internal::TargetPassScope scope(pass, std::max(1, target_.width >> level),
                    std::max(1, target_.height >> level), target_.colorFormat);
                if (!scope.active()) return;

                if (!applyMipPipeline_(s.blitShader, internal::PipelineRole::Blit)) return;

                sg_bindings bind = {};
                bind.vertex_buffers[0] = s.vbuf;
                bind.views[VIEW_tc_fbomip_srcTex] = mipScratchTexture_.getViewForMip(level);
                bind.samplers[SMP_tc_fbomip_srcSmp] = s.sampler;
                sg_apply_bindings(&bind);
                applyMipSource_(level);
                sg_draw(0, 4, 1);
            }
        }
    }

    void finishActivation_() {
        if (mipmaps_) generateMipmaps_();
        if (!target_.matrixStack.empty() || !target_.cameraStack.empty() || !target_.scissorStack.empty()) {
            static OnceGate warned;
            if (warned.isFirstTime()) logWarning("Fbo") << "end() discarded unmatched matrix, camera or scissor pushes";
        }
        target_.matrixStack.clear();
        target_.cameraStack.clear();
        target_.scissorStack.clear();
    }

    void beginInternal(float r, float g, float b, float a, bool doClear) {
        if (!allocated_) return;
        if (sg_query_view_state(depthAttView_) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(target_.sampleCount > 1 ? msaaColorAttView_ : curColorTex_().getAttachmentView()) != SG_RESOURCESTATE_VALID ||
            (target_.sampleCount > 1 && sg_query_view_state(curResolveAttView_()) != SG_RESOURCESTATE_VALID)) {
            logWarning("Fbo") << "begin rejected: invalid attachments";
            return;
        }
        if (target_.activation) {
            logWarning("Fbo") << "recursive or cross-window begin rejected";
            return;
        }
        // Validate and lease before interrupting the parent. Parked leases are
        // never reused, including for nesting with identical format/samples.
        auto* lease = internal::leaseTargetResources(target_.colorFormat, target_.sampleCount);
        if (!lease) return;
        target_.pooledResources = lease;
        target_.inheritLogicalDrawFlags(internal::currentTarget());
        target_.camera = {};
        target_.cameraStack.clear();
        target_.currentMatrix = Mat4::identity();
        target_.matrixStack.clear();
        target_.currentScissor = {0, 0, 0, 0, false};
        target_.scissorStack.clear();
        target_.viewport = {0, 0, float(target_.width), float(target_.height), true};
        target_.sglLayerNext = 0;
        target_.openShaderScopes = 0;
        target_.selectedPipeline = internal::fillSelection(target_.currentBlendMode, target_.depthTestEnabled);
        target_.owner = this;
        target_.finalize = [](void* owner) { static_cast<Fbo*>(owner)->finishActivation_(); };
        // A preparation entry parks the outgoing parent while version-copy
        // scopes run, and cannot be resumed by those scopes.
        uint64_t token = 0;
        try { token = internal::pushTarget(target_, true); }
        catch (...) {
            lease->leased = false;
            target_.pooledResources = nullptr;
            throw;
        }
        if (!token) {
            lease->leased = false;
            target_.pooledResources = nullptr;
            return;
        }
        try {
            sgl_tc_reset_matrix_stacks();
            sgl_tc_context_ensure_buffers(lease->context);
            sgl_defaults();
            syncVersionFrame_();
            if (drawnThisFrame_) {
                const int previous = currentVersion_;
                if (advanceVersion_() && !doClear && target_.sampleCount == 1) {
                    const Texture& src = previous == 0 ? colorTexture_ : versionPool_[previous - 1].tex;
                    blitColorInto_(src, curColorTex_());
                }
            }
            drawnThisFrame_ = false;
            target_.binding = {};
            if (target_.sampleCount > 1) {
                target_.binding.attachments.colors[0] = msaaColorAttView_;
                target_.binding.attachments.resolves[0] = curResolveAttView_();
            } else {
                target_.binding.attachments.colors[0] = curColorTex_().getAttachmentView();
            }
            target_.binding.attachments.depth_stencil = depthAttView_;
            target_.clearAction = {};
            target_.clearAction.colors[0].load_action = doClear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
            target_.clearAction.colors[0].clear_value = {r, g, b, a};
            target_.clearAction.depth.load_action = SG_LOADACTION_CLEAR;
            target_.clearAction.depth.clear_value = 1;
            internal::setupScreenFovWithSize(TAU / 8, float(target_.width), float(target_.height), 0, 0, false);
            // Match the base: camera setup selects 3D; a fresh Fbo starts in
            // the inherited 2D blend mode, including its logical depth flag.
            internal::loadPipeline(internal::active2D(target_.currentBlendMode));
            if (!internal::startPreparedTarget(target_, token)) {
                target_.finalize = nullptr;
                internal::popTarget(target_, token);
            }
        } catch (...) {
            target_.finalize = nullptr;
            internal::popTarget(target_, token);
            throw;
        }
    }

private:
    std::shared_ptr<void> aliveToken_ = std::make_shared<char>();

    void moveFrom(Fbo&& other) {
        // aliveToken_ intentionally not moved (see lifetimeToken())
        target_ = std::move(other.target_);
        target_.owner = this;
        format_ = other.format_;
        allocated_ = other.allocated_;
        mipmaps_ = other.mipmaps_;
        numMipLevels_ = other.numMipLevels_;
        colorTexture_ = std::move(other.colorTexture_);
        mipScratchTexture_ = std::move(other.mipScratchTexture_);
        versionPool_ = std::move(other.versionPool_);
        currentVersion_ = other.currentVersion_;
        poolFrame_ = other.poolFrame_;
        lastDrawFrame_ = other.lastDrawFrame_;
        drawnThisFrame_ = other.drawnThisFrame_;
        poolAllocFailed_ = other.poolAllocFailed_;
        versionNoticeShown_ = other.versionNoticeShown_;
        msaaColorImage_ = other.msaaColorImage_;
        msaaColorAttView_ = other.msaaColorAttView_;
        resolveAttView_ = other.resolveAttView_;
        depthImage_ = other.depthImage_;
        depthAttView_ = other.depthAttView_;

        other.allocated_ = false;
        other.mipmaps_ = false;
        other.numMipLevels_ = 1;
        other.versionPool_.clear();
        other.currentVersion_ = 0;
        other.poolFrame_ = UINT64_MAX;
        other.lastDrawFrame_ = UINT64_MAX;
        other.drawnThisFrame_ = false;
        other.poolAllocFailed_ = false;
        other.versionNoticeShown_ = false;
        other.target_ = internal::RenderTarget{};
        other.target_.isFbo = true;
        other.target_.passKind = internal::TargetPassKind::PublicFbo;
        other.target_.colorFormat = SG_PIXELFORMAT_RGBA8;
        other.target_.sampleCount = 1;
        other.format_ = TextureFormat::RGBA8;
        other.msaaColorImage_ = {};
        other.msaaColorAttView_ = {};
        other.resolveAttView_ = {};
        other.depthImage_ = {};
        other.depthAttView_ = {};
    }

    // Platform-specific pixel reading (implemented in platform files)
    friend class VideoWriter;
    bool readPixelsPlatform(unsigned char* pixels, bool reportError = true) const;
    bool readPixelsFloatPlatform(float* pixels) const;
};

} // namespace trussc
