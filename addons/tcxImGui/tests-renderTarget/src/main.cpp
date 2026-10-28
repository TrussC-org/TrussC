#include <TrussC.h>
#include <tcxImGui.h>
#ifndef TC_IMGUI_BASE_FIXTURE
#include <tcxNodeInspector.h>
#endif
#include "../../../../core/tests/common/tcDisplayTest.h"
#include <chrono>
#include <cstdio>
#include <fstream>
using namespace tc;
using namespace tcx::imgui;
namespace {
int failures = 0, errors = 0, validationErrors = 0, screenSamples = 1;
bool completed = false, screenOnly = false, nativeDpi2 = false;
fs::path output, baseline;
nlohmann::json manifest = nlohmann::json::object();
void check(const char* label, bool passed) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", label);
    if (!passed) ++failures;
}
void pixel(const Pixels& image, int x, int y, std::array<int,4> expected, const char* label, bool includeAlpha=true) {
    auto c = image.getColor(x,y);
    std::array<int,4> actual = {int(std::lround(c.r*255)), int(std::lround(c.g*255)),
        int(std::lround(c.b*255)), int(std::lround(c.a*255))};
    bool passed = true;
    for (int i=0;i<(includeAlpha?4:3);++i) passed &= std::abs(actual[i]-expected[i])<=1;
    std::printf("PIXEL %s: %d %d %d %d\n",label,actual[0],actual[1],actual[2],actual[3]);
    check(label,passed);
}
Pixels read(Fbo& fbo) {
    Pixels image; image.allocate(fbo.getWidth(),fbo.getHeight(),4);
    check("Fbo readback", fbo.readPixels(image.getData())); return image;
}
void capture(const Pixels& image, const std::string& name) {
    if (output.empty()) return;
    fs::create_directories(output);
    check("PNG saved", bool(image.save(output/(name+".png"))));
    manifest["captures"][name]["size"]={image.getWidth(),image.getHeight()};
    for (int x : {4,20,44,68,92}) {
        auto c=image.getColor(x,20);
        manifest["captures"][name]["samples"].push_back({int(std::lround(c.r*255)),int(std::lround(c.g*255)),int(std::lround(c.b*255)),int(std::lround(c.a*255))});
    }
    if (!baseline.empty()) {
        Pixels previous;
        if (fs::exists(baseline/(name+".png"))) {
            check("base PNG loaded",bool(previous.load(baseline/(name+".png"))));
            bool equal=previous.getWidth()==image.getWidth() && previous.getHeight()==image.getHeight() &&
                previous.getChannels()==image.getChannels();
            if(equal) equal=std::memcmp(previous.getData(),image.getData(),image.getWidth()*image.getHeight()*image.getChannels())==0;
            check("screen UI byte-identical to base",equal);
        } else if(name.find("imguiScreenDpi2")==0) check("screen baseline exists",false);
    }
    std::ofstream file(output/"samples.json"); file<<manifest.dump(2)<<'\n';
}
void fixedContext() {
    ImGui::GetIO().IniFilename=nullptr;
    ImGui::GetStyle().AntiAliasedFill=false;
    ImGui::GetStyle().AntiAliasedLines=false;
}
void marker(ImU32 color, float x=10) {
    ImGui::GetBackgroundDrawList()->AddRectFilled({x,10},{x+20,30},color);
}
#ifndef TC_IMGUI_BASE_FIXTURE
void noOpCallback(const ImDrawList*, const ImDrawCmd*) {}
void transparent(int samples, TextureFormat format, bool unfilterable) {
    ImGuiManager ui; ui.setupOffscreen(); fixedContext();
    Fbo fbo, parent; fbo.allocate(128,64,samples,format); parent.allocate(128,64,samples,format);
    Texture white;
    unsigned char bytes[]={255,255,255,255}; float floatBytes[]={1,1,1,1};
    white.allocate(1,1,unfilterable ? TextureFormat::RGBA32F : TextureFormat::RGBA8,TextureUsage::Dynamic);
    white.loadData(unfilterable ? static_cast<const void*>(floatBytes) : static_cast<const void*>(bytes),1,1,4);
    if (unfilterable && sg_query_pixelformat(SG_PIXELFORMAT_RGBA32F).filter)
        std::puts("SKIP unfilterable texture dispatch: backend supports RGBA32F filtering; both pipeline descriptors are checked");
    sg_sampler_desc samplerDesc={}; samplerDesc.min_filter=SG_FILTER_NEAREST; samplerDesc.mag_filter=SG_FILTER_NEAREST;
    const auto sampler=sg_make_sampler(&samplerDesc);
    const auto texture=unfilterable ? simgui_imtextureid_with_sampler(white.getView(),sampler) :
        simgui_imtextureid(white.getView());
    fbo.begin(0,0,0,0);
    const bool savedMode=internal::pixelPerfectMode(); auto& window=internal::currentWindowContext();
    const auto savedDpi=window.dpiScale; const bool savedMain=window.isMain;
    window.dpiScale=2; internal::pixelPerfectMode()=true;
    {
        auto scope=ui.scoped(); fixedContext(); check("offscreen scope accepted",bool(scope));
        check("offscreen takes Fbo extent and density",ImGui::GetIO().DisplaySize.x==128 &&
            ImGui::GetIO().DisplaySize.y==64 && ImGui::GetIO().DisplayFramebufferScale.x==1);
        auto* list=ImGui::GetBackgroundDrawList();
        auto rect=[&](float x, ImU32 color) { list->AddImage(texture,{x,8},{x+16,32},{0,0},{1,1},color); };
        rect(12,IM_COL32(255,0,0,255)); rect(36,IM_COL32(255,0,0,128));
        rect(60,IM_COL32(255,0,0,128)); rect(84,IM_COL32(255,0,0,255));
        list->AddCallback(noOpCallback,nullptr);
        rect(60,IM_COL32(0,255,0,128)); rect(84,IM_COL32(0,255,0,128));
        list->PushClipRect({110,10},{116,20},true);
        rect(106,IM_COL32(0,0,255,255)); list->PopClipRect();
    }
    int recipesChecked=0;
    for (const auto& item : internal::currentTarget().resources().rawCache->cache) {
        const auto desc=sg_query_pipeline_desc(item.second);
        if(desc.layout.buffers[0].stride!=sizeof(ImDrawVert)) continue;
        ++recipesChecked;
        check("offscreen recipe uses actual target signature",desc.colors[0].pixel_format==internal::currentTarget().colorFormat && desc.sample_count==samples && desc.depth.pixel_format==internal::currentTarget().depthFormat);
        const auto& color=desc.colors[0];
        check("offscreen RGBA source-over recipe",color.write_mask==SG_COLORMASK_RGBA && color.blend.enabled &&
            color.blend.op_rgb==SG_BLENDOP_ADD && color.blend.op_alpha==SG_BLENDOP_ADD &&
            color.blend.src_factor_rgb==SG_BLENDFACTOR_SRC_ALPHA && color.blend.dst_factor_rgb==SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA &&
            color.blend.src_factor_alpha==SG_BLENDFACTOR_ONE && color.blend.dst_factor_alpha==SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA);
    }
    check("both ImGui sampling recipes selected",recipesChecked==2);
    window.isMain=savedMain; window.dpiScale=savedDpi; internal::pixelPerfectMode()=savedMode;
    setupScreenOrtho(); setColor(0,0,1); drawRect(120,40,8,8);
    fbo.end();
    auto attachment=read(fbo);
    const std::array<std::array<int,4>,5> values={{{0,0,0,0},{255,0,0,255},{128,0,0,128},{64,128,0,192},{127,128,0,255}}};
    const std::array<std::array<int,4>,5> composites={{{0,0,255,255},{255,0,0,255},{128,0,127,255},{64,128,63,255},{127,128,0,255}}};
    int row=0; for(int x:{4,20,44,68,92}) pixel(attachment,x,20,values[row++],"transparent attachment table");
    if(format==TextureFormat::RGBA16F) {
        std::vector<float> floats(128*64*4);
        check("half-float attachment readback",fbo.readPixelsFloat(floats.data()));
        const float alpha=128.f/255.f;
        const std::array<std::array<float,4>,5> ideal={{{0,0,0,0},{1,0,0,1},
            {alpha,0,0,alpha},{alpha*(1-alpha),alpha,0,2*alpha-alpha*alpha},{1-alpha,alpha,0,1}}};
        row=0;
        for(int x:{4,20,44,68,92}) {
            bool matches=true;
            for(int channel=0;channel<4;++channel)
                matches &= std::abs(floats[(20*128+x)*4+channel]-ideal[row][channel])<=1.f/1024.f;
            check("normalized RGBA16F equation within half precision",matches); ++row;
        }
    }
    pixel(attachment,124,44,{0,0,255,255},"Fbo drawing continues after UI scissor reset");
    pixel(attachment,112,15,{0,0,255,255},"clipped interior"); pixel(attachment,118,15,{0,0,0,0},"clipped exterior");
    parent.begin(0,0,1,1); setupScreenOrtho(); setColor(1); setBlendMode(BlendMode::Alpha); fbo.draw(0,0); parent.end();
    auto composite=read(parent); row=0;
    for(int x:{4,20,44,68,92}) pixel(composite,x,20,composites[row++],"blue parent table");
    const std::string suffix=std::string(samples==1?".s1":".s4")+(format==TextureFormat::RGBA8?".rgba8":".rgba16f")+(unfilterable?".unfilterable":".filtered");
    const auto actualSampling=sg_query_pixelformat(unfilterable?SG_PIXELFORMAT_RGBA32F:SG_PIXELFORMAT_RGBA8).filter?"filtered":"unfilterable";
    manifest["captures"]["imguiTransparentFbo.attachment"+suffix]["actualSampling"]=actualSampling;
    manifest["captures"]["imguiTransparentFbo.parent"+suffix]["actualSampling"]=actualSampling;
    capture(attachment,"imguiTransparentFbo.attachment"+suffix);
    capture(composite,"imguiTransparentFbo.parent"+suffix);
    // Parent is itself premultiplied and can be chained through another target.
    Fbo chain; chain.allocate(128,64); chain.begin(0,0,1,1); setupScreenOrtho(); parent.draw(0,0); chain.end();
    row=0; for(int x:{4,20,44,68,92}) pixel(read(chain),x,20,composites[row++],"premultiplied chain");
    setupScreenOrtho(); setColor(1); parent.draw(256,0);
    sg_destroy_sampler(sampler);
}
void refusalAndContexts() {
    ImGuiManager a,b; a.setupOffscreen(); fixedContext(); b.setupOffscreen(); fixedContext();
    Fbo first,second; first.allocate(128,64); second.allocate(128,64,4,TextureFormat::RGBA16F);
    ImGuiContext* firstContext=nullptr;
    first.begin(0,0,0,0);
    {
        auto scope=a.scoped(); firstContext=ImGui::GetCurrentContext(); fixedContext(); check("first context accepted",bool(scope)); marker(IM_COL32(255,0,0,255));
        const auto context=ImGui::GetCurrentContext();
        second.begin(0,0,0,0);
        { auto child=b.scoped(); fixedContext(); check("nested distinct context accepted",bool(child)); marker(IM_COL32(0,255,0,255),40); }
        second.end(); check("nested scope restores ImGui context",ImGui::GetCurrentContext()==context);
    }
    first.end();
    auto originalFirst=read(first);
    capture(originalFirst,"imguiSameContextTwice.first");
    const auto before=sg_query_stats().cur_frame;
    const int firstFrameNumber=firstContext->FrameCount;
    const int previousErrors=errors;
    first.begin();
    { auto scope=a.scoped(); check("same context refused",!scope); if(scope) marker(IM_COL32(0,0,255,255)); }
    first.end();
    check("exactly one tc Error for refusal",errors==previousErrors+1);
    check("refusal happens before ImGui NewFrame",firstContext->FrameCount==firstFrameNumber);
    const auto after=sg_query_stats().cur_frame;
    check("refusal makes zero upload/draw calls",before.num_update_buffer==after.num_update_buffer && before.num_draw==after.num_draw);
    manifest["checks"]["imguiSameContextTwice"]={{"tcErrors",errors-previousErrors},
        {"bufferUploads",after.num_update_buffer-before.num_update_buffer},{"drawCalls",after.num_draw-before.num_draw},
        {"sokolValidationErrors",validationErrors}};
    auto firstPixels=read(first), secondPixels=read(second);
    pixel(firstPixels,20,20,{255,0,0,255},"first pixels preserved"); pixel(secondPixels,50,20,{0,255,0,255},"other context target");
    check("first attachment byte-identical after refusal",std::memcmp(originalFirst.getData(),firstPixels.getData(),128*64*4)==0);
    capture(firstPixels,"imguiSameContextTwice.refused");
    capture(firstPixels,"imguiTwoContextsTwoFbos.a"); capture(secondPixels,"imguiTwoContextsTwoFbos.b");
    Fbo parent; parent.allocate(256,64); parent.begin(0,0,1,1); setupScreenOrtho(); setColor(1); first.draw(0,0); second.draw(128,0); parent.end();
    const auto parentPixels=read(parent);
    pixel(parentPixels,20,20,{255,0,0,255},"context A composite");
    pixel(parentPixels,178,20,{0,255,0,255},"context B composite");
    pixel(parentPixels,100,20,{0,0,255,255},"contexts do not leak");
    capture(parentPixels,"imguiTwoContextsTwoFbos.parent");
    setupScreenOrtho(); parent.draw(0,0);
    const int differentErrors=errors;
    second.begin(); internal::currentTarget().flushLayers(); clear(0,0,0,0);
    {auto scope=a.scoped(); check("same context another target refused after restart",!scope); if(scope) marker(IM_COL32(0,0,255,255));}
    second.end();
    pixel(read(second),20,20,{0,0,0,0},"different-target refused scope writes no pixels"); check("one Error on different target",errors==differentErrors+1);
    manifest["checks"]["differentTargetRefusal"]["tcErrors"]=errors-differentErrors;
    first.allocate(96,64,4);
    first.begin(0,0,0,0);
    const int resizeErrors=errors;
    const auto resizeStats=sg_query_stats().cur_frame;
    {auto scope=a.scoped(); check("resize does not reset context reservation",!scope); if(scope) marker(IM_COL32(0,0,255,255));}
    const auto resizeAfter=sg_query_stats().cur_frame;
    check("resize refusal has no uploads/draws",resizeStats.num_update_buffer==resizeAfter.num_update_buffer && resizeStats.num_draw==resizeAfter.num_draw);
    first.end();
    check("resize refusal reports once",errors==resizeErrors+1);
    manifest["checks"]["resizeRefusal"]["tcErrors"]=errors-resizeErrors;
    pixel(read(first),20,20,{0,0,0,0},"resized target remains clear after refusal");
    // Reserve the window's context now; attempting an offscreen scope must not
    // replace the pending UI frame that the ordinary onRender listener consumes.
    imguiSetup(); fixedContext(); check("window begin accepted",imguiBegin()); marker(IM_COL32(255,255,0,255),450/getDpiScale()); imguiEnd();
    const int windowErrors=errors;
    first.begin(); {auto scope=ImGuiManager::instance().scoped(); check("pending window reservation refused",!scope);} first.end();
    check("pending frame refusal reports once",errors==windowErrors+1);
    manifest["checks"]["pendingWindowRefusal"]["tcErrors"]=errors-windowErrors;

}
void targetAssociation() {
    ImGuiManager ui; ui.setupOffscreen();
    Fbo first,second; first.allocate(64,64); second.allocate(64,64);
    first.begin(0,0,0,0);
    const int previousErrors=errors;
    {
        auto scope=ui.scoped(); check("association test begins",bool(scope));
        if(scope) marker(IM_COL32(255,0,0,255));
        second.begin(0,0,0,0);
        // Ending the scope with a different target active must discard its UI.
    }
    check("cross-target end emits one Error",errors==previousErrors+1);
    second.end(); first.end();
    pixel(read(first),20,20,{0,0,0,0},"mismatched UI does not reach first target");
    pixel(read(second),20,20,{0,0,0,0},"mismatched UI does not leak to second target");
    const auto previous=simgui_tc_get_context();
    auto* previousImGui=ImGui::GetCurrentContext();
    {
        ImGuiManager windowUi; windowUi.setup(); fixedContext();
        check("independent window context begins",windowUi.begin());
        marker(IM_COL32(255,0,0,255));
        first.begin(0,0,0,0);
        const int windowErrors=errors;
        windowUi.end();
        check("window end rejects wrong target immediately",errors==windowErrors+1);
        first.end();
        pixel(read(first),20,20,{0,0,0,0},"window UI does not leak to child");
    }
    simgui_tc_set_context(previous); ImGui::SetCurrentContext(previousImGui);

}

#endif
void screenUi(bool pixelMode) {
    auto& window=internal::currentWindowContext();
    if(!nativeDpi2) {
        check("main framebuffer metadata initialized", !window.isMain || (window.fbWidth==sapp_width() && window.fbHeight==sapp_height()));
        window.dpiScale=2;
    }
    internal::pixelPerfectMode()=pixelMode;
    imguiBegin();
    check("window DisplaySize 256x192",ImGui::GetIO().DisplaySize.x==256 && ImGui::GetIO().DisplaySize.y==192);
    check("window FramebufferScale stays 2",ImGui::GetIO().DisplayFramebufferScale.x==2 && getDpiScale()==2);
    marker(IM_COL32(255,0,0,255));
    auto* list=ImGui::GetBackgroundDrawList();
    list->AddText({40,40},IM_COL32_WHITE,"fixed font");
    list->PushClipRect({40,60},{50,70},true); list->AddRectFilled({30,50},{60,80},IM_COL32_WHITE); list->PopClipRect();
    ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x-66,ImGui::GetIO().DisplaySize.y-52});
    ImGui::SetNextWindowSize({64,50});
    ImGui::Begin("anchor",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    ImGui::Button("fixed"); ImGui::End(); imguiEnd();
}
void screenCapture(const std::string& name) {
    Pixels image; const bool captured=bool(grabScreen(image));
    check("afterFrame grabScreen includes finished ImGui",captured);
    if(!captured) return;
    pixel(image,20,20,{255,0,0,255},"DPI 2 first device pixel",false);
    pixel(image,59,59,{255,0,0,255},"DPI 2 last device pixel",false);
    pixel(image,19,20,{0,0,0,255},"DPI 2 outside left",false);
    pixel(image,60,20,{0,0,0,255},"DPI 2 outside right",false);
    capture(image,name);
    manifest["captures"][name]["targetSamples"]=internal::currentWindowContext().swapchainTarget.sampleCount;
#ifndef TC_IMGUI_BASE_FIXTURE
    int checked=0;
    for(const auto& item : internal::currentTarget().resources().rawCache->cache) {
        const auto desc=sg_query_pipeline_desc(item.second);
        if(desc.layout.buffers[0].stride!=sizeof(ImDrawVert)) continue;
        ++checked;
        check("screen RGB mask and original blend",desc.colors[0].write_mask==SG_COLORMASK_RGB &&
            desc.colors[0].blend.src_factor_rgb==SG_BLENDFACTOR_SRC_ALPHA &&
            desc.colors[0].blend.dst_factor_rgb==SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA &&
            desc.colors[0].blend.src_factor_alpha==SG_BLENDFACTOR_ONE && desc.colors[0].blend.dst_factor_alpha==SG_BLENDFACTOR_ZERO);
    }
    check("both screen sampling recipes selected",checked==2);
#endif
}
bool secondaryComplete=false;
bool secondaryPresented=false; // the SKIP below only applies before the first present
using Clock = std::chrono::steady_clock;
auto lastSecondaryFrame = Clock::now();
// Bound (platform): an occluded secondary window never presents (#878 pattern). Overflow skips its captures and prints one SKIP.
constexpr auto noFrameLimit = std::chrono::seconds(3);
// Bound (platform): a stalled window loop must not hang the run. Overflow fails the run with one check.
constexpr auto runLimit = std::chrono::seconds(30);
class SecondaryFixture : public App {
    int frame_=0;
    EventListener after_;
public:
    void setup() override {
        imguiSetup(); fixedContext();
        after_=events().afterFrame.listen([this] {
            lastSecondaryFrame=Clock::now(); secondaryPresented=true;
            if(secondaryComplete) return; // Main may have skipped an unpresented window.
            if(frame_==1 || frame_==2) screenCapture(std::string("imguiScreenDpi2.")+(frame_==1?"pixelPerfectOff":"pixelPerfectOn")+".secondary");
            if(frame_==2) secondaryComplete=true;
        });
    }
    void draw() override {
        clear(0,0,0); setupScreenOrtho();
        if(frame_<2) screenUi(frame_==1);
        ++frame_;
    }
};
#ifndef TC_IMGUI_BASE_FIXTURE
// Drive the actual addon registrations, with defaults registered before setup.
void inputPriorityChecks() {
    int defaultCalls = 0, afterImGuiCalls = 0;
    std::vector<EventListener> defaults;
    auto addDefault = [&](auto& event) {
        defaults.push_back(event.listen([&](auto&) { ++defaultCalls; }));
    };
    addDefault(events().mousePressed); addDefault(events().mouseReleased);
    addDefault(events().mouseMoved); addDefault(events().mouseDragged);
    addDefault(events().mouseScrolled); addDefault(events().keyPressed);
    addDefault(events().keyReleased);
    ImGuiManager manager;
    manager.setup(); fixedContext();
    auto& io = ImGui::GetIO();
    io.WantCaptureMouse = true; io.WantCaptureKeyboard = true;
    auto capture = [&](auto& event, auto payload) {
        event.notify(payload);
        check("actual ImGui blocks earlier default listener", payload.consumed && defaultCalls == 0);
    };
    capture(events().mousePressed, MouseEventArgs{});
    capture(events().mouseDragged, MouseDragEventArgs{});
    capture(events().mouseReleased, MouseEventArgs{});
    capture(events().mouseMoved, MouseMoveEventArgs{});
    capture(events().mouseScrolled, ScrollEventArgs{});
    capture(events().keyPressed, KeyEventArgs{});
    capture(events().keyReleased, KeyEventArgs{});
    // -15 lies after ImGui's -20 but before the old gizmo priority (-10).
    // A gizmo grab must skip this observer even while ImGui wants the mouse.
    auto observer = events().mousePressed.listen([&](auto&) { ++afterImGuiCalls; }, -15);
    auto& inspector = tcx::NodeInspector::instance();
    inspector.setEnabled(true);
    inspector.enableGizmoInput();
    auto selected = std::make_shared<Node>();
    selected->setPos(100,100);
    setSelectedNode(selected.get());
    MouseEventArgs press{}; press.pos = {130,100};
    events().mousePressed.notify(press);
    check("actual gizmo captures before ImGui and earlier default", press.consumed && inspector.isGizmoDragging() && defaultCalls == 0 && afterImGuiCalls == 0);
    MouseDragEventArgs drag{}; drag.pos = {140,100};
    events().mouseDragged.notify(drag);
    check("actual gizmo drag keeps capture", drag.consumed && defaultCalls == 0);
    MouseEventArgs release{};
    events().mouseReleased.notify(release);
    check("actual gizmo release keeps capture", release.consumed && !inspector.isGizmoDragging() && defaultCalls == 0);
    setSelectedNode(nullptr);
    inspector.setEnabled(false);
    io.WantCaptureMouse = false; io.WantCaptureKeyboard = false;
    manager.shutdown();
}
#endif

#ifndef TC_IMGUI_BASE_FIXTURE
// #947 PR 4: use real secondary-window delivery and live mandatory hooks.
std::shared_ptr<Window> exitWindow;
bool secondaryExitCompleted = false;
bool secondaryExitTicked = false, secondaryExitSkippedNoTick = false;
EventListener secondaryExitUpdate, secondaryExitDraw;
class SecondaryExitApp : public App {
    ImGuiManager offscreen_;
    EventListener ordinary_, log_;
    int exits_ = 0, shutdowns_ = 0, finalized_ = 0, ordinaryCalls_ = 0;
    bool exitFinished_ = false;
#ifndef __EMSCRIPTEN__
    ScreenRecorder recorder_;
    Fbo fbo_;
    fs::path file_;
#endif
public:
    void setup() override {
        inputPriorityChecks(); // actual addon consumers on this secondary stream
        imguiSetup(); fixedContext(); offscreen_.setupOffscreen(); fixedContext();
        tcx::NodeInspector::setToggleKey(KEY_F1);
        ordinary_ = events().exit.listen([this] { ++ordinaryCalls_; }, EventPriority::AfterApp);
        log_ = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (secondaryExitSkippedNoTick) return;
            if (e.message == "ImGui shutdown") {
                ++shutdowns_; check("secondary ImGui shutdown follows App exit", exits_ == 1 && exitFinished_);
            }
            if (e.message.starts_with("[VideoWriter] saved ")) {
                ++finalized_; check("secondary recorder finalization follows App exit", exits_ == 1 && exitFinished_);
            }
        });
#ifndef __EMSCRIPTEN__
        // Media Foundation H.264 rejects frames of 32x32 or smaller (#886).
        fbo_.allocate(64, 64); fbo_.begin(1, 0, 0, 1); fbo_.end();
        file_ = fs::temp_directory_path() / ("tc-secondary-exit-" + std::to_string(Clock::now().time_since_epoch().count()) + ".mp4");
        check("secondary recorder starts", bool(recorder_.start(fbo_, file_)));
#endif
    }
    void update() override {
#ifndef __EMSCRIPTEN__
        if (recorder_.getFrameCount() > 0) exitWindow->close();
#else
        exitWindow->close();
#endif
    }
    void exit() override {
        // A never-ticked window may reach teardown without running setup().
        if (secondaryExitSkippedNoTick) return;
        ++exits_;
        auto* manager = ImGuiManager::findForCurrentWindow();
        check("secondary window ImGui available in App exit", manager && manager->isInitialized());
        check("secondary offscreen ImGui available in App exit", offscreen_.isInitialized());
        check("secondary inspector input available in App exit", events().keyPressed.listenerCount() == 3); // App, ImGui, inspector
#ifndef __EMSCRIPTEN__
        check("secondary recorder available in App exit", recorder_.isRecording());
#endif
        events().exit.clear();
        exitFinished_ = true;
    }
    void cleanup() override {
        if (secondaryExitSkippedNoTick) return;
        check("secondary App exits once", exits_ == 1 && exitFinished_);
        check("secondary ordinary hook removed by clear", ordinaryCalls_ == 0);
        check("both secondary ImGui hooks survive clear", shutdowns_ == 2 && !offscreen_.isInitialized());
        check("secondary inspector hook survives clear", events().keyPressed.listenerCount() == 0);
        check("secondary mandatory hooks disconnect", events().exit.listenerCount() == 0);
#ifndef __EMSCRIPTEN__
        check("secondary recorder hook survives clear", !recorder_.isRecording() && finalized_ == 1);
        VideoPlayer player; check("secondary recording finalized and loads", bool(player.load(file_))); player.close();
        recorder_.stop(); std::error_code ignored; fs::remove(file_, ignored);
#endif
        secondaryExitCompleted = true;
    }
};
class SecondaryExitHarness : public App {
    int frames_ = 0;
public:
    void setup() override {
        WindowSettings s; s.setSize(128, 128);
        exitWindow = createWindow(s); check("secondary teardown display created", bool(exitWindow));
        if (exitWindow) {
            secondaryExitUpdate = exitWindow->events().update.listen([] { secondaryExitTicked = true; }, -1000);
            secondaryExitDraw = exitWindow->events().draw.listen([] { secondaryExitTicked = true; }, -1000);
            exitWindow->setApp(std::make_shared<SecondaryExitApp>());
        }
        else exitApp();
    }
    void update() override {
        if (secondaryExitCompleted) { exitWindow.reset(); exitApp(); }
        if (++frames_ == 600) {
            if (!secondaryExitTicked) {
                std::puts("SKIP: tests-renderTarget --secondary-exit: the secondary window never reached its first tick (occluded or not presented on this host); the teardown checks require it");
                secondaryExitSkippedNoTick = true;
            } else check("secondary teardown fixture completed within frame guard", false);
            exitApp();
        }
    }
};
#endif

class Fixture : public App {
    int frame_=0;
    int exitCalls_=0, imguiShutdowns_=0, recorderFinalizations_=0, userExitCalls_=0;
    bool appExitFinished_=false;
    EventListener render_,after_,log_,userExit_;
    Clock::time_point started_=Clock::now();
    std::shared_ptr<Window> secondary_;
#ifndef TC_IMGUI_BASE_FIXTURE
    ImGuiManager recovery_;
    Fbo recoveryFbo_;
#ifndef __EMSCRIPTEN__
    Fbo recordingFbo_;
    fs::path recordingFile_;
#endif
    simgui_tc_context pendingAcrossCommit_ = {};
    ImGuiContext* pendingImGui_ = nullptr;
    bool pendingReadback_=false, pendingChecked_=false;
    float pendingDpi_ = 1; // DPI the pending frame was built at (frame 0, before screenUi())
#endif
public:
    void exit() override {
        ++exitCalls_;
        auto* manager = ImGuiManager::findForCurrentWindow();
        check("ImGui remains initialized inside App::exit", manager && manager->isInitialized());
#ifndef TC_IMGUI_BASE_FIXTURE
        if (!screenOnly) check("offscreen ImGui remains initialized inside App::exit", recovery_.isInitialized());
        // Keep the actual inspector hook live, with observable teardown state.
        tcx::NodeInspector::attach();
        check("NodeInspector remains attached inside App::exit", tcx::NodeInspector::isAttached());
#ifndef __EMSCRIPTEN__
        if (!screenOnly) check("recorder remains live inside App::exit", isRecording() && recordingFrameCount()>0);
#endif
#endif
        const auto subscriptionsBeforeClear=events().exit.listenerCount();
        events().exit.clear();
        check("App exit clear removes ordinary subscriptions", events().exit.listenerCount()+1==subscriptionsBeforeClear);
#ifndef TC_IMGUI_BASE_FIXTURE
        size_t mandatoryHooks=screenOnly?2:3; // window/offscreen ImGui + inspector
#ifndef __EMSCRIPTEN__
        if (!screenOnly) ++mandatoryHooks; // recorder
#endif
        check("clear preserves each mandatory teardown subscription", events().exit.listenerCount()==mandatoryHooks+1);
#endif
        appExitFinished_=true;
    }
    void cleanup() override {
        check("App exit ran exactly once", exitCalls_==1);
        check("cleared ordinary exit hook did not run", userExitCalls_==0);
        auto* manager = ImGuiManager::findForCurrentWindow();
        check("ImGui shuts down before App::cleanup", !manager || !manager->isInitialized());
#ifndef TC_IMGUI_BASE_FIXTURE
        check("offscreen ImGui shuts down before App::cleanup", !recovery_.isInitialized());
        check("each ImGui teardown ran once after App exit clear", imguiShutdowns_==(screenOnly?1:2));
        check("NodeInspector teardown ran after App exit clear", !tcx::NodeInspector::isAttached());
        // The App binding is the only remaining exit subscription: the
        // mandatory hooks disconnected themselves during this notification.
        check("each surviving teardown hook disconnected itself", events().exit.listenerCount()==1);
#ifndef __EMSCRIPTEN__
        if (!screenOnly) {
            check("recorder finalized once after App exit clear", !isRecording() && recorderFinalizations_==1);
            check("finalized recording is nonempty", fs::exists(recordingFile_) && fs::file_size(recordingFile_)>0);
            if (!isRecording()) {
                VideoPlayer player;
                check("finalized recording loads", bool(player.load(recordingFile_)));
            }
            // Only after the verdict: avoid leaving a live encoder on failure.
            stopRecording();
            std::error_code ignored;
            fs::remove(recordingFile_,ignored);
        }
#endif
#endif
    }
    void setup() override {
#ifndef TC_IMGUI_BASE_FIXTURE
        inputPriorityChecks();
#endif
        sg_enable_stats();
        log_=getLogger().onLog.listen([this](LogEventArgs& args){
            if(args.message=="ImGui shutdown" && exitCalls_ && internal::currentWindowContext().isMain) {
                ++imguiShutdowns_;
                check("ImGui teardown follows App::exit", appExitFinished_);
            }
            if(args.message.starts_with("[VideoWriter] saved ") && exitCalls_) {
                ++recorderFinalizations_;
                check("recorder finalization follows App::exit", appExitFinished_);
            }
            if(args.level==LogLevel::Error) {
                ++errors;
                if(args.message.find("[sg]")!=std::string::npos) ++validationErrors;
            }
        });
        imguiSetup(); fixedContext();
        userExit_=events().exit.listen([this] { ++userExitCalls_; }, EventPriority::AfterApp);
        manifest["requestedScreenSamples"]=screenSamples;
        manifest["screenSamples"]=internal::currentWindowContext().swapchainTarget.sampleCount;
        manifest["version"]=getVersion();
        manifest["dpiSource"]=nativeDpi2?"native window DPI from Xft.dpi":"simulated window metrics";
        manifest["backend"]=getBackendName(); manifest["baseCapability"]=screenOnly?"screen-only; target-bound UI unavailable on base":"target-bound UI";
        // Restore the simulated main-window identity before present() returns.
        render_=events().onRender.listen([] {
            internal::currentWindowContext().isMain=true;
        },2000);
        after_=events().afterFrame.listen([this] {
#ifndef TC_IMGUI_BASE_FIXTURE
            if(pendingReadback_) {
                Pixels image; const bool captured=bool(grabScreen(image));
                check("pending frame after commit readback",captured);
                if(captured) pixel(image,110,int(20*pendingDpi_),{0,0,255,255},"original pending frame renders after commit",false);
                pendingReadback_=false; pendingChecked_=true;
                return;
            }
#endif
            if(frame_==1 && !screenOnly) {
                Pixels image; const bool captured=bool(grabScreen(image));
                check("offscreen-to-screen capture",captured);
                if(!captured) return;
                pixel(image,20,20,{255,0,0,255},"context A on screen",false);
                pixel(image,178,20,{0,255,0,255},"context B on screen",false);
                pixel(image,100,20,{0,0,255,255},"no foreign UI leakage",false);
                pixel(image,460,20,{255,255,0,255},"reserved window frame survives refusal",false);
                pixel(image,324,20,{64,128,63,255},"transparent parent chain on screen",false);
                capture(image,"imguiTwoContextsTwoFbos.screen");
            }
            if(frame_==2 || frame_==3)
                screenCapture(std::string("imguiScreenDpi2.")+(frame_==2?"pixelPerfectOff":"pixelPerfectOn")+".main");
        });
    }
    void update() override {
        const auto now=Clock::now();
        if(now-started_>runLimit) {
            check("all fixtures completed within 30 seconds (wall-clock timeout)",false);
            exitApp(1);
            return;
        }
        // Poll from the main window even if the secondary never reaches setup().
        // Once it has presented, both captures must finish (or the run times out).
        if(secondary_ && !secondaryComplete && !secondaryPresented && now-lastSecondaryFrame>=noFrameLimit) {
            std::puts("SKIP: imguiScreenDpi2.pixelPerfectOff.secondary, imguiScreenDpi2.pixelPerfectOn.secondary: window renders no frames (occluded or hidden)");
            secondaryComplete=true;
        }
    }
    void draw() override {
#ifndef TC_IMGUI_BASE_FIXTURE
        if(frame_==1 && !screenOnly && !pendingChecked_) {
            const auto generation=internal::currentTarget().resources().generation;
            const auto budget=internal::sglBudget();
            internal::resizeSgl(budget.maxVertices+32,budget.maxCommands+16);
            check("actual sgl rebuild replaces generation",internal::currentTarget().resources().generation!=generation);
            // Present the intermediate blue marker before the ordinary screen
            // scene clears it. Keep frame_ at 1 until afterFrame has read it;
            // screenUi() is still called exactly once for each original scene.
            clear(0,0,0); setupScreenOrtho(); setColor(1);
            recoveryFbo_.allocate(48,40,4,TextureFormat::RGBA16F);
            recoveryFbo_.begin(0,0,0,0); {auto ui=recovery_.scoped(); check("same context recovers after normal commit",bool(ui)); if(ui) marker(IM_COL32(0,255,0,255));} recoveryFbo_.end();
            pixel(read(recoveryFbo_),20,20,{0,255,0,255},"next frame marker");
            const auto previous=simgui_tc_get_context();
            auto* previousImGui=ImGui::GetCurrentContext();
            const int pendingErrors=errors;
            simgui_frame_desc_t unusedDesc={};
            check("pending frame survives commit and refuses overwrite",!tcx::imgui::detail::beginTargetFrame(pendingAcrossCommit_,unusedDesc));
            check("pending overwrite emits one Error",errors==pendingErrors+1);
            tcx::imgui::detail::renderTargetFrame(pendingAcrossCommit_);
            manifest["checks"]["pendingAcrossCommit"]["tcErrors"]=errors-pendingErrors;
            forgetContext(pendingImGui_); pendingImGui_=nullptr;
            tcx::imgui::detail::destroyTargetContext(pendingAcrossCommit_); pendingAcrossCommit_={};
            simgui_tc_set_context(previous); ImGui::SetCurrentContext(previousImGui);
            pendingReadback_=true;
            return; // Framework present() commits; afterFrame reads before any later draw.
        }
#endif
        clear(0,0,0); setupScreenOrtho(); setColor(1);
        if(frame_==0 && !screenOnly) {
            internal::pixelPerfectMode()=true; setupScreenOrtho();
#ifndef TC_IMGUI_BASE_FIXTURE
            for(int samples:{1,4}) for(auto format:{TextureFormat::RGBA8,TextureFormat::RGBA16F}) for(bool unfilterable:{false,true}) transparent(samples,format,unfilterable);
            refusalAndContexts(); targetAssociation();
#endif
        }
        if(frame_==1 || frame_==2) screenUi(frame_==2);
        if(frame_==3 && !internal::supportsSecondaryWindows()) {
            // Single-window platforms (Pi, web, mobile): no secondary fixtures to wait for.
            std::puts("SKIP: secondary windows unsupported on this platform");
            secondaryComplete=true;
        } else if(frame_==3) {
            WindowSettings secondarySettings; secondarySettings.setSize(nativeDpi2?256:512,nativeDpi2?192:384); secondarySettings.setHighDpi(nativeDpi2); secondarySettings.sampleCount=screenSamples;
            lastSecondaryFrame=Clock::now();
            secondary_=createWindow(secondarySettings); check("real secondary window created",bool(secondary_));
            if(secondary_) secondary_->setApp(std::make_shared<SecondaryFixture>());
            else secondaryComplete=true; // Creation already failed; do not wait for a tick.
        }
#ifndef TC_IMGUI_BASE_FIXTURE
        if(frame_==0 && !screenOnly) {
            recovery_.setupOffscreen(); fixedContext(); recoveryFbo_.allocate(32,32);
            recoveryFbo_.begin(0,0,0,0); {auto ui=recovery_.scoped(); if(ui) marker(IM_COL32(255,0,0,255));} recoveryFbo_.end();
#ifndef __EMSCRIPTEN__
            // Media Foundation H.264 rejects frames of 32x32 or smaller (#886).
            recordingFbo_.allocate(64,64);
            recordingFbo_.begin(1,0,0,1); recordingFbo_.end();
            recordingFile_=fs::temp_directory_path()/("tc-imgui-exit-clear-"+
                std::to_string(Clock::now().time_since_epoch().count())+".mp4");
            check("live recorder starts for exit-clear regression", bool(startRecording(recordingFbo_,recordingFile_)));
#endif
        }
#endif
#ifndef TC_IMGUI_BASE_FIXTURE
        if(frame_==0 && !screenOnly) {
            // No onRender listener for this backend context: the framework's
            // normal present commits before its owner dispatches the pending UI.
            const auto previous=simgui_tc_get_context();
            auto* previousImGui=ImGui::GetCurrentContext();
            simgui_desc_t desc={}; desc.logger.func=internal::sokolLog;
            pendingAcrossCommit_=simgui_tc_make_context(&desc);
            simgui_tc_set_context(pendingAcrossCommit_); fixedContext();
            pendingImGui_=ImGui::GetCurrentContext();
            simgui_frame_desc_t frameDesc={}; frameDesc.width=getFramebufferWidth(); frameDesc.height=getFramebufferHeight();
            pendingDpi_=getDpiScale();
            frameDesc.dpi_scale=pendingDpi_; frameDesc.delta_time=1.f/60;
            check("pending context reserves before normal present",tcx::imgui::detail::beginTargetFrame(pendingAcrossCommit_,frameDesc));
            marker(IM_COL32(0,0,255,255),100/pendingDpi_);
            simgui_tc_set_context(previous); ImGui::SetCurrentContext(previousImGui);
        }
#endif
        if(frame_>=5 && secondaryComplete) {
            if(secondary_) secondary_->close();
            check("zero sokol validation errors",validationErrors==0);
            check("only expected refusal/association Errors",errors==(screenOnly?0:7));
            manifest["expectedTcErrors"]=screenOnly?0:7;
            manifest["tcErrors"]=errors; manifest["sokolValidationErrors"]=validationErrors;
            if(!output.empty()) { std::ofstream file(output/"samples.json"); file<<manifest.dump(2)<<'\n'; }
            internal::currentWindowContext().isMain=true;
            completed=true; exitApp();
        }
        ++frame_;
    }
};
}
int main(int argc,char** argv) {
    if(argc==1) {std::puts("SKIP: use --self-check, --record dir or --compare dir under a display"); return 0;}
#ifndef TC_IMGUI_BASE_FIXTURE
    if (std::string(argv[1]) == "--secondary-exit") {
        if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: secondary windows unavailable"); return 0; }
        WindowSettings settings; settings.setSize(128, 128);
        return tcCoreTest::runDisplayTest<SecondaryExitHarness>(settings, failures, [] {
            if (!secondaryExitSkippedNoTick) check("secondary teardown fixture completed", secondaryExitCompleted);
            exitWindow.reset();
        });
    }
#endif
    for(int i=1;i<argc;++i) {
        if(std::string(argv[i])=="--screen-only") screenOnly=true;
        if(std::string(argv[i])=="--native-dpi2") nativeDpi2=true;
        if(std::string(argv[i])=="--msaa4") screenSamples=4;
        if(std::string(argv[i])=="--record" && i+1<argc) output=fs::absolute(argv[++i]);
        if(std::string(argv[i])=="--compare" && i+1<argc) {baseline=fs::absolute(argv[++i]); output=baseline.string()+".compare";}
    }
    WindowSettings settings; settings.setSize(nativeDpi2?256:512,nativeDpi2?192:384); settings.setHighDpi(nativeDpi2); settings.sampleCount=screenSamples;
    return tcCoreTest::runDisplayTest<Fixture>(settings,failures,[]{check("all fixtures completed",completed);});
}
