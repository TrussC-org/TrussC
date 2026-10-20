// Deterministic per-target fixtures. fixture.ttf contains only a synthetic A
// triangle, generated with fontFaceIndex's sfnt builder (no external font data).


bool approxEq(float a, float b) { return std::abs(a - b) < 0.001f; }
void expectPixel(const Pixels& pixels, int x, int y, Color expected, const char* label) {
    const auto actual = pixels.getColor(x, y);
    const bool matches = std::abs(actual.r - expected.r) < 0.02f &&
                         std::abs(actual.g - expected.g) < 0.02f &&
                         std::abs(actual.b - expected.b) < 0.02f;
    check(label, matches);
    if (!matches) std::printf("  pixel (%d,%d) actual=(%f,%f,%f) expected=(%f,%f,%f)\n", x, y,
                             double(actual.r), double(actual.g), double(actual.b),
                             double(expected.r), double(expected.g), double(expected.b));
}
Pixels readTarget(Fbo& target) {
    Pixels pixels; pixels.allocate(target.getWidth(), target.getHeight(), 4);
    check("fixture readback", target.readPixels(pixels.getData()));
    return pixels;
}
class ClipFixture : public App {
    void draw() override {
        const auto& clip = internal::currentTarget().currentScissor;
        check("RectNode emits attachment-pixel clip (10,10,20,20)", clip.active &&
              clip.x == 10 && clip.y == 10 && clip.w == 20 && clip.h == 20);
        std::printf("RECTNODE clip=(%g,%g,%g,%g) active=%d\n", clip.x, clip.y, clip.w, clip.h, int(clip.active));
        setColor(0, 1, 0); drawRect(-10, -10, 50, 50);
    }
};
// Runs with the real native window/DPI, before any simulated context fields.
// A Retina 256x192 logical window allocates a 512x384 attachment here.
void runNativeClipChecks() {
    const bool savedPixelMode = internal::pixelPerfectMode();
    Fbo target;
    target.allocate(getFramebufferWidth(), getFramebufferHeight(), 1);
    for (bool pixelMode : {false, true}) {
        internal::pixelPerfectMode() = pixelMode;
        std::printf("RECTNODE native hostDpi=%g framebuffer=%dx%d fbo=%dx%d samples=%d pixelPerfect=%d\n",
                    getDpiScale(), getFramebufferWidth(), getFramebufferHeight(), target.getWidth(), target.getHeight(),
                    target.getSampleCount(), int(pixelMode));
        target.begin(0, 0, 0); setupScreenOrtho(); setBlendMode(BlendMode::Alpha); disableDepthTest();
        check("native RectNode Fbo matches real framebuffer and uses pixel coordinates",
              target.getWidth() == getFramebufferWidth() && target.getHeight() == getFramebufferHeight() &&
              tc::getWidth() == target.getWidth() && tc::getHeight() == target.getHeight() && internal::targetPixelScale() == 1);
        auto node = std::make_shared<ClipFixture>();
        node->setPos(10, 10); node->setSize(20, 20); node->setClipping(true); node->handleDraw();
        check("native RectNode pop restores inactive empty clip", !internal::currentTarget().currentScissor.active &&
              internal::currentTarget().scissorStack.empty());
        setColor(1, 0, 0); drawRect(40, 10, 5, 5);
        target.end();
        const auto pixels = readTarget(target);
        int green = 0, red = 0, mismatched = 0;
        for (int y = 0; y < pixels.getHeight(); ++y) for (int x = 0; x < pixels.getWidth(); ++x) {
            const bool inClip = x >= 10 && x < 30 && y >= 10 && y < 30;
            const bool inMarker = x >= 40 && x < 45 && y >= 10 && y < 15;
            const auto* rgba = pixels.getData() + (size_t(y)*pixels.getWidth()+x)*4;
            green += rgba[0] == 0 && rgba[1] == 255 && rgba[2] == 0;
            red += rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0;
            mismatched += rgba[0] != (inMarker ? 255 : 0) || rgba[1] != (inClip ? 255 : 0) ||
                          rgba[2] != 0 || rgba[3] != 255;
        }
        std::printf("RECTNODE native RGBA mask green=%d red=%d mismatched=%d expected=400,25,0\n", green, red, mismatched);
        check("native RectNode exact full-attachment RGBA mask", green == 400 && red == 25 && mismatched == 0);
    }
    internal::pixelPerfectMode() = savedPixelMode;
}

class InspectFont : public tc::Font {
public:
    bool fits() const { return gridFitLands(); }
};

void runSyntheticDrawChecks() {
#if defined(SOKOL_GLCORE)
    // Deliberately differ from window size and sample count.
    Fbo fbo; fbo.allocate(160, 96, 1);
    unsigned char rgba[] = {128, 128, 0, 128};
    Texture source; source.allocate(1, 1, TextureFormat::RGBA8, TextureUsage::Dynamic); source.loadData(rgba, 1, 1, 4);
    tcx::lut::Lut3D lut = tcx::lut::createIdentity(2);
    tcx::lut::LutShader lutShader; check("synthetic LUT shader loads", lutShader.load());
    lutShader.setLut(lut); lutShader.setTexture(source);
    Shader hap; check("synthetic Hap YCoCg shader loads", hap.load(ycocg_shader_desc));
    hap.setTexture(0, source.getView(), source.getSampler());
#if defined(__linux__) && !defined(__ANDROID__)
    unsigned char yBytes[] = {128, 128, 128, 128};
    unsigned char uvBytes[] = {128, 128};
    Texture yPlane, uvPlane;
    yPlane.allocate(2, 2, TextureFormat::R8, TextureUsage::Dynamic); yPlane.loadData(yBytes, 2, 2, 1);
    uvPlane.allocate(1, 1, TextureFormat::RG8, TextureUsage::Dynamic); uvPlane.loadData(uvBytes, 1, 1, 2);
    internal::NV12VideoShader nv12; nv12.loadShader();
#endif
    for (int path = 0; path < 3; ++path) {
        fbo.begin(0, 0, 0); setupScreenOrtho(); disableDepthTest();
        setBlendMode(BlendMode::Alpha); translate(20, 10);
        auto& target = internal::currentTarget();
        pushScissor(0, 0, 145, 90); target.viewport = {4, 6, 120, 72, true};
        EasyCam camera; camera.begin();
        const auto context = target.camera.currentCameraContext;
        const auto view = getViewMatrix(), projection = getProjectionMatrix(), model = getMatrix();
        if (path == 0) lutShader.draw(70, 40, 8, 8);
#if defined(__linux__) && !defined(__ANDROID__)
        if (path == 1) nv12.draw(70, 40, 8, 8, yPlane, uvPlane);
#endif
        if (path == 2) {
            const auto data = tcx::hap::detail::makeYCoCgDrawData(0, 0, 8, 8, projection, view, model, Color(1, 1, 1));
            const auto expected = (projection * view * model).transposed();
            check("Hap draw-data packs target P*V*M once", std::memcmp(data.uniforms.mvp, expected.m, sizeof(expected.m)) == 0 && data.vertices[0].x == 0);
            hap.setUniform(0, &data.uniforms, sizeof(data.uniforms));
            hap.submitVertices(data.vertices, 4, PrimitiveType::Quads);
        }
        check("LUT/NV12/Hap retain target scoped state and active3D role", target.selectedPipeline == internal::ImmediateSelection{internal::PipelineRole::Geometry3D, BlendMode::Alpha, true} && !target.depthTestEnabled &&
              target.camera.currentCameraContext == context && sameMatrix(getViewMatrix(), view) && sameMatrix(getProjectionMatrix(), projection) &&
              sameMatrix(getMatrix(), model) && target.viewport.x == 4 && target.viewport.y == 6 && target.viewport.w == 120 && target.currentScissor.w == 145);
        camera.end(); setColor(0, 1, 0); drawRect(0, 0, 5, 5); popScissor(); fbo.end();
        auto pixels = readTarget(fbo);
        expectPixel(pixels, 20, 15, Color(0, 1, 0), "marker after synthetic direct draw retains model/camera");
        if (path < 2) {
            const auto center = projection * view * model * Vec4(74, 44, 0, 1);
            const int px = 4 + int((center.x / center.w + 1) * 60);
            const int py = 6 + int((1 - center.y / center.w) * 36);
            expectPixel(pixels, px, py, path == 0 ? Color(0.25f, 0.25f, 0) : Color(0.51f, 0.51f, 0.51f),
                        "LUT/NV12 rectangle follows real camera/model/viewport");
        }
        if (path == 2) {
            const auto center = projection * view * model * Vec4(4, 4, 0, 1);
            const int px = 4 + int((center.x / center.w + 1) * 60);
            const int py = 6 + int((1 - center.y / center.w) * 36);
            expectPixel(pixels, px, py, Color(0.502f, 0.504f, 0.498f), "synthetic Hap uses one target MVP");
        }
    }
#endif
}

void runMatrixChecks() {
    runNativeClipChecks();
    auto& window = internal::currentWindowContext();
    auto& screen = internal::currentTarget();
    const auto savedCamera = screen.camera;
    const auto savedModel = getMatrix();
    const auto screenDepth = screen.matrixStack.size();
    const auto savedRole = screen.selectedPipeline;
    const auto savedViewport = screen.viewport;
    Fbo first, second;
    first.allocate(160, 96); second.allocate(160, 96);
    setupScreenFovWithSize(0, 96, 64);
    setCameraPosition(1, 2, 3);
    pushMatrix(); translate(8, 9);
    pushScissor(2, 3, 80, 50);
    screen.viewport = {1, 2, 90, 60, true};
    const auto pointer = screen.camera.currentCameraContext;
    const auto model = getMatrix();
    setBlendMode(BlendMode::Alpha);
    const auto selection = screen.selectedPipeline;
    internal::RenderTargetResources* shared = nullptr;
    for (Fbo* fbo : {&first, &second, &first}) {
        fbo->begin(0, 0, 0);
        auto& target = internal::currentTarget();
        if (!shared) shared = &target.resources();
        check("same-format targets borrow one GPU context", shared == &target.resources());
        check("fresh activation has identity model, empty scopes and full viewport",
              sameMatrix(getMatrix(), Mat4::identity()) && target.matrixStack.empty() &&
              target.cameraStack.empty() && target.scissorStack.empty() && !target.currentScissor.active &&
              target.viewport.w == 160 && target.viewport.h == 96 && getCameraPosition().x == 0);
        check("drawing getters and target camera extent", tc::getWidth() == 160 && tc::getHeight() == 96 &&
              tc::getSize().x == 160 && target.camera.currentViewW == 160 && target.camera.currentViewH == 96 &&
              sameMatrix(getViewMatrix(), target.camera.currentViewMatrix) && sameMatrix(getProjectionMatrix(), target.camera.currentProjectionMatrix));
        const auto context = target.camera.currentCameraContext;
        const auto pipeline = target.selectedPipeline;
        EasyCam camera;
        camera.setDistance(180);
        for (bool ortho : {false, true}) {
            camera.setOrtho(ortho); camera.begin();
            check("EasyCam uses 5/3 Fbo aspect and preserves pickability",
                  approxEq(std::abs(getProjectionMatrix().m[5] / getProjectionMatrix().m[0]), 5.0f / 3) &&
                  target.camera.currentViewW == 160 && target.camera.currentViewH == 96 && target.camera.currentCameraContext->pickable);
            const auto center = worldToScreen(Vec3(0, 0, 0));
            const auto roundtrip = screenToWorld(Vec2(center.x, center.y), 0);
            check("camera center and world/screen round trip", approxEq(center.x, 80) && approxEq(center.y, 48) &&
                  std::abs(roundtrip.x) < 0.01f && std::abs(roundtrip.y) < 0.01f && std::abs(roundtrip.z) < 0.1f);
            if (!ortho) {
                const auto edge = worldToScreen(Vec3(30, 0, 0));
                check("EasyCam projected edge matches target focal length", std::abs(edge.x - 99.3137f) < 0.01f);
            }
            camera.begin(); setBlendMode(BlendMode::Add); enableDepthTest(); camera.end();
            check("nested camera restores active3D independently of logical flags", target.selectedPipeline == internal::ImmediateSelection{internal::PipelineRole::Geometry3D, BlendMode::Alpha, true} && target.depthTestEnabled && target.currentBlendMode == BlendMode::Add);
            camera.end();
            check("camera restores immutable entry snapshot and actual selected variant", target.camera.currentCameraContext == context && target.selectedPipeline == pipeline &&
                  sameMatrix(getMatrix(), Mat4::identity()));
        }
        // A rejected nested begin/end leaves the active target untouched.
        Fbo rejected; rejected.allocate(37, 23);
        const auto state = target.camera.currentCameraContext;
        rejected.begin(); rejected.end();
        check("nested Fbo rejection has no state mutation", &internal::currentTarget() == &target && state == target.camera.currentCameraContext);
        setupScreenOrtho(); disableDepthTest(); setBlendMode(BlendMode::Alpha);
        translate(6, 7); pushMatrix(); scale(2); popMatrix();
        setMatrix(Mat4::translate(10, 12, 0)); multMatrix(Mat4::scale(2, 2, 1));
        setColor(1, 0, 0); drawRect(0, 0, 5, 5);
        resetMatrix(); setColor(0, 1, 0); drawRect(30, 20, 5, 5);
        fbo->end();
        auto operations = readTarget(*fbo);
        expectPixel(operations, 12, 14, Color(1, 0, 0), "setMatrix/multMatrix retain non-identity view in sgl");
        expectPixel(operations, 32, 22, Color(0, 1, 0), "resetMatrix retains camera view");
        fbo->begin(0, 0, 0); setupScreenOrtho();
        // clear restarts the pass, retaining the camera/model/viewport/clip.
        translate(40, 30); pushMatrix();
        const auto view = getViewMatrix(); const auto proj = getProjectionMatrix();
        clear(0, 0, 0);
        check("clear retains matrices and CPU stack", sameMatrix(getViewMatrix(), view) && sameMatrix(getProjectionMatrix(), proj) && target.matrixStack.size() == 1);
        setColor(1, 0, 0); drawRect(0, 0, 8, 8); popMatrix(); resetMatrix();
        fbo->end();
        check("return restores screen pointer, model, stacks, viewport and pipeline",
              screen.camera.currentCameraContext == pointer && sameMatrix(getMatrix(), model) && screen.matrixStack.size() == screenDepth + 1 &&
              screen.scissorStack.size() == 1 && screen.viewport.x == 1 && screen.viewport.w == 90 && screen.selectedPipeline == selection &&
              getCameraPosition().x == 1);
        auto pixels = readTarget(*fbo);
        expectPixel(pixels, 43, 33, Color(1, 0, 0), "clear/restart reapplies V*M");
        expectPixel(pixels, 3, 3, Color(0, 0, 0), "Fbo does not inherit screen transform or clip");
    }
    popScissor(); popMatrix(); screen.viewport = savedViewport;

    // Force host DPI through the same context fields used by secondary windows.
    // GPU resources remain this window's actual context; Fbo pixels are DPI 1.
    const bool mainWindow = window.isMain;
    const auto hostDpi = window.dpiScale;
    const int hostW = window.fbWidth, hostH = window.fbHeight;
    const bool pixelMode = internal::pixelPerfectMode();
    window.isMain = false; window.fbWidth = 192; window.fbHeight = 128;
    for (float dpi : {1.0f, 2.0f}) for (bool pixelPerfect : {false, true}) {
        window.dpiScale = dpi; internal::pixelPerfectMode() = pixelPerfect;
        std::printf("RECTNODE simulated hostDpi=%g framebuffer=%dx%d fbo=%dx%d pixelPerfect=%d\n",
                    getDpiScale(), getFramebufferWidth(), getFramebufferHeight(), first.getWidth(), first.getHeight(), int(pixelPerfect));
        InspectFont font;
        font.setOversampling(1);
        check("fixed synthetic TTF loads at explicit host raster DPI", font.load(fs::path(__FILE__).parent_path() / "fixture.ttf", 20).ok());
        first.begin(0, 0, 0); setupScreenOrtho(); setBlendMode(BlendMode::Alpha); disableDepthTest();
        check("Fbo coordinate scale ignores host DPI and pixelPerfect", internal::targetPixelScale() == 1 && tc::getWidth() == 160 && tc::getHeight() == 96 &&
              getFramebufferWidth() == 192 && getWindowWidth() == int(192 / (pixelPerfect ? 1 : dpi)));
        auto node = std::make_shared<ClipFixture>();
        node->setPos(10, 10); node->setSize(20, 20); node->setClipping(true); node->handleDraw();
        check("clipped node geometry stays object-local", node->getWidth() == 20 && node->getHeight() == 20 && node->getSize().x == 20);
        setColor(1, 0, 0); drawRect(40, 10, 5, 5);
        const auto context = internal::currentTarget().camera.currentCameraContext;
        const auto selected = internal::currentTarget().selectedPipeline;
        setColor(1, 1, 1);
        drawBitmapString("A", 60, 10); drawBitmapString("A", 60, 30, true);
        drawBitmapString("A", Vec3(90, 10, 0));
        check("font metrics retain requested coordinate size across raster DPI", std::abs(font.getWidth("A") - 12) < 1 && font.fits());
        font.drawString("A", 60, 55);
        font.enableWrap(true); font.setMaxLineLength(25);
        check("loaded-font wrapping metrics use normalized advances", font.getWidth("AAAA") <= 25 && approxEq(font.getHeight("AAAA"), 2 * font.getLineHeight()));
        font.enableWrap(false);
        font.setWritingMode(WritingMode::VerticalRL); font.drawString("A", 100, 55);
        pushMatrix(); translate(125, 75); scale(0.5f);
        check("font grid fitting stands down off one target pixel per model unit", !font.fits());
        font.drawString("A", 0, 0); popMatrix();
        internal::FontCacheKey fontKey;
        fontKey.fontPath = internal::pathToUtf8(fs::path(__FILE__).parent_path() / "fixture.ttf");
        fontKey.fontSize = int(20 * dpi); fontKey.oversample = 1;
        const auto manager = internal::SharedFontCache::getInstance().get(fontKey);
        check("font sampler requests mipmaps from raster DPI versus target density", manager && manager->hasMipmaps() == (dpi == 2));
        check("text restores camera, pipeline and matrix scopes", internal::currentTarget().camera.currentCameraContext == context && internal::currentTarget().selectedPipeline == selected &&
              sameMatrix(getMatrix(), Mat4::identity()) && internal::currentTarget().matrixStack.empty() && !sgl_error().stack_underflow && !sgl_error().stack_overflow);
        first.end();
        auto pixels = readTarget(first);
        expectPixel(pixels, 15, 15, Color(0, 1, 0), "RectNode clip interior one");
        expectPixel(pixels, 25, 25, Color(0, 1, 0), "RectNode clip interior two");
        expectPixel(pixels, 9, 15, Color(0, 0, 0), "RectNode left clip");
        expectPixel(pixels, 30, 15, Color(0, 0, 0), "RectNode right clip");
        expectPixel(pixels, 42, 12, Color(1, 0, 0), "RectNode pop restores inactive clip");
        int fontPixels = 0;
        for (int y = 55; y < 80; ++y) for (int x = 60; x < 75; ++x) if (pixels.getColor(x, y).r > 0.5f) ++fontPixels;
        check("loaded font emits bounded visible glyphs into Fbo", fontPixels > 20 && fontPixels < 200);
    }
    // Attachment pixels are independent of host DPI and pixelPerfect.
    window.fbWidth = 512; window.fbHeight = 384; window.dpiScale = 2;
    Fbo pointTarget; pointTarget.allocate(512, 384);
    Mesh pointMesh; pointMesh.setMode(PrimitiveMode::Points); pointMesh.addVertex(40, 40, 0);
    for (bool pixelPerfect : {false, true}) {
        internal::pixelPerfectMode() = pixelPerfect;
        pointTarget.begin(0, 0, 0); setupScreenOrtho();
        setPointStyle(PointStyle::Square); setPointSize(12); setColor(0, 1, 0); translate(20, 10); pointMesh.draw();
        pointTarget.end(); auto pixels = readTarget(pointTarget);
        int firstX = 512, lastX = -1, firstY = 384, lastY = -1, count = 0;
        for (int y = 0; y < 384; ++y) for (int x = 0; x < 512; ++x) if (pixels.getColor(x, y).g > 0.9f) {
            firstX = std::min(firstX, x); lastX = std::max(lastX, x);
            firstY = std::min(firstY, y); lastY = std::max(lastY, y); ++count;
        }
        const int extent = 12;
        check("point target extent and model transform at host DPI 2", lastX - firstX + 1 == extent && lastY - firstY + 1 == extent &&
              count == extent * extent && firstX + lastX == 119 && firstY + lastY == 99);
    }
    window.isMain = mainWindow; window.dpiScale = hostDpi; window.fbWidth = hostW; window.fbHeight = hostH;
    internal::pixelPerfectMode() = pixelMode;
    // Direct fullscreen drawing retains a custom target camera, model and clip.
    first.begin(0, 0, 0); setupScreenFovWithSize(0, 160, 96, 0.25f, 600);
    translate(20, 10); pushScissor(0, 0, 140, 90);
    auto& directTarget = internal::currentTarget();
    directTarget.viewport = {4, 6, 120, 72, true};
    EasyCam directCamera; directCamera.begin();
    const auto directContext = directTarget.camera.currentCameraContext;
    const auto directView = getViewMatrix(), directProjection = getProjectionMatrix(), directModel = getMatrix();
    FullscreenShader directShader; check("direct fixture shader loads", directShader.load(targetstate_discard_shader_desc)); directShader.draw();
    check("direct shader restores full active3D target state", directTarget.selectedPipeline == internal::ImmediateSelection{internal::PipelineRole::Geometry3D, BlendMode::Alpha, true} && !directTarget.depthTestEnabled &&
          directTarget.camera.currentCameraContext == directContext && sameMatrix(getViewMatrix(), directView) &&
          sameMatrix(getProjectionMatrix(), directProjection) && sameMatrix(getMatrix(), directModel) && directTarget.currentScissor.w == 140 && directTarget.viewport.x == 4 && directTarget.viewport.w == 120);
    directCamera.end(); setColor(0, 1, 0); drawRect(0, 0, 5, 5); popScissor(); first.end();
    auto directPixels = readTarget(first);
    expectPixel(directPixels, 20, 15, Color(0, 1, 0), "post-shader marker retains V*M");

    // PBR submissions snapshot target camera/model and window lights by value.
    Mesh mesh; mesh.setMode(PrimitiveMode::Triangles);
    mesh.addVertex(60, 30, 0).addVertex(100, 30, 0).addVertex(80, 70, 0);
    for (int i = 0; i < 3; ++i) mesh.addNormal(0, 0, 1);
    Material material; material.setBaseColor(0.7f, 0.7f, 0.7f).setMetallic(1).setRoughness(0.3f);
    Light light; light.setDirectional(0, 0, -1); light.setIntensity(1); addLight(light); setMaterial(material);
    float specular[2] = {};
    for (int i = 0; i < 2; ++i) {
        Fbo& fbo = i ? second : first;
        fbo.begin(0, 0, 0); setupScreenOrtho();
        setCameraPosition(i ? Vec3(300, 48, 10) : Vec3(80, 48, 100));
        const auto eye = getCameraPosition();
        const auto vp = (getProjectionMatrix() * getViewMatrix()).transposed();
        mesh.draw();
        const auto snapshot = internal::currentTarget().deferredPbrDraws.back().cmd;
        setCameraPosition(7, 8, 9); light.setIntensity(3);
        check("PBR uniforms retain submitted target eye, model, view and world light",
              snapshot.fsp.cameraPos[0] == eye.x && snapshot.fsp.cameraPos[2] == eye.z &&
              snapshot.vsp.model[0] == 1 && snapshot.vsp.model[12] == 0 &&
              std::memcmp(snapshot.vsp.viewProj, vp.m, sizeof(vp.m)) == 0 &&
              snapshot.fsp.lightPosType[0][2] == -1 && snapshot.fsp.lightColorIntensity[0][3] == 1 &&
              internal::currentTarget().deferredPbrDraws.back().cmd.fsp.cameraPos[0] == eye.x);
        fbo.end(); auto pixels = readTarget(fbo); specular[i] = pixels.getColor(80, 48).r;
        light.setIntensity(1);
    }
    std::printf("PBR specular center front=%f side=%f\n", double(specular[0]), double(specular[1]));
    check("non-emissive specular probe responds to target eye position", specular[0] > 0.8f && specular[1] < 0.3f);
    clearMaterial(); clearLights();

    runSyntheticDrawChecks();

    // Unmatched pushes are discarded and LOAD begins a fresh local scope.
    first.begin(); pushMatrix(); pushScissor(1, 1, 2, 2);
    EasyCam unmatched; unmatched.begin(); first.end();
    first.begin();
    check("LOAD starts fresh after unmatched scopes", internal::currentTarget().matrixStack.empty() && internal::currentTarget().cameraStack.empty() &&
          internal::currentTarget().scissorStack.empty() && sameMatrix(getMatrix(), Mat4::identity()) && internal::currentTarget().camera.currentScreenFov == TAU / 8);
    first.end();
    screen.camera = savedCamera; screen.currentMatrix = savedModel; screen.selectedPipeline = savedRole;
    internal::applyTargetDrawingState();
}

int depthCaptures = 0;
bool depthPending = false;
EventListener depthAfterFrame;
void captureDepthFixture() {
    if (!depthPending) return;
    Pixels pixels;
    check("overlapping-depth screen capture", grabScreen(pixels));
    if (pixels.isAllocated()) expectPixel(pixels, getFramebufferWidth() / 2, getFramebufferHeight() / 2, Color(1, 0, 0), "near sgl triangle survives far triangle after scope restoration");
    depthPending = false; ++depthCaptures;
}
void drawDepthFixture(int variant, Fbo& fbo) {
    setupScreenOrtho(); setBlendMode(BlendMode::Alpha); disableDepthTest(); resetMatrix(); clear(0, 0, 0);
    auto& target = internal::currentTarget();
    const auto selected = target.selectedPipeline;
    const auto context = target.camera.currentCameraContext;
    EasyCam camera; camera.setDistance(100); camera.begin();
    internal::ensureCurrentPass();
    setColor(1, 0, 0); drawTriangle(Vec3(-20, -20, 10), Vec3(20, -20, 10), Vec3(0, 20, 10));
    internal::currentTarget().flushLayers();
    if (variant == 0) { fbo.begin(0, 0, 0); fbo.end(); }
    if (variant == 1) {
        FullscreenShader shader; check("temporary shader loads", shader.load(targetstate_discard_shader_desc)); shader.draw();
    }
    if (variant == 2) { camera.begin(); setBlendMode(BlendMode::Add); camera.end(); }
    check("scope restores active3D with depth flag false", target.selectedPipeline == internal::ImmediateSelection{internal::PipelineRole::Geometry3D, BlendMode::Alpha, true} && !target.depthTestEnabled);
    setColor(0, 1, 0); drawTriangle(Vec3(-20, -20, -10), Vec3(20, -20, -10), Vec3(0, 20, -10));
    camera.end();
    check("EasyCam restores entry 2D pipeline and immutable context", target.selectedPipeline == selected && target.camera.currentCameraContext == context);
    depthPending = true;
}
