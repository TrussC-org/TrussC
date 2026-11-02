// #821 PR 4a: compatible target selection, generations and corrected consumers.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "pipeline.glsl.h"
#include "ycocg.glsl.h"
#include "../../../../addons/tcxLut/src/tcLut.h"
#include "../../../../addons/tcxHap/src/impl/tcxHapDrawData.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include "../../../../core/platform/linux/tcVideoNV12Shader.h"
#endif
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace tc;
namespace {
int failures = 0;
bool completed = false;
fs::path output;
nlohmann::json manifest=nlohmann::json::object();
void check(const char* label, bool value) {
    std::printf("%s %s\n", value ? "PASS" : "FAIL", label);
    if (!value) ++failures;
}
Pixels read(Fbo& fbo) {
    Pixels pixels; pixels.allocate(fbo.getWidth(), fbo.getHeight(), 4);
    const auto format = fbo.getTextureFormat();
    if (format == TextureFormat::RGBA16F || format == TextureFormat::RGBA32F) {
        // D3D11 byte readback does not convert floating-point attachments.
        Pixels floats;
        floats.allocate(fbo.getWidth(), fbo.getHeight(), 4, PixelFormat::F32);
        check("pipeline fixture readback", fbo.readPixelsFloat(floats.getDataF32()));
        for (int y = 0; y < fbo.getHeight(); ++y)
            for (int x = 0; x < fbo.getWidth(); ++x)
                pixels.setColor(x, y, floats.getColor(x, y));
    } else {
        check("pipeline fixture readback", fbo.readPixels(pixels.getData()));
    }
    return pixels;
}
void save(const Pixels& pixels, const std::string& name) {
    if (output.empty()) return;
    fs::create_directories(output);
    check("pipeline fixture PNG saved", bool(pixels.save(output / (name + ".png"))));
    manifest[name]["size"]={pixels.getWidth(),pixels.getHeight()};
    for(const auto xy : {std::pair<int,int>{16,16}, {44,20}, {60,50}}) {
        if(xy.first>=pixels.getWidth() || xy.second>=pixels.getHeight()) continue;
        const auto c=pixels.getColor(xy.first,xy.second);
        manifest[name]["samples"].push_back({{"xy",{xy.first,xy.second}},
            {"rgba",{int(std::lround(c.r*255)),int(std::lround(c.g*255)),int(std::lround(c.b*255)),int(std::lround(c.a*255))}}});
    }
    std::ofstream file(output/"samples.json"); file<<manifest.dump(2)<<'\n';
}
void expect(const Pixels& pixels, int x, int y, Color expected, const char* label, int tolerance=2) {
    const auto c = pixels.getColor(x,y);
    std::printf("PIXEL %s (%d,%d)=(%.0f,%.0f,%.0f,%.0f)\n",label,x,y,c.r*255,c.g*255,c.b*255,c.a*255);
    auto matches=[&](float actual,float wanted) { return std::abs(std::lround(actual*255)-std::lround(wanted*255))<=tolerance; };
    check(label,matches(c.r,expected.r) && matches(c.g,expected.g) && matches(c.b,expected.b));
}
void patch(Shader& shader) {
    ShaderVertex vertices[] = {{-0.8f,-0.8f,0,0,0,0,1,0,1}, {0.8f,-0.8f,0,0,0,0,1,0,1},
        {-0.8f,0.8f,0,0,0,0,1,0,1}, {0.8f,0.8f,0,0,0,0,1,0,1}};
    shader.submitVertices(vertices,4,PrimitiveType::TriangleStrip);
}
class RecipeShader : public Shader {
public:
    internal::PipelineSelection selection() { return selectPipelineForCurrentTarget_(); }
    sg_pipeline_desc recipe() const { return recipe_; }
};
const char* formatName(TextureFormat format) { return format==TextureFormat::RGBA8?"rgba8":"rgba16f"; }
void signaturesAndLeases(int samples, TextureFormat format) {
    Fbo first, second; first.allocate(160,96,samples,format); second.allocate(113,79,samples,format,true);
    RecipeShader shader; check("recipe program loads",shader.load(pipeline_mesh_shader_desc));
    first.begin(0,0,0); setupScreenOrtho();
    auto& target=internal::currentTarget();
    const auto selected=shader.selection();
    const auto desc=sg_query_pipeline_desc(selected.raw);
    check("raw descriptor uses actual target attachments",desc.colors[0].pixel_format==target.colorFormat &&
        desc.sample_count==samples && desc.depth.pixel_format==target.depthFormat);
    check("equivalent raw recipe reuses pipeline",shader.selection().raw.id==selected.raw.id);
    auto changed=shader.recipe(); changed.colors[0].blend.enabled=false;
    const auto blend=target.pipeline(internal::PipelineKey::raw(internal::PipelineRole::Shader,changed));
    check("complete blend state separates recipes",blend.raw.id!=selected.raw.id);
    changed=shader.recipe(); changed.layout.buffers[0].stride=48;
    check("vertex layout separates recipes",target.pipeline(internal::PipelineKey::raw(internal::PipelineRole::Shader,changed)).raw.id!=selected.raw.id);
    changed=shader.recipe(); changed.primitive_type=SG_PRIMITIVETYPE_TRIANGLE_STRIP;
    check("topology separates recipes",target.pipeline(internal::PipelineKey::raw(internal::PipelineRole::Shader,changed)).raw.id!=selected.raw.id);
    changed=shader.recipe(); changed.depth.compare=SG_COMPAREFUNC_LESS_EQUAL; changed.depth.write_enabled=true;
    check("effective depth separates recipes",target.pipeline(internal::PipelineKey::raw(internal::PipelineRole::Shader,changed)).raw.id!=selected.raw.id);
    const auto context=target.resources().context.id;
    const auto immediate=internal::activeFill2D();
    Light shadow; shadow.setDirectional(0,-1,0); shadow.enableShadow(32); addLight(shadow);
    beginShadowPass(shadow);
    const auto& shadowTarget=internal::currentTarget();
    const auto shadowPipeline=internal::currentTarget().pipeline(shadowTarget.fixedPipeline);
    const auto shadowDesc=sg_query_pipeline_desc(shadowPipeline.raw);
    check("fixed shadow signature is R32F/DEPTH/1",shadowTarget.depthFormat==SG_PIXELFORMAT_DEPTH &&
        shadowDesc.depth.pixel_format==SG_PIXELFORMAT_DEPTH && shadowDesc.colors[0].pixel_format==SG_PIXELFORMAT_R32F && shadowDesc.sample_count==1);
    endShadowPass(); removeLight(shadow);
    patch(shader);
    second.begin(0,0,0); setupScreenOrtho();
    check("nested compatible targets share raw cache",shader.selection().raw.id==selected.raw.id);
    check("nested immediate contexts and pipelines are exclusive",internal::currentTarget().resources().context.id!=context && internal::activeFill2D().id!=immediate.id);
    second.end(); first.end();
    expect(read(first),80,48,Color(0,1,0),"queued shader survives nested lease");
    // Retire a compatible bucket while the parent keeps its old generation.
    first.begin(0,0,0); patch(shader);
    const auto old=shader.selection();
    second.allocate(114,80,samples,format);
    second.begin(0,0,0);
    const auto replacement=shader.selection();
    check("resize replaces generation while parked parent retains it",replacement.generation->serial!=old.generation->serial &&
          sg_query_pipeline_state(old.raw)==SG_RESOURCESTATE_VALID);
    second.end(); first.end();
    expect(read(first),80,48,Color(0,1,0),"parked generation queued pixels survive retirement");
    first.begin(0,0,0);
    {
        RecipeShader local; check("scope-local program loads",local.load(pipeline_mesh_shader_desc));
        patch(local);
        RecipeShader moved=std::move(local);
        const auto program=moved.selection().raw.id;
        check("move preserves program identity",program!=0 && !local.isLoaded());
        check("reload creates another program generation",moved.load(pipeline_mesh_shader_desc) && moved.selection().raw.id!=program);
    }
    first.end(); expect(read(first),80,48,Color(0,1,0),"destroy/reload retains captured program draws");
}
class PointFixtureNode : public App {
public:
    Mesh* points=nullptr;
    void draw() override { setColor(0,1,0); setPointSize(12); setPointStyle(PointStyle::Square); points->draw(); }
};
void pointsAndPremult(int samples, TextureFormat format) {
    auto& window=internal::currentWindowContext(); const float savedDpi=window.dpiScale;
    const bool savedMode=internal::pixelPerfectMode();
    const int savedWidth=window.fbWidth, savedHeight=window.fbHeight;
    window.fbWidth=512; window.fbHeight=384;
    Fbo fbo; fbo.allocate(160,96,samples,format);
    Mesh points; points.setMode(PrimitiveMode::Points); points.addVertex(40,40,0);
    Texture source; unsigned char bytes[]={128,0,0,128};
    source.allocate(1,1,TextureFormat::RGBA8,TextureUsage::Dynamic); source.loadData(bytes,1,1,4); source.setPremultipliedAlpha(true);
    for (float dpi : {1.f,2.f}) for (bool pixelMode : {false,true}) for (auto style : {PointStyle::Square,PointStyle::Round,PointStyle::Pixel}) {
        window.dpiScale=dpi; internal::pixelPerfectMode()=pixelMode;
        fbo.begin(0,0,0); setupScreenOrtho(); disableDepthTest(); setBlendMode(BlendMode::Alpha);
        setPointStyle(style); setPointSize(12); setColor(0,1,0); translate(20,10); points.draw();
        const auto expected=(getProjectionMatrix()*getViewMatrix()*getMatrix()).transposed();
        const auto& command=internal::currentTarget().deferredPointDraws.back().cmd;
        check("point snapshot uploads target P*V*M exactly once",std::memcmp(command.vsp.viewProj,expected.m,sizeof(expected.m))==0);
        const auto pointPrimitive=sg_query_pipeline_desc(command.pip).primitive_type;
        translate(80,30); fbo.end(); const auto pixels=read(fbo);
        int count=0,xFirst=160,xLast=-1,yFirst=96,yLast=-1;
        for(int y=0;y<96;++y) for(int x=0;x<160;++x) if(pixels.getColor(x,y).g>0.9f) {
            ++count; xFirst=std::min(xFirst,x); xLast=std::max(xLast,x); yFirst=std::min(yFirst,y); yLast=std::max(yLast,y);
        }
        if(style==PointStyle::Square) check("DPI 1/2 Fbo square exact 12x12 mask",count==144 && xLast-xFirst+1==12 && yLast-yFirst+1==12 && xFirst+xLast==119 && yFirst+yLast==99);
        if(style==PointStyle::Pixel) {
            double coverage=0; int occupied=0, left=160, right=-1, top=96, bottom=-1;
            for(int y=0;y<96;++y) for(int x=0;x<160;++x) {
                const auto c=pixels.getColor(x,y); coverage+=c.g;
                if(c.g>0) { ++occupied; left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y); }
            }
            std::printf("PIXEL MASK samples=%d coverage=%g occupied=%d bounds=%dx%d\n",samples,coverage,occupied,right-left+1,bottom-top+1);
            // A 1px native POINT remains one pixel of sample coverage. MSAA
            // can distribute that coverage over adjacent resolved pixels;
            // allow only the four-pixel byte-quantization bound, not a splat.
            check("Pixel retains one device-pixel coverage",std::abs(coverage-1)<=4.0/255 && occupied>0 && right-left+1<=2 && bottom-top+1<=2 &&
                  pointPrimitive==SG_PRIMITIVETYPE_POINTS);
        }
        if(style==PointStyle::Round) check("round point retains bounded circular mask",count>70 && count<=144 && xLast-xFirst+1<=12 && yLast-yFirst+1<=12);
        if(style==PointStyle::Square) save(pixels,"point-size-"+std::to_string(samples)+"-"+formatName(format)+"-dpi"+std::to_string(int(dpi))+"-pixel"+std::to_string(int(pixelMode)));
    }
    fbo.begin(0,0,0); setupScreenOrtho();
    auto& pointTarget=internal::currentTarget(); pointTarget.viewport={8,4,128,80,true};
    internal::applyTargetDrawingState();
    EasyCam camera; camera.setDistance(173); camera.begin();
    setPointStyle(PointStyle::Square); setPointSize(12); setColor(0,1,0);
    auto node=internal::makeConstructedApp<PointFixtureNode>(); node->points=&points; node->setPos(3,4);
    pushMatrix(); rotate(0.2f); scale(1.4f,0.7f);
    const auto nodeModel=getMatrix()*node->getLocalMatrix();
    const auto expectedNode=(getProjectionMatrix()*getViewMatrix()*nodeModel).transposed();
    node->handleDraw(); popMatrix();
    const auto captured=pointTarget.deferredPointDraws.back().cmd.vsp;
    check("Node point snapshot matches CPU-composed MVP",std::memcmp(captured.viewProj,expectedNode.m,sizeof(expectedNode.m))==0);
    const auto center=expectedNode.transposed()*Vec4(40,40,0,1);
    const int px=8+int((center.x/center.w+1)*64),py=4+int((1-center.y/center.w)*40);
    camera.end(); fbo.end();
    expect(read(fbo),px,py,Color(0,1,0),"Node/changed camera/nonuniform rotation point center");
    window.fbWidth=savedWidth; window.fbHeight=savedHeight;
    window.dpiScale=savedDpi; internal::pixelPerfectMode()=savedMode;
    fbo.begin(0,0,1); setupScreenOrtho(); setBlendMode(BlendMode::Alpha); setColor(1,1,1); source.draw(8,8,32,32); fbo.end();
    expect(read(fbo),16,16,Color(128.f/255,0,127.f/255),"premult source in target does not multiply alpha twice");
    fbo.begin(0.25f,0.25f,0.25f); setupScreenOrtho(); setBlendMode(BlendMode::Add); pushStyle(); setBlendMode(BlendMode::Alpha); popStyle();
    setColor(1,0,0,0.5f); drawRect(0,0,32,32); fbo.end();
    expect(read(fbo),16,16,Color(192.f/255,64.f/255,64.f/255),"Fbo style pop selects returned blend");
}
void transformedConsumers(int samples, TextureFormat format) {
    Fbo fbo; fbo.allocate(160,96,samples,format);
    Texture source; unsigned char bytes[]={255,0,0,255}; source.allocate(1,1,TextureFormat::RGBA8,TextureUsage::Dynamic); source.loadData(bytes,1,1,4);
    auto lut=tcx::lut::createIdentity(2); tcx::lut::LutShader shader; check("LUT loads",shader.load()); shader.setLut(lut); shader.setTexture(source);
#if defined(__linux__) && !defined(__ANDROID__)
    Texture planeY,planeUV; unsigned char yBytes[]={128,128,128,128},uvBytes[]={128,128};
    planeY.allocate(2,2,TextureFormat::R8,TextureUsage::Dynamic); planeY.loadData(yBytes,2,2,1);
    planeUV.allocate(1,1,TextureFormat::RG8,TextureUsage::Dynamic); planeUV.loadData(uvBytes,1,1,2);
    internal::NV12VideoShader nv12; nv12.loadShader();
#endif
    float table[24]; tcx::lut::generateIdentity(table,2);
    for(int i=0;i<24;i+=3) std::swap(table[i],table[i+2]);
    tcx::lut::Lut3D swapped; swapped.allocate(2,table);
    Shader hap; check("synthetic YCoCg program loads",hap.load(ycocg_shader_desc));
    Texture hapTexture; unsigned char hapBytes[]={128,128,0,128};
    hapTexture.allocate(1,1,TextureFormat::RGBA8,TextureUsage::Dynamic); hapTexture.loadData(hapBytes,1,1,4);
    hap.setTexture(0,hapTexture.getView(),hapTexture.getSampler());
    for(int path=0;path<4;++path) {
#if !defined(__linux__) || defined(__ANDROID__)
        if(path==1) { std::puts("SKIP NV12 fixture: Linux-only video backend"); continue; }
#endif
        fbo.begin(0,0,0); setupScreenOrtho(); translate(20,10); scale(2,1);
        if(path!=2) pushScissor(40,16,12,16);
        if(path==0) shader.draw(5,5,20,24);
        if(path==2) {
            const auto& target=internal::currentTarget();
            const auto data=tcx::hap::detail::makeYCoCgDrawData(5,5,20,24,target.camera.currentProjectionMatrix,target.camera.currentViewMatrix,target.currentMatrix,Color(1,1,1));
            const auto mvp=(getProjectionMatrix()*getViewMatrix()*getMatrix()).transposed();
            check("Hap packs the target MVP once",std::memcmp(data.uniforms.mvp,mvp.m,sizeof(mvp.m))==0);
            hap.setUniform(0,&data.uniforms,sizeof(data.uniforms)); hap.submitVertices(data.vertices,4,PrimitiveType::Quads);
        }
        if(path==3) { shader.setLut(swapped); shader.drawSubsection(5,5,20,24,0,0,1,1); }
#if defined(__linux__) && !defined(__ANDROID__)
        if(path==1) {
            VideoPlayer player;
            internal::VideoPlayerPlatformAccess::drawNV12ForTests(player,&nv12,std::move(planeY),std::move(planeUV),5,5,20,24);
        }
#endif
        if(path!=2) popScissor(); fbo.end(); auto pixels=read(fbo);
        expect(pixels,44,20,path==0?Color(1,0,0):path==1?Color(131.f/255,130.f/255,131.f/255):path==2?Color(0.502f,0.504f,0.498f):Color(0,0,1),"transformed clipped consumer interior",path==1?1:2);
        if(path==2) {
            // Hap already follows the target MVP. General deferred scissor
            // snapshots are outside PR 4a; this fixture verifies its geometry.
            expect(pixels,28,20,Color(0,0,0),"Hap transformed rectangle left exterior");
            expect(pixels,72,20,Color(0,0,0),"Hap transformed rectangle right exterior");
        } else {
            expect(pixels,36,20,Color(0,0,0),"consumer respects target clip exterior");
            expect(pixels,55,20,Color(0,0,0),"consumer respects right clip edge");
            check("clipped MSAA resolve keeps opaque clear outside geometry",std::lround(pixels.getColor(36,20).a*255)==255);
        }
        save(pixels,std::string(path==0?"lut-transform-":path==1?"nv12-transform-":path==2?"hap-transform-":"lut-swap-subsection-")+std::to_string(samples)+"-"+formatName(format));
    }
    Texture halfSource; unsigned char halfBytes[]={255,0,0,255,0,255,0,128};
    halfSource.setFilter(TextureFilter::Nearest);
    halfSource.allocate(2,1,TextureFormat::RGBA8,TextureUsage::Dynamic); halfSource.loadData(halfBytes,2,1,4);
    shader.setLut(lut); shader.setTexture(halfSource);
    fbo.begin(0,0,0); setupScreenOrtho(); translate(20,10); scale(2,1); pushScissor(40,16,12,16);
    shader.drawSubsection(5,5,20,24,1,0,1,1); popScissor(); fbo.end();
    auto halfPixels=read(fbo);
    expect(halfPixels,44,20,Color(0,128.f/255,0),"LUT half-alpha subsection UVs");
    check("LUT retains its established alpha equation",std::abs(std::lround(halfPixels.getColor(44,20).a*255)-128)<=1);
    save(halfPixels,"lut-half-subsection-"+std::to_string(samples)+"-"+formatName(format));
#if defined(__linux__) && !defined(__ANDROID__)
    // Fractional host DPI cannot leak into an attachment-coordinate NV12 draw.
    Texture fractionalY,fractionalUV;
    fractionalY.allocate(2,2,TextureFormat::R8,TextureUsage::Dynamic); fractionalY.loadData(yBytes,2,2,1);
    fractionalUV.allocate(1,1,TextureFormat::RG8,TextureUsage::Dynamic); fractionalUV.loadData(uvBytes,1,1,2);
    auto& window=internal::currentWindowContext();
    const bool originalPixel=internal::pixelPerfectMode();
    const int originalWidth=window.fbWidth, originalHeight=window.fbHeight; const float originalDpi=window.dpiScale;
    window.fbWidth=1001; window.fbHeight=751; window.dpiScale=1.5f;
    for(bool pixelMode:{false,true}) {
        internal::pixelPerfectMode()=pixelMode;
        fbo.begin(0,0,0); setupScreenOrtho(); translate(20,10); scale(2,1); pushScissor(40,16,12,16);
        nv12.draw(5,5,20,24,fractionalY,fractionalUV); popScissor(); fbo.end();
        expect(read(fbo),44,20,Color(131.f/255,130.f/255,131.f/255),"NV12 fractional 1001x751 / DPI 1.5 target control",1);
    }
    window.fbWidth=originalWidth; window.fbHeight=originalHeight; window.dpiScale=originalDpi;
    internal::pixelPerfectMode()=originalPixel;
#endif

}
void lazyResizeChecks() {
#if defined(SOKOL_GLCORE)
    // This synthetic window probe commits several times in one app tick.
    // Metal retains one drawable per tick, so keep it on the GL backend.
    auto& main = internal::currentWindowContext();
    auto& budget = internal::sglBudget();
    const int oldVertices = budget.maxVertices, oldCommands = budget.maxCommands;
    const bool statsEnabled = sg_stats_enabled();
    sg_enable_stats();
    auto triangle = [] {
        clear(0,0,0);
        sgl_matrix_mode_projection(); sgl_load_identity();
        sgl_matrix_mode_modelview(); sgl_load_identity();
        internal::loadPipeline(internal::activeFill2D()); sgl_c4f(1,0,0,1);
        sgl_begin_triangles(); sgl_v2f(-0.75f,0.75f); sgl_v2f(0.25f,0.75f); sgl_v2f(-0.75f,-0.25f); sgl_end();
        check("resize probe starts with one lazy triangle",!internal::currentTarget().inPass && sgl_num_vertices()==3 && sgl_tc_context_has_pending_draws(sgl_get_context()));
    };
    auto backendPixels = [](const char* label) {
        auto& owner = internal::currentWindowContext();
        const auto epoch = owner.renderEpoch;
        Pixels pixels;
        // present() is host-owned. This synthetic tick reads the completed
        // owner's source through the internal path, before its next beginFrame,
        // rather than treating another owner's pending frame as a screenshot.
        const bool captured = captureWindow(pixels);
        check("lazy resize finished-owner readback",captured);
        check("lazy resize capture keeps the finished owner closed",!owner.framePending &&
            !owner.swapchainTarget.inPass && !owner.swapchainTarget.passStartedThisFrame && owner.renderEpoch==epoch);
        if (captured) expect(pixels,pixels.getWidth()/5,pixels.getHeight()/5,Color(1,0,0),label);
    };
    triangle();
    const int budgetCommands=sgl_num_commands();
    const auto before = main.swapchainTarget.resources().context.id;
    const auto generationBefore = main.swapchainTarget.resources().generation;
    internal::resizeSgl(1024,256);
    check("budget rebuild retains lazy recording",main.swapchainTarget.resources().context.id==before && sgl_num_vertices()==3 && sgl_num_commands()==budgetCommands);
    present();
    auto draws = sg_query_stats().prev_frame.num_draw;
    std::printf("LAZY RESIZE GPU budget draws=%u expected=1\n",draws);
    check("budget resize submits exactly one backend draw",draws==1);
    backendPixels("budget resize triangle reached backend");
    beginFrame();
    check("budget rebuild retries after present",main.swapchainTarget.resources().generation!=generationBefore && budget.maxVertices==1024 && budget.maxCommands==256 && !budget.pendingResize);
    internal::resizeSgl(oldVertices,oldCommands);
    beginFrame();
    {
        // A secondary-window coordinator using this native drawable. Alter its
        // reported dimensions exactly as the reviewer's windowless probe did.
        Window secondaryOwner;
        auto& secondary = trussc::internal::WindowRequestAccess::context(secondaryOwner);
        internal::WindowRequestAccess::binding(secondaryOwner).metadata.dpi_scale = secondary.dpiScale;
        secondary.fbWidth=256; secondary.fbHeight=192;
        secondary.swapchainTarget.colorFormat=main.swapchainTarget.colorFormat;
        secondary.swapchainTarget.depthFormat=main.swapchainTarget.depthFormat;
        secondary.swapchainTarget.sampleCount=main.swapchainTarget.sampleCount;
        secondary.acquireSwapchain=[](void*) { return sglue_swapchain(); };
        internal::currentWindowCtx()=&secondary;
        internal::prepareWindowTarget(); setupScreenOrtho();
        triangle();
        const int dimensionCommands=sgl_num_commands();
        const auto oldContext = secondary.swapchainTarget.resources().context.id;
        secondary.fbWidth=320; secondary.fbHeight=240;
        internal::prepareWindowTarget();
        check("dimension rebuild retains lazy recording",secondary.swapchainTarget.resources().context.id==oldContext && sgl_num_vertices()==3 && sgl_num_commands()==dimensionCommands);
        present();
        draws=sg_query_stats().prev_frame.num_draw;
        std::printf("LAZY RESIZE GPU dimensions draws=%u expected=1\n",draws);
        check("dimension resize submits exactly one backend draw",draws==1);
        // The coordinator's dimensions are synthetic; read its borrowed native
        // drawable at the actual size, keeping the completed owner selected.
        // Capturing main here would start its pending frame and CLEAR the same
        // surface. Real secondary windows own a separate native drawable.
        secondary.fbWidth=secondary.lastSwapchain.width; secondary.fbHeight=secondary.lastSwapchain.height;
        backendPixels("dimension resize triangle reached backend");
        secondary.fbWidth=320; secondary.fbHeight=240;
        internal::prepareWindowTarget();
        check("dimension rebuild retries after present",secondary.swapchainTarget.resources().context.id!=oldContext && secondary.swapchainTarget.resources().width==320);
        secondary.teardownTargets();
        internal::currentWindowCtx()=&main;
    }
    sgl_set_context(main.swapchainTarget.resources().context);
    beginFrame();
    if (!statsEnabled) sg_disable_stats();
#else
    std::puts("SKIP lazy resize native probe: requires GL; dummy covers the coordinator cases");
#endif
}
class PipelineApp : public App {
    int frame_=0, baseVertices_=0, baseCommands_=0;
    Fbo survivor_, otherSurvivor_;
    std::shared_ptr<Window> secondary_;
    EventListener after_;
public:
    void setup() override {
        baseVertices_=internal::sglBudget().maxVertices; baseCommands_=internal::sglBudget().maxCommands;
        survivor_.allocate(32,24); otherSurvivor_.allocate(29,19,4);
        if (internal::supportsSecondaryWindows()) {
            WindowSettings secondary; secondary.setSize(80,60);
            secondary_ = createWindow(secondary);
            check("secondary window created for global resize",bool(secondary_));
        } else {
            // Omits creation + 50 generation checks: 51 fewer assertions.
            std::puts("SKIP secondary window created for global resize: secondary windows unsupported");
        }
        after_=events().afterFrame.listen([this] {
            if(frame_==2) {
                Pixels pixels; check("blend-return screen capture",grabScreen(pixels));
                const float density=internal::targetPixelScale();
                for(int row=0;row<2;++row) expect(pixels,int(16*density),int((32+80*row)*density),Color(192.f/255,64.f/255,64.f/255),"lazy/active blend after end is Add");
                save(pixels,"blend-after-end.after");
            }
        });
    }
    void draw() override {
        if(frame_==0) {
            lazyResizeChecks();
            for(int samples:{1,4}) for(auto format:{TextureFormat::RGBA8,TextureFormat::RGBA16F}) {
                signaturesAndLeases(samples,format); pointsAndPremult(samples,format); transformedConsumers(samples,format);
            }
        }
        if(frame_==1) {
            setupScreenOrtho(); clear(0.25f,0.25f,0.25f); disableDepthTest();
            for(int row=0;row<2;++row) {
                setBlendMode(BlendMode::Alpha); if(row) internal::ensureCurrentPass();
                survivor_.begin(0,0,0); setBlendMode(BlendMode::Add); survivor_.end();
                setColor(1,0,0,0.5f); drawRect(0,float(row*80),40,64);
                setBlendMode(BlendMode::Add); drawRect(48,float(row*80),40,64);
            }
        }
        if(frame_>=2 && frame_<52) {
            // Real sgl rebuild, fifty separate frames; two surviving Fbos and
            // the window must never retain contexts from the previous setup.
            survivor_.begin(1,0,0); const auto old=internal::currentTarget().resources().generation; survivor_.end();
            otherSurvivor_.begin(0,0,1); otherSurvivor_.end();
            const auto windowGeneration = secondary_ ? trussc::internal::WindowRequestAccess::context(*secondary_).swapchainTarget.resources().generation : 0;
            internal::resizeSgl(baseVertices_+(frame_%2?0:64),baseCommands_+(frame_%2?0:16));
            if(secondary_) check("sgl resize replaces secondary window generation",trussc::internal::WindowRequestAccess::context(*secondary_).swapchainTarget.resources().generation != windowGeneration && trussc::internal::WindowRequestAccess::context(*secondary_).swapchainTarget.resources().context.id != 0);
            otherSurvivor_.begin(0,0,1); otherSurvivor_.end();
            expect(read(otherSurvivor_),4,4,Color(0,0,1),"second surviving lease after global resize");
            survivor_.allocate(frame_%2?113:160,frame_%2?79:96,frame_%2?4:1,frame_%2?TextureFormat::RGBA16F:TextureFormat::RGBA8);
            survivor_.begin(0,1,0); check("resize recreates lease generation",internal::currentTarget().resources().generation!=old); survivor_.end();
            expect(read(survivor_),4,4,Color(0,1,0),"resize survivor pixels");
        }
        ++frame_;
        if(frame_==53) { if(secondary_) secondary_->close(); completed=true; exitApp(); }
    }
};
}
TC_CORE_TEST_MAIN(int argc,char** argv) {
    if(argc==1) { std::puts("SKIP: use --gpu-check [--msaa4] [--record dir]"); return 0; }
    WindowSettings settings; settings.setSize(256,192); settings.setHighDpi(true); settings.sampleCount=1;
    for(int i=1;i<argc;++i) {
        if(std::strcmp(argv[i],"--msaa4")==0) settings.sampleCount=4;
        if(std::strcmp(argv[i],"--record")==0 && i+1<argc) output=fs::absolute(argv[++i]).lexically_normal();
    }
    return tcCoreTest::runDisplayTest<PipelineApp>(settings,failures,[]{ check("pipeline suite and resize x50 completed",completed); });
}
