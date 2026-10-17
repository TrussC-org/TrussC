// #821 PR 3: target stack, consuming flushes and backend MSAA preservation.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "stack.glsl.h"
#include <cstdio>
#include <cstring>

using namespace tc;
namespace {
int failures = 0;
bool completed = false;
int screenCaptures = 0;
void check(const char* name, bool value) {
    std::printf("%s: %s\n", name, value ? "PASS" : "FAIL");
    if (!value) ++failures;
}
Pixels read(Fbo& fbo) {
    Pixels result;
    result.allocate(fbo.getWidth(), fbo.getHeight(), 4);
    check("resolved Fbo readback", fbo.readPixels(result.getData()));
    return result;
}
void expect(const Pixels& image, int x, int y, Color wanted, const char* label) {
    const auto actual = image.getColor(x, y);
    const bool value = std::abs(actual.r - wanted.r) <= 2.0f/255 &&
        std::abs(actual.g - wanted.g) <= 2.0f/255 && std::abs(actual.b - wanted.b) <= 2.0f/255;
    std::printf("PIXEL %s (%d,%d) rgb=(%.0f,%.0f,%.0f)\n", label, x, y,
                actual.r*255, actual.g*255, actual.b*255);
    check(label, value);
}
void rectangle(float x, float y, float w, float h, Color color) {
    setColor(color); drawRect(x, y, w, h);
}
size_t leaseCount() {
    size_t count = 0;
    for (const auto& bucket : internal::fboSharedMap()) count += bucket.second.leases.size();
    return count;
}
void patch(Shader& shader, bool right) {
    const float x0 = right ? 0.1f : -0.9f;
    const float x1 = right ? 0.9f : -0.1f;
    const Color color = right ? Color(0,1,0,0.5f) : Color(1,0,0,0.5f);
    const ShaderVertex vertices[] = {
        {x0,-0.5f,0,0,0,color.r,color.g,color.b,color.a},
        {x1,-0.5f,0,0,0,color.r,color.g,color.b,color.a},
        {x0, 0.5f,0,0,0,color.r,color.g,color.b,color.a},
        {x1, 0.5f,0,0,0,color.r,color.g,color.b,color.a}};
    shader.submitVertices(vertices, 4, PrimitiveType::TriangleStrip);
}
void stateAndPixels(int samples, bool differentFormat) {
    std::printf("CASE stateAndPixels frame=0 samples=%d childFormat=%s\n",
                samples, differentFormat ? "RGBA16F" : "RGBA8");
    Fbo a, b, c;
    a.allocate(64,48,samples);
    b.allocate(37,29,samples,differentFormat ? TextureFormat::RGBA16F : TextureFormat::RGBA8,true);
    c.allocate(19,17,samples);
    auto& window = internal::currentWindowContext();
    const auto* bottom = &internal::currentTarget();
    const bool started = bottom->passStartedThisFrame;
    const bool open = bottom->inPass;
    a.begin(0,0,0); setupScreenOrtho(); setBlendMode(BlendMode::Alpha);
    auto* parent = &internal::currentTarget();
    const auto token = parent->activation;
    const auto context = parent->resources().context;
    rectangle(0,0,16,16,Color(1,0,0));
    pushMatrix(); translate(4,0);
    pushScissor(0,0,64,40);
    const auto camera = parent->camera.currentCameraContext;
    const auto pipeline = parent->selectedPipeline;
    const auto model = parent->currentMatrix;
    b.begin(0,0,0);
    auto* middle = &internal::currentTarget();
    const auto middleToken = middle->activation;
    check("A/B have distinct leased contexts, tokens and independent drawing state",
          !parent->inPass && middle->inPass && middle->resources().context.id != context.id &&
          middleToken != token && middle->matrixStack.empty() && middle->cameraStack.empty() &&
          !middle->currentScissor.active && middle->width == 37 && middle->height == 29);
    setupScreenOrtho(); rectangle(0,0,16,16,Color(0,0,1));
    c.begin(1,1,0);
    check("A/B/C stack and lease identities survive vector growth", window.targetStack.size() == 4 &&
          !parent->inPass && !middle->inPass && internal::currentTarget().activation != middleToken &&
          internal::currentTarget().resources().context.id != middle->resources().context.id);
    c.end();
    check("C pop resumes B exactly", &internal::currentTarget() == middle && middle->activation == middleToken && middle->inPass);
    Light light; light.setDirectional(0,-1,-1); light.enableShadow(32); addLight(light);
    Mesh triangle; triangle.setMode(PrimitiveMode::Triangles);
    triangle.addVertex(-2,-2,0).addVertex(2,-2,0).addVertex(0,2,0);
    pushMatrix(); translate(2,3,4);
    const auto callerModel = getMatrix();
    beginShadowPass(light);
    check("shadow seeds its model from the calling nested Fbo", internal::currentWindowContext().shadow.inPass &&
          std::memcmp(callerModel.m,getMatrix().m,sizeof(callerModel.m)) == 0 && internal::currentTarget().matrixStack.empty());
    pushMatrix(); translate(1,0,0); shadowDraw(triangle); popMatrix();
    c.begin(1,1,0); c.end();
    shadowDraw(triangle); // resumed fixed target must rebind its explicit pipeline
    endShadowPass();
    check("shadow pop restores calling Fbo and its transform scope", &internal::currentTarget() == middle && middle->inPass &&
          middle->matrixStack.size() == 1 && std::memcmp(callerModel.m,getMatrix().m,sizeof(callerModel.m)) == 0);
    popMatrix(); removeLight(light);
    rectangle(20,0,8,8,Color(0,1,0));
    b.end();
    check("B pop restores A matrices, camera, selected pipeline and clipping", &internal::currentTarget() == parent &&
          parent->activation == token && parent->inPass && parent->resources().context.id == context.id &&
          parent->camera.currentCameraContext == camera && parent->selectedPipeline == pipeline &&
          std::memcmp(model.m, parent->currentMatrix.m, sizeof(model.m)) == 0 &&
          parent->matrixStack.size() == 1 && parent->scissorStack.size() == 1 && parent->currentScissor.active);
    rectangle(28,0,16,16,Color(0,1,0));
    popScissor(); popMatrix();
    // B's mip scopes completed before A resumes, so B can be sampled immediately.
    setColor(1,1,1); b.draw(0,24,37,20);
    a.end();
    check("A pop restores the original bottom open/lazy state", &internal::currentTarget() == bottom &&
          bottom->inPass == open && bottom->passStartedThisFrame == started && window.targetStack.size() == 1);
    const auto image = read(a);
    expect(image,8,8,Color(1,0,0),"MSAA interrupt: first draw survives nested Fbos and resolve");
    expect(image,40,8,Color(0,1,0),"MSAA interrupt: resumed draw survives resolve");
    expect(image,6,30,Color(0,0,1),"nested mipped child is immediately sampleable");
    const auto before = leaseCount();
    for (int i=0;i<4;++i) { a.begin(); a.end(); }
    check("sequential activations reuse idle leases", leaseCount() == before);
    a.begin(); a.end();
    expect(read(a),8,8,Color(1,0,0),"fresh LOAD activation retains persistent color");
    a.begin(); clear(0,0,1); a.end();
    expect(read(a),8,8,Color(0,0,1),"explicit restart clear erases previous color");
}
void inheritedBlendPixels(int samples) {
    Fbo fbo; fbo.allocate(32,32,samples);
    setBlendMode(BlendMode::Add); disableDepthTest();
    fbo.begin(0,0,0);
    check("fresh Fbo selects inherited Add role with depth disabled",
          internal::currentTarget().selectedPipeline == unsigned(BlendMode::Add));
    // Keep the default Fbo camera: its setup must restore the base 2D pipeline.
    rectangle(0,0,32,32,Color(1,0,0,0.5f));
    rectangle(0,0,32,32,Color(0,1,0,0.5f));
    fbo.end();
    expect(read(fbo),16,16,Color(0.5f,0.5f,0),"inherited Add retains both half-alpha contributions");
    setBlendMode(BlendMode::Alpha);
}
void depthPixels(int samples) {
    Fbo a, b;
    a.allocate(96,64,samples); b.allocate(8,8);
    a.begin(0,0,0);
    EasyCam camera; camera.setDistance(100); camera.begin();
    setColor(1,0,0); drawTriangle(Vec3(-20,-20,10),Vec3(20,-20,10),Vec3(0,20,10));
    b.begin(0,0,0); b.end();
    setColor(0,1,0); drawTriangle(Vec3(-20,-20,-10),Vec3(20,-20,-10),Vec3(0,20,-10));
    camera.end(); a.end();
    expect(read(a),48,32,Color(1,0,0),"parent depth LOAD occludes farther draw after interruption");
    a.begin(); camera.begin();
    setColor(0,1,0); drawTriangle(Vec3(-20,-20,-10),Vec3(20,-20,-10),Vec3(0,20,-10));
    camera.end(); a.end();
    expect(read(a),48,32,Color(0,1,0),"fresh LOAD activation clears old depth");
}
void shaderPixels(int samples, bool differentFormat) {
    std::printf("CASE shaderPixels frame=0 samples=%d childFormat=%s\n",
                samples, differentFormat ? "RGBA16F" : "RGBA8");
    Fbo a,b,c,reference;
    a.allocate(64,48,samples);
    b.allocate(37,29,samples,differentFormat ? TextureFormat::RGBA16F : TextureFormat::RGBA8);
    c.allocate(19,17,samples);
    reference.allocate(64,48,samples);
    Shader first, second;
    check("spanning shaders load", first.load(stack_mesh_shader_desc) && second.load(stack_mesh_shader_desc));
    reference.begin(0,0,0); first.begin(); patch(first,false); patch(first,true); first.end(); reference.end();
    a.begin(0,0,0); first.begin(); patch(first,false);
    const int reserved = internal::currentTarget().sglLayerNext;
    for (int i=0;i<2;++i) {
        b.begin(0,0,0); second.begin(); patch(second,false);
        const int middleReserved = internal::currentTarget().sglLayerNext;
        c.begin(0,0,0);
        rectangle(0,0,19,17,Color(0,0,1)); // parked B's Shader must not capture C
        c.end();
        check("child Shader scope retains its layer accounting across C", internal::currentTarget().sglLayerNext == middleReserved);
        patch(second,true); second.end(); b.end();
        check("parked parent Shader reservation survives repeated children", internal::currentTarget().sglLayerNext == reserved &&
              internal::currentTarget().openShaderScopes == 1);
    }
    second.begin();
    b.begin(0,0,0); b.end();
    check("two open Shader scopes retain parent protection", internal::currentTarget().openShaderScopes == 2);
    second.end(); patch(first,true); first.end();
    internal::currentTarget().flushLayers();
    check("closed Shader scopes reset only after consuming remaining draws", internal::currentTarget().openShaderScopes == 0 &&
          internal::currentTarget().sglLayerNext == 0 && internal::currentTarget().deferredShaderDraws.empty());
    a.end();
    expect(read(c),9,8,Color(0,0,1),"child 2D drawing ignores the parked parent Shader");
    auto actual = read(a), flat = read(reference);
    check("spanning Shader pixels match flattened rendering in all RGBA channels",
          actual.getTotalBytes() == flat.getTotalBytes() &&
          std::memcmp(actual.getData(),flat.getData(),actual.getTotalBytes()) == 0);
    expect(actual,16,24,Color(128.f/255,0,0),"first Shader draw contributes exactly once");
    expect(actual,48,24,Color(0,128.f/255,0),"post-resume Shader draw contributes exactly once");
}
void mixedLayersPixels(int samples) {
    Fbo actual, reference, child, grandchild;
    actual.allocate(96,64,samples); reference.allocate(96,64,samples);
    child.allocate(23,17,samples,TextureFormat::RGBA16F); grandchild.allocate(11,9);
    Shader shader; FullscreenShader fullscreen;
    check("mixed replay shaders load", shader.load(stack_mesh_shader_desc) && fullscreen.load(stack_discard_shader_desc));
    Mesh mesh, points;
    mesh.setMode(PrimitiveMode::Triangles);
    mesh.addVertex(-8,-8,0).addVertex(8,-8,0).addVertex(0,8,0);
    for (int i=0;i<3;++i) mesh.addNormal(0,0,1);
    points.setMode(PrimitiveMode::Points); points.addVertex(64,32,0).addColor(Color(0,0,1));
    Material material; material.setBaseColor(0,0,0).setEmissive(0,0,1).setEmissiveStrength(1);
    auto geometry = [&] {
        setColor(1,1,1);
        setMaterial(material); pushMatrix(); translate(48,32); mesh.draw(); popMatrix(); clearMaterial();
        setPointStyle(PointStyle::Square); setPointSize(8); points.draw();
    };
    auto draw = [&](Fbo& output, bool nested) {
        output.begin(0,0,0); setupScreenOrtho(); setBlendMode(BlendMode::Alpha); disableDepthTest();
        rectangle(0,0,24,24,Color(1,0,0,0.5f));
        shader.begin(); patch(shader,false); shader.end(); geometry();
        if (nested) {
            child.begin(0,0,0); grandchild.begin(0,0,0); grandchild.end(); child.end();
        }
        rectangle(0,0,24,24,Color(0,1,0,0.5f));
        geometry(); fullscreen.draw();
        if (nested) { child.begin(0,0,0); child.end(); }
        shader.begin(); patch(shader,true); shader.end();
        output.end();
    };
    draw(reference,false); draw(actual,true);
    const auto flat = read(reference), nested = read(actual);
    int maximumDelta = 0;
    if (nested.getTotalBytes() == flat.getTotalBytes()) {
        for (size_t i=0;i<flat.getTotalBytes();++i)
            maximumDelta = std::max(maximumDelta,std::abs(int(nested.getData()[i])-int(flat.getData()[i])));
    }
    // Use the existing 3D/MSAA allowance for the mixed PBR/point fixture;
    // a tile store may quantize color before the following alpha blend.
    std::printf("MIXED samples=%d maximum RGBA delta=%d tolerance=2\n",samples,maximumDelta);
    check("interleaved 2D/Shader/PBR/points/fullscreen nesting matches flattened RGBA", nested.getTotalBytes() == flat.getTotalBytes() && maximumDelta <= 2);
    expect(nested,8,8,Color(64.f/255,128.f/255,0),"mixed consuming flushes contribute each half-alpha quad once");
    const auto pbr = nested.getColor(48,32);
    check("mixed replay contains visible emissive PBR geometry", pbr.b > 0.5f && pbr.r < 0.1f && pbr.g < 0.1f);
    expect(nested,64,32,Color(0,128.f/255,128.f/255),"mixed replay contains points below the final Shader layer");
}
void misuseAndScopes() {
    Fbo a,b,unallocated;
    a.allocate(32,24); b.allocate(16,12);
    a.begin(0,0,0);
    auto* parent = &internal::currentTarget();
    const auto token = parent->activation;
    a.begin();
    check("recursive begin leaves activation unchanged", parent->activation == token && internal::currentWindowContext().targetStack.size() == 2);
    b.begin(0,0,0); auto* child = &internal::currentTarget(); a.end();
    check("out-of-order end preserves child", &internal::currentTarget() == child && child->inPass && a.isActive());
    a.allocate(2,2); a.clear();
    Fbo moved(std::move(a));
    check("active reallocate, release and move are rejected", a.getWidth() == 32 && a.isActive() && !moved.isAllocated());
    internal::WindowContext foreign;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &foreign; b.end();
    internal::currentWindowCtx() = previous;
    check("cross-window end leaves the owner activation intact", &internal::currentTarget() == child && b.isActive());
    check("wrong token does not end a pass", !internal::popTarget(*child,child->activation+1) && child->inPass);
    b.end();
    check("inactive and unallocated pairs are harmless", !b.isActive());
    b.end(); unallocated.begin(); unallocated.end();
    check("unmatched end keeps the valid parent", &internal::currentTarget() == parent && parent->inPass);
    // Failed fixed resource preparation never parks a valid parent.
    internal::RenderTarget invalid;
    invalid.passKind = internal::TargetPassKind::Fixed; invalid.width = invalid.height = 1;
    { internal::TargetPassScope scope(invalid); check("failed scope is inactive", !scope.active()); }
    check("failed preparation preserves parent and token", &internal::currentTarget() == parent && parent->inPass && parent->activation == token);
    auto earlyReturn = [&] {
        sg_pass pass = {}; pass.attachments.colors[0] = b.getTexture().getAttachmentView();
        internal::TargetPassScope scope(pass,16,12,SG_PIXELFORMAT_RGBA8);
        check("fixed scope enters", scope.active());
        return;
    };
    earlyReturn();
    check("RAII early return resumes parent", &internal::currentTarget() == parent && parent->inPass);
    try {
        sg_pass pass = {}; pass.attachments.colors[0] = b.getTexture().getAttachmentView();
        internal::TargetPassScope scope(pass,16,12,SG_PIXELFORMAT_RGBA8);
        throw 1;
    } catch (int) {}
    check("RAII exception resumes parent", &internal::currentTarget() == parent && parent->inPass);
    internal::RenderTarget throwing;
    throwing.passKind = internal::TargetPassKind::Fixed;
    throwing.width = 16; throwing.height = 12; throwing.hasDepth = false;
    throwing.sampleCount = 1; throwing.colorFormat = SG_PIXELFORMAT_RGBA8; throwing.depthFormat = SG_PIXELFORMAT_NONE;
    throwing.binding.attachments.colors[0] = b.getTexture().getAttachmentView();
    throwing.finalize = [](void*) { throw 1; };
    { internal::TargetPassScope scope(throwing); }
    check("scope destructor contains finalization exceptions and restores parent", &internal::currentTarget() == parent && parent->inPass && !throwing.activation);
    struct TransferState { int starts=0; int ends=0; bool parked=true; } transferState;
    internal::RenderTarget transfer;
    transfer.passKind = internal::TargetPassKind::Transfer;
    transfer.width = 16; transfer.height = 12; transfer.hasDepth = false;
    transfer.colorFormat = SG_PIXELFORMAT_RGBA8; transfer.sampleCount = 1; transfer.depthFormat = SG_PIXELFORMAT_NONE;
    transfer.owner = &transferState;
    transfer.beginTransfer = [](void* owner) {
        auto& state = *static_cast<TransferState*>(owner); ++state.starts;
        const auto& window = internal::currentWindowContext();
        state.parked &= !window.currentTarget->inPass && !window.targetStack[window.targetStack.size()-2].target->inPass;
    };
    transfer.endTransfer = [](void* owner) {
        auto& state = *static_cast<TransferState*>(owner); ++state.ends;
        const auto& window = internal::currentWindowContext();
        state.parked &= !window.currentTarget->inPass && !window.targetStack[window.targetStack.size()-2].target->inPass;
    };
    { internal::TargetPassScope scope(transfer); check("native transfer has no open raster pass", scope.active() && !transfer.inPass); }
    check("transfer finishes once before parent resumes", transferState.starts == 1 && transferState.ends == 1 && transferState.parked && parent->inPass);
    transferState = TransferState{};
    transfer.endTransfer = [](void* owner) { ++static_cast<TransferState*>(owner)->ends; throw 43; };
    bool completionThrew = false;
    {
        internal::TargetPassScope scope(transfer);
        try { internal::popTarget(transfer,transfer.activation); }
        catch (int value) { completionThrew = value == 43; }
    }
    check("throwing transfer completion restores parent before rethrow and runs once", completionThrew &&
          transferState.starts == 1 && transferState.ends == 1 && !transfer.activation &&
          &internal::currentTarget() == parent && parent->inPass);
    transfer.endTransfer = [](void* owner) { ++static_cast<TransferState*>(owner)->ends; };
    transferState = TransferState{};
    transfer.beginTransfer = [](void* owner) { ++static_cast<TransferState*>(owner)->starts; throw 42; };
    bool constructionThrew = false;
    try { internal::TargetPassScope failed(transfer); }
    catch (int value) { constructionThrew = value == 42; }
    check("failed scope construction rolls back its activation and resumes parent", constructionThrew &&
          transferState.starts == 1 && transferState.ends == 1 && !transfer.activation && &internal::currentTarget() == parent && parent->inPass);
    unsigned char bytes[16*12*4] = {};
    std::puts("CASE intentional readback rejection inside public Fbo (not an IBL readback)");
    check("readback still rejects any public Fbo activation", !b.readPixels(bytes));
    a.end();
    auto& bottom = internal::currentTarget();
    check("bottom entry cannot be popped", !internal::popTarget(bottom,bottom.activation) && internal::currentWindowContext().targetStack.size() == 1);
}
void versionCopyPixels() {
    Fbo a,b; a.allocate(64,24); b.allocate(32,24);
    a.begin(0,0,0); setupScreenOrtho();
    b.begin(1,0,0); b.end(); setColor(1,1,1); b.draw(0,0,32,24);
    b.begin(); setupScreenOrtho(); rectangle(16,0,16,24,Color(0,1,0)); b.end();
    setColor(1,1,1); b.draw(32,0,32,24); a.end();
    auto image = read(a);
    expect(image,24,12,Color(1,0,0),"recorded snapshot retains the previous version");
    expect(image,40,12,Color(1,0,0),"nested LOAD preparation blits the previous version");
    expect(image,56,12,Color(0,1,0),"new version receives subsequent draws");
}
void environmentPixels() {
    std::puts("CASE environmentPixels frame=0 outerSamples=4 childSamples=1");
    Pixels hdr; hdr.allocate(16,8,4,PixelFormat::F32);
    auto* values = static_cast<float*>(hdr.getDataVoid());
    for (int y=0;y<8;++y) for (int x=0;x<16;++x) {
        const int index = 4*(y*16+x);
        values[index] = float(x+1)/32; values[index+1] = float(y+1)/16;
        values[index+2] = 0.125f; values[index+3] = 1;
    }
    Environment flat, nested;
    check("top-level IBL bake", flat.loadFromHDR(hdr));
    Fbo a,b; a.allocate(32,24,4); b.allocate(16,12);
    a.begin(0,0,0); setupScreenOrtho(); rectangle(0,0,16,16,Color(1,0,0));
    b.begin(0,0,0);
    auto* parent = &internal::currentTarget(); const auto token = parent->activation;
    check("IBL bake inside a nested public Fbo", nested.loadFromHDR(hdr));
    check("all face, mip and LUT scopes restore their parent", &internal::currentTarget() == parent && parent->activation == token && parent->inPass);
    b.end(); a.end();
    expect(read(a),8,8,Color(1,0,0),"IBL scopes preserve the outer MSAA draw");
    FullscreenShader cube, copy;
    check("IBL probe shaders load", cube.load(stack_cube_shader_desc) && copy.load(tc_fbomip_blit_shader_desc));
    copy.setParams(tc_fbomip_mip_params_t{});
    Fbo probe; probe.allocate(8,8);
    const Vec3 directions[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto sample = [&](const Texture& texture, Vec3 direction, int level) {
        stack_cube_params_t params = {};
        params.directionLod[0] = direction.x; params.directionLod[1] = direction.y;
        params.directionLod[2] = direction.z; params.directionLod[3] = float(level);
        cube.setParams(params); cube.setTexture(0,texture.getView(),texture.getSampler());
        probe.begin(0,0,0); cube.draw(); probe.end();
        auto image = read(probe);
        check("IBL cube output retains the source's nonzero blue", image.getColor(4,4).b > 0.05f && image.getColor(4,4).b < 0.95f);
        return image;
    };
    auto equal = [&](const Pixels& first, const Pixels& second) {
        return first.getTotalBytes() == second.getTotalBytes() &&
            std::memcmp(first.getData(),second.getData(),first.getTotalBytes()) == 0;
    };
    for (Vec3 direction : directions) {
        check("nested irradiance face matches top-level pixels", equal(sample(flat.getIrradianceMap(),direction,0),sample(nested.getIrradianceMap(),direction,0)));
        for (int level=0;level<flat.getPrefilterMipLevels();++level)
            check("nested prefilter face/mip matches top-level pixels", equal(sample(flat.getPrefilterMap(),direction,level),sample(nested.getPrefilterMap(),direction,level)));
    }
    auto lut = [&](const Texture& texture) {
        copy.setTexture(0,texture.getView(),texture.getSampler()); probe.begin(0,0,0); copy.draw(); probe.end();
        auto image = read(probe);
        check("BRDF LUT has a nonzero integrated response", image.getColor(4,4).r > 0.1f && image.getColor(4,4).r < 1.0f);
        return image;
    };
    check("nested BRDF LUT matches top-level pixels", equal(lut(flat.getBrdfLut()),lut(nested.getBrdfLut())));
}
class StackApp : public App {
    EventListener after_, render_;
    Fbo screenChild_, unfinished_, persistent_;
    Shader screenShader_, nestedShader_;
    int frame_ = 0;
public:
    void setup() override {
        setFps(VSYNC);
        screenChild_.allocate(20,16,4);
        unfinished_.allocate(8,8);
        persistent_.allocate(16,12);
        check("screen Shader loads", screenShader_.load(stack_mesh_shader_desc) && nestedShader_.load(stack_mesh_shader_desc));
        after_ = events().afterFrame.listen([this] {
            if (completed) return;
            check("present has only the bottom entry", internal::currentWindowContext().targetStack.size() == 1 && !unfinished_.isActive());
            Pixels screen; check("screen readback", grabScreen(screen));
            if (screen.isAllocated()) {
                expect(screen,4,4,Color(0,0,1),"lazy queued texture draw samples the completed child");
                expect(screen,screen.getWidth()/4,screen.getHeight()/2,Color(128.f/255,0,0),"screen first Shader draw survives child exactly once");
                expect(screen,3*screen.getWidth()/4,screen.getHeight()/2,Color(0,128.f/255,0),"screen second Shader draw survives child exactly once");
            }
            ++screenCaptures;
        });
        render_ = events().onRender.listen([this] {
            const auto& target = internal::currentTarget();
            check("onRender pass selected resumable stores before begin", target.resumable && target.inPass);
            screenChild_.begin(0,0,1); screenChild_.end();
        });
    }
    void draw() override {
        std::printf("FRAME renderPassStack index=%d windowSamples=%d\n", frame_,
                    internal::currentWindowContext().swapchainTarget.sampleCount);
        if (frame_ == 2) { completed = true; exitApp(); return; }
        if (frame_ == 0) {
            for (int samples : {1,4}) for (bool different : {false,true}) {
                stateAndPixels(samples,different);
                shaderPixels(samples,different);
            }
            mixedLayersPixels(1); mixedLayersPixels(4);
            inheritedBlendPixels(1); inheritedBlendPixels(4);
            depthPixels(1); depthPixels(4); misuseAndScopes();
            versionCopyPixels(); environmentPixels();
            persistent_.begin(1,0,0); persistent_.end();
        }
        if (frame_ == 1) {
            persistent_.begin();
            check("target started bit tracks its own new window epoch", internal::currentTarget().frameEpoch == internal::currentWindowContext().renderEpoch);
            persistent_.end();
            expect(read(persistent_),8,6,Color(1,0,0),"next-frame LOAD retains persistent pixels");
        }
        clear(0,0,0); setupScreenOrtho(); setBlendMode(BlendMode::Alpha);
        check("bottom remains lazy before explicit ensure", !internal::currentTarget().inPass);
        setColor(1,1,1); screenChild_.getTexture().draw(0,0,16,12);
        screenChild_.begin(0,0,1); screenChild_.end();
        check("producing a sampled Fbo does not eagerly flush the lazy bottom", !internal::currentTarget().inPass);
        internal::ensureCurrentPass();
        screenShader_.begin(); patch(screenShader_,false);
        const int reservation = internal::currentTarget().sglLayerNext;
        for (int i=0;i<2;++i) {
            internal::currentTarget().flushLayers();
            screenChild_.begin(0,0,0);
            rectangle(0,0,20,16,Color(0,1,0));
            screenChild_.end();
            expect(read(screenChild_),10,8,Color(0,1,0),"child draw and readback preserve an open screen Shader scope");
            check("screen scope retains nonnegative layer reservation", internal::currentTarget().sglLayerNext == reservation && reservation > 0);
        }
        nestedShader_.begin();
        screenChild_.begin(0,0,1); screenChild_.end();
        nestedShader_.end(); patch(screenShader_,true); screenShader_.end();
        // Deliberate misuse: also crosses Node::drawTree's matrix guard. Its
        // pushMatrix -1 warning occurs after every pixel case above, and
        // present() must unwind this target before afterFrame's readback.
        if (frame_ == 0) unfinished_.begin(0,0,1);
        ++frame_;
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 1) { std::puts("SKIP: use --gpu-check [--msaa4] for target-stack pixel tests"); return 0; }
    WindowSettings settings; settings.setSize(256,192); settings.setHighDpi(true);
    settings.sampleCount = argc > 2 && std::strcmp(argv[2],"--msaa4") == 0 ? 4 : 1;
    return tcCoreTest::runDisplayTest<StackApp>(settings, failures, [] {
        check("all stack cases completed", completed && screenCaptures >= 2);
    });
}
