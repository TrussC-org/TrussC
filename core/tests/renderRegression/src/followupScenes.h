// #713 slice 1: source-only fixtures also compiled against the old base.
Fbo interruption_;
Texture upload_, mipUpload_;
Pixels uploadRed_, uploadGreen_;

void setupFollowupScenes() {
    interruption_.allocate(32, 32);
    uploadRed_.allocate(2, 2, 4); uploadGreen_.allocate(2, 2, 4);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        uploadRed_.setColor(x, y, Color(1, 0, 0));
        uploadGreen_.setColor(x, y, Color(0, 1, 0));
    }
    upload_.allocate(2, 2, TextureFormat::RGBA8, TextureUsage::Dynamic);
    mipUpload_.allocate(uploadRed_, TextureUsage::Dynamic, true);
}
void interruptDrawing() {
    // Ordinary sgl draws leave the root lazy. Open it so this checks continuation.
    internal::ensureCurrentPass();
    auto& parent = internal::currentTarget();
    const bool offscreen = parent.isFbo;
    check(name()+" parent pass open", parent.inPass && parent.passStartedThisFrame);
    interruption_.begin(0, 0, 1, 1);
    check(name()+" parent pass suspended", !parent.inPass &&
        internal::currentWindowContext().targetStack.back().resumeParent);
    rect(0, 0, 16, 32, Color(1, 0, 1));
    interruption_.end();
    const bool resumed = &internal::currentTarget() == &parent && parent.inPass;
    check(name()+" parent pass resumed", resumed);
    std::printf("CONTINUATION %s target=%s reached=%d\n", name().c_str(),
        offscreen ? "fbo" : "swapchain", resumed);
}
void drawDepthStencil(float w, float h) {
    auto draw = [&](float x, float y, float width, float height, float z, Color color,
                    bool mask, bool testStencil) {
        sgl_matrix_mode_projection(); sgl_push_matrix(); sgl_load_identity();
        sgl_matrix_mode_modelview(); sgl_push_matrix(); sgl_load_identity();
        auto key = internal::PipelineKey::immediate(internal::fillSelection(BlendMode::Disabled, true));
        key.desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        key.desc.depth.write_enabled = true;
        key.desc.stencil.enabled = true;
        key.desc.stencil.ref = 1;
        key.desc.stencil.read_mask = 0xFF;
        key.desc.stencil.write_mask = mask ? 0xFF : 0;
        key.desc.stencil.front.compare = key.desc.stencil.back.compare =
            testStencil ? SG_COMPAREFUNC_EQUAL : SG_COMPAREFUNC_ALWAYS;
        key.desc.stencil.front.pass_op = key.desc.stencil.back.pass_op =
            mask ? SG_STENCILOP_REPLACE : SG_STENCILOP_KEEP;
        if (mask) key.desc.colors[0].write_mask = SG_COLORMASK_NONE;
        sgl_load_pipeline(internal::currentTarget().pipeline(key).immediate);
        sgl_c4f(color.r, color.g, color.b, 1);
        sgl_begin_quads();
        sgl_v3f(2*x/w-1, 1-2*y/h, z);
        sgl_v3f(2*(x+width)/w-1, 1-2*y/h, z);
        sgl_v3f(2*(x+width)/w-1, 1-2*(y+height)/h, z);
        sgl_v3f(2*x/w-1, 1-2*(y+height)/h, z);
        sgl_end();
        sgl_pop_matrix(); sgl_matrix_mode_projection(); sgl_pop_matrix();
        sgl_matrix_mode_modelview();
    };
    draw(16, 16, 96, 64, 0.2f, Color(1, 0, 0), false, false);
    draw(160, 16, 32, 64, 0.1f, Color(0), true, false);
    interruptDrawing(); // Both depth and this stencil mask must survive.
    draw(48, 16, 96, 64, 0.8f, Color(0, 1, 0), false, false);
    draw(144, 16, 80, 64, 0.05f, Color(0, 0, 1), false, true);
    // A separate mask isolates the public upload interruption from the Fbo one.
    draw(160, 96, 32, 64, 0.1f, Color(0), true, false);
    upload_.loadData(uploadRed_);
    draw(144, 96, 80, 64, 0.05f, Color(0, 1, 1), false, true);
    internal::restoreCurrentPipeline();
}
bool drawFollowupScene(float w, float h) {
    if (is("sglFboInterrupt") || is("msaaInterrupted")) {
        for (int i = 0; i < 4; ++i) {
            rect(16 + i*48, 16, 32, 48, Color(i%2 == 0, i%2 != 0, 0));
            setColor(1, 1, 0);
            drawTriangle(16+i*48, 96, 47+i*48, 135, 16+i*48, 144);
            interruptDrawing();
        }
    } else if (is("textureImmediate")) {
        auto drawUpdates = [&](Texture& texture, float y) {
            texture.loadData(uploadRed_);
            setColor(1.0f); texture.draw(16, y, 64, 48);
            // Deliberately no ensureCurrentPass(): the window's first row is lazy.
            texture.loadData(uploadGreen_);
            texture.draw(96, y, 64, 48);
        };
        drawUpdates(upload_, 16);
        drawUpdates(mipUpload_, 96);
    } else if (is("depthStencil")) {
        drawDepthStencil(w, h);
    } else return false;
    return true;
}
void assertFollowupScene(const Pixels& px, bool offscreen, const std::string& label, float s) {
    if (is("sglFboInterrupt") || is("msaaInterrupted")) {
        for (int i = 0; i < 4; ++i)
            expect(px, offscreen, label+" preserved segment "+std::to_string(i), 24+i*48, 32,
                i%2 == 0 ? Rgba{255,0,0,255} : Rgba{0,255,0,255}, s, 0);
        expect(px, offscreen, label+" resolved triangle", 20, 112, {255,255,0,255}, s, 0);
    } else if (is("textureImmediate")) {
        for (int y : {32,112}) {
            expect(px, offscreen, label+" old upload "+std::to_string(y), 32, y, {255,0,0,255}, s, 0);
            expect(px, offscreen, label+" immediate upload "+std::to_string(y), 128, y, {0,255,0,255}, s, 0);
        }
    } else if (is("depthStencil")) {
        expect(px, offscreen, label+" near depth", 80, 32, {255,0,0,255}, s, 0);
        expect(px, offscreen, label+" far depth", 128, 32, {0,255,0,255}, s, 0);
        expect(px, offscreen, label+" stencil inside", 176, 32, {0,0,255,255}, s, 0);
        expect(px, offscreen, label+" stencil outside", 208, 32, {0,0,0,255}, s, 0);
        expect(px, offscreen, label+" upload stencil inside", 176, 112, {0,255,255,255}, s, 0);
        expect(px, offscreen, label+" upload stencil outside", 208, 112, {0,0,0,255}, s, 0);
    }
}
