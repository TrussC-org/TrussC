// Review round 1 (adversarial): real backend, native-call recording, and the
// same deferred tool envelopes the MCP server uses. No HTTP server is needed.
#include <tcxImGui.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <future>
#include <cstdlib>
using namespace tc;
using namespace tcx::imgui;
namespace d = tcx::imgui::detail;
namespace md = tc::mcp::internal;
namespace {
ImGuiManager globalOwnedManager; // constructed before the function-static hook registries
int failures = 0, checks = 0, cursorCalls = 0, keyboardCalls = 0, keyboardQueries = 0;
sapp_mouse_cursor cursor = SAPP_MOUSECURSOR_ARROW;
bool keyboard = false;
void check(const char* name, bool ok) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
std::future<md::ReplyThunk> request(const char* label = "idle value", const std::string& text = "true") {
    nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
        {"params", {{"name", "tcx_imgui_input"}, {"arguments", {{"label", label}, {"text", text}}}}}};
    auto& ds = md::deferralState();
    ds.hasEnvelope = false; ds.target = nullptr; ds.owner = nullptr;
    auto result = md::Server::instance().processMessage(req.dump());
    md::DeferredResponse response;
    response.id = std::move(ds.id);
    response.response = std::make_shared<std::promise<md::ReplyThunk>>();
    auto future = response.response->get_future();
    if (!ds.hasEnvelope) {
        response.response->set_value([result] { return result; });
        return future;
    }
    response.makeEnvelope = std::move(ds.envelope);
    response.target = ds.target;
    response.deadline = std::chrono::steady_clock::now() + md::kTargetedDeferralTimeout;
    md::deferredResponses().push_back(std::move(response));
    ds.hasEnvelope = false; ds.target = nullptr;
    return future;
}
bool ready(std::future<md::ReplyThunk>& reply) {
    return reply.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
bool replyHas(std::future<md::ReplyThunk>& reply, const char* text) {
    return ready(reply) && reply.get()().find(text) != std::string::npos;
}
void checkContextBound(ImGuiContext* ctx, const char* label) {
    auto& cs = d::contexts().at(ctx);
    const auto rejected = d::rejectedPendingValues;
    const auto localRejected = cs.rejectedPendingValues;
    std::vector<std::future<md::ReplyThunk>> replies;
    for (size_t i = 0; i < d::kMaxPendingValuesPerContext; ++i) replies.push_back(request(label));
    if (cs.pendingValues.empty()) { check("tool edits enter the context queue", false); return; }
    auto last = cs.pendingValues.back();
    check("N pending tool edits retain their reply promises", cs.pendingValues.size() == d::kMaxPendingValuesPerContext && !ready(replies.back()));
    bool errors = true;
    for (int i = 0; i < 32; ++i) {
        auto extra = request(label);
        if (!ready(extra)) { errors = false; continue; }
        auto reply = nlohmann::json::parse(extra.get()());
        errors &= reply.value("jsonrpc", "") == "2.0" && reply.at("id") == 1 &&
            reply.contains("error") && reply["error"].value("code", 0) == -32000 &&
            reply["error"].value("message", "").find("queue full") != std::string::npos;
    }
    check("N+1 and repeated overflow are JSON-RPC errors, counted without queue growth",
        errors && cs.pendingValues.size() == d::kMaxPendingValuesPerContext &&
        md::deferredResponses().size() == d::kMaxPendingValuesPerContext &&
        cs.rejectedPendingValues == localRejected + 32 && d::rejectedPendingValues == rejected + 32 &&
        last->state == d::PendingValue::State::Queued);
    d::finishPendingValues(cs, ctx->FrameCount, true);
    bool settled = true;
    for (auto& reply : replies) settled &= ready(reply);
    check("bounded edits settle and release all deferred responses", settled && cs.pendingValues.empty() && md::deferredResponses().empty());
    auto next = request(label);
    check("settlement releases queue capacity", !ready(next) && cs.pendingValues.size() == 1);
    d::finishPendingValues(cs, ctx->FrameCount, true);
    check("newly admitted edit settles", ready(next));
}

// This process deliberately skips the framework exit event and all automatic
// locals. The global manager must outlive both populated hook registries safely.
[[noreturn]] void staticExitRegression() {
    enableCollection();
    globalOwnedManager.setupOffscreen();
    Fbo fbo; fbo.allocate(256,256);
    bool value = false;
    ImGuiContext* ctx = nullptr;
    ImVec2 center;
    auto draw = [&] {
        fbo.begin(0,0,0,0);
        {
            auto scope = globalOwnedManager.scoped();
            check("global offscreen begin accepted", bool(scope));
            ctx = ImGui::GetCurrentContext();
            ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({256,256});
            ImGui::Begin("global interaction");
            ImGui::Checkbox("touched checkbox", &value);
            center = ImGui::GetItemRectMin(); center.x += 5; center.y += 5;
            ImGui::End();
        }
        fbo.end(); internal::endGpuFrame();
    };
    draw(); draw();
    ImGui::SetCurrentContext(ctx);
    ImGui::GetIO().AddMousePosEvent(center.x, center.y);
    ImGui::GetIO().AddMouseButtonEvent(0, true); draw();
    ImGui::GetIO().AddMouseButtonEvent(0, false); draw();
    check("global owned manager records a real interaction before std::exit",
        value && !getTouched().empty() && getTouched().back().ctx == ctx);
    std::printf("static-exit regression: %d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    std::exit(failures ? 1 : 0);
}

void regressions() {
    imguiSetup(); enableCollection(); mcp::setToolLevel(mcp::ToolLevel::Control);
    const auto windowBackend = simgui_tc_get_context();
    Fbo fbo; fbo.allocate(128,128);
    ImGuiManager offscreen; offscreen.setupOffscreen();
    ImGuiContext* ctx = nullptr;
    bool value = false;
    char textValue[32] = {};
    auto draw = [&] {
        fbo.begin(0,0,0,0);
        {
            auto scope = offscreen.scoped();
            check("offscreen begin accepted", bool(scope));
            ctx = ImGui::GetCurrentContext();
            ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({128,128});
            ImGui::Begin("probe"); ImGui::Checkbox("idle value", &value);
            ImGui::InputText("idle text", textValue, sizeof(textValue)); ImGui::End();
        }
        fbo.end(); internal::endGpuFrame();
    };
    // Window text input owns these states before the offscreen frame.
    cursor = SAPP_MOUSECURSOR_IBEAM; keyboard = true;
    cursorCalls = keyboardCalls = keyboardQueries = 0;
    draw();
    check("offscreen begin makes zero native cursor/keyboard calls",
        cursorCalls == 0 && keyboardCalls == 0 && keyboardQueries == 0 && keyboard && cursor == SAPP_MOUSECURSOR_IBEAM);
    // Also test WantTextInput=true: suppress both show and hide directions.
    ImGui::SetCurrentContext(ctx); ImGui::GetIO().WantTextInput = true;
    keyboard = false; draw();
    check("offscreen text input does not show the native keyboard", keyboardCalls == 0 && keyboardQueries == 0 && !keyboard);

    // Ordinary window frames still show/hide the keyboard and set the cursor.
    for (bool wantsText : {true, false}) {
        ImGuiManager window; window.setup();
        ImGui::GetIO().WantTextInput = wantsText;
        keyboard = !wantsText; cursorCalls = keyboardCalls = keyboardQueries = 0;
        fbo.begin(0,0,0,0);
        { auto scope = window.scoped(); check("window manager Fbo scope accepted", bool(scope)); }
        fbo.end(); internal::endGpuFrame();
        check("window manager Fbo frame suppresses native controls", cursorCalls == 0 && keyboardCalls == 0 && keyboardQueries == 0 && keyboard == !wantsText);
        ImGui::GetIO().WantTextInput = wantsText;
        ImGui::SetMouseCursor(wantsText ? ImGuiMouseCursor_TextInput : ImGuiMouseCursor_ResizeEW);
        keyboard = !wantsText; cursorCalls = keyboardCalls = keyboardQueries = 0;
        check("window begin accepted", window.begin());
        check("window begin controls native keyboard and cursor", keyboard == wantsText && keyboardCalls == 1 && keyboardQueries > 0 && cursorCalls == 2 &&
            cursor == (wantsText ? SAPP_MOUSECURSOR_IBEAM : SAPP_MOUSECURSOR_RESIZE_EW));
        auto* windowCtx = ImGui::GetCurrentContext();
        bool windowValue = false;
        ImGui::Begin("window probe"); ImGui::Checkbox("window value", &windowValue); ImGui::End();
        window.end();
        checkContextBound(windowCtx, "window value");
        window.shutdown(); internal::endGpuFrame();
    }
    auto& pending = d::contexts().at(ctx).pendingValues;
    auto edit = request();
    draw();
    check("offscreen edit writes the checkbox and waits for verification", value && pending.size() == 1 && pending.front()->state == d::PendingValue::State::Verify && !ready(edit));
    auto applied = pending.front();
    // No owning-window onRender or UI redraw. Only main afterFrame advances.
    d::clockSkew += applied->checkDeadline - d::now() - std::chrono::seconds(1);
    events().afterFrame.notify();
    check("edit remains pending before its check deadline", !ready(edit));
    d::clockSkew += applied->checkDeadline - d::now();
    events().afterFrame.notify();
    check("idle applied edit completes at its check deadline", replyHas(edit, "ok") && pending.empty());

    auto expired = request(); auto queued = pending.back();
    d::clockSkew += queued->deadline - d::now();
    events().afterFrame.notify();
    check("idle unrendered edit expires at its write deadline", queued->state == d::PendingValue::State::Expired && replyHas(expired, "too late") && pending.empty());

    checkContextBound(ctx, "idle value");

    std::vector<std::future<md::ReplyThunk>> replies;
    for (int i=0; i<200; ++i) replies.push_back(request());
    auto last = pending.back();
    events().afterFrame.notify();
    int superseded = 0;
    for (int i=0; i<199; ++i) superseded += replyHas(replies[i], "Superseded");
    check("idle superseded edits are reclaimed without a redraw", superseded == 199 && pending.size() == 1);
    d::clockSkew += last->deadline - d::now(); events().afterFrame.notify();
    check("200 queued tool requests leave no records past their deadlines", replyHas(replies.back(), "too late") && pending.empty() && md::deferredResponses().empty());

    // Distinct widget IDs also expire; cleanup does not rely on supersession.
    int done = 0;
    for (size_t i=0; i<d::kMaxPendingValuesPerContext; ++i)
        d::queueValue(ctx, ImGuiID(i+1), queued->kind, queued->dataType, queued->components,
            queued->bytes, d::kPendingValueLifetime, d::kValueCheckDeadline, [&] { ++done; });
    const auto distinctRejected = d::rejectedPendingValues;
    auto extraDistinct = d::queueValue(ctx, ImGuiID(d::kMaxPendingValuesPerContext+1), queued->kind,
        queued->dataType, queued->components, queued->bytes,
        d::kPendingValueLifetime, d::kValueCheckDeadline, [&] { ++done; });
    check("N+1 distinct pending edits are bounded and rejection is counted",
        !extraDistinct && pending.size() == d::kMaxPendingValuesPerContext &&
        d::rejectedPendingValues == distinctRejected + 1);
    d::clockSkew += pending.back()->deadline - d::now(); events().afterFrame.notify();
    check("N distinct idle requests expire and each callback runs once",
        pending.empty() && done == int(d::kMaxPendingValuesPerContext));
    events().afterFrame.notify(); check("deadline callbacks run only once", done == int(d::kMaxPendingValuesPerContext));

    // Fill several independent contexts to test the aggregate bound separately.
    std::vector<ImGuiContext*> extraContexts;
    bool accepted = true;
    for (size_t c = 0; c < d::kMaxPendingValuesTotal / d::kMaxPendingValuesPerContext; ++c) {
        auto* extra = ImGui::CreateContext();
        extraContexts.push_back(extra);
        for (size_t i = 0; i < d::kMaxPendingValuesPerContext; ++i)
            accepted &= bool(d::queueValue(extra, ImGuiID(i+1), queued->kind,
                queued->dataType, queued->components, queued->bytes,
                d::kPendingValueLifetime, d::kValueCheckDeadline, nullptr));
    }
    check("aggregate capacity accepts N edits across contexts", accepted);
    const auto rejected = d::rejectedPendingValues;
    auto overflow = request();
    check("total N+1 is rejected and counted even with room in its own context",
        replyHas(overflow, "queue full") && pending.empty() &&
        d::pendingValueCount() == d::kMaxPendingValuesTotal && d::rejectedPendingValues == rejected + 1);
    for (auto* extra : extraContexts) { forgetContext(extra); ImGui::DestroyContext(extra); }
    check("destroying contexts releases aggregate capacity", d::pendingValueCount() == 0);

    // Clear the previous completed-frame marker, then skip one owning frame.
    events().onRender.notify();
    auto notDrawn = request(); events().onRender.notify();
    check("idle owning-window frame settles an unrendered edit", replyHas(notDrawn, "not drawn") && pending.empty());
    // The text branch goes through the real Server handler, without any UI redraw.
    ctx->InputEventsQueue.clear();
    auto& cs = d::contexts().at(ctx);
    const auto rejectedText = d::rejectedPendingValues;
    const auto localRejectedText = cs.rejectedPendingValues;
    size_t acceptedText = 0, refusedText = 0;
    bool envelopes = true;
    for (int i = 0; i < 1000; ++i) {
        auto reply = request("idle text");
        if (!ready(reply)) { envelopes = false; continue; }
        auto result = nlohmann::json::parse(reply.get()());
        if (result.contains("error")) {
            ++refusedText;
            envelopes &= result["error"].value("code", 0) == -32000 &&
                result["error"].value("message", "").find("queue full") != std::string::npos;
        } else ++acceptedText;
    }
    check("1000 idle text requests admit only the context edit budget",
        envelopes && acceptedText == d::kMaxPendingValuesPerContext && refusedText == 1000 - acceptedText &&
        cs.pendingText.size() == acceptedText && d::pendingValueCount() == acceptedText);
    check("idle text overflow is counted locally and globally and bounds events and bytes",
        d::rejectedPendingValues == rejectedText + refusedText &&
        cs.rejectedPendingValues == localRejectedText + refusedText &&
        size_t(ctx->InputEventsQueue.Size) <= d::pendingEventCount(cs) &&
        d::pendingEventCount(cs) <= d::kMaxPendingEventsPerContext &&
        d::pendingByteCount(cs) <= d::kMaxPendingBytesPerContext);
    auto mixed = request();
    check("text and fixed-size edits share the context budget", replyHas(mixed, "queue full"));
    const auto deadline = cs.pendingText.back().deadline;
    d::clockSkew += deadline - d::now() - std::chrono::seconds(1);
    events().afterFrame.notify();
    check("idle text remains queued before its deadline", cs.pendingText.size() == acceptedText && ctx->InputEventsQueue.Size > 0);
    // Preserve a native event which does not belong to a tool injection.
    ImGui::SetCurrentContext(ctx); ImGui::GetIO().AddFocusEvent(false);
    const auto nativeId = ctx->InputEventsNextEventId - 1;
    d::clockSkew += deadline - d::now(); events().afterFrame.notify();
    check("idle text events and reservations are reclaimed at the deadline, preserving native input",
        cs.pendingText.empty() && d::pendingValueCount() == 0 &&
        ctx->InputEventsQueue.Size == 1 && ctx->InputEventsQueue[0].EventId == nativeId &&
        d::pendingByteCount(cs) == 0);
    ctx->InputEventsQueue.clear();
    auto renewed = request("idle text");
    check("deadline cleanup releases text capacity", replyHas(renewed, "ok") && cs.pendingText.size() == 1);
    events().onRender.notify();
    check("idle owning-window settlement also reclaims text", cs.pendingText.empty() && ctx->InputEventsQueue.empty());

    // Default trickling consumes Ctrl-down and mouse-down in the first frame,
    // leaving their releases in the queue. Exercise both cancellation callers,
    // including the macOS modifier translation, through the real tool handler.
    for (bool mac : {false, true}) {
        ctx->IO.ConfigMacOSXBehaviors = mac;
        for (bool settleAll : {false, true}) {
            auto partial = request("idle text", "hello");
            check("partial text request is accepted", replyHas(partial, "ok"));
            draw();
            check("one NewFrame consumes tool Ctrl and mouse presses only",
                ctx->IO.ConfigInputTrickleEventQueue && ctx->IO.KeyCtrl && ctx->IO.MouseDown[0] &&
                !ctx->InputEventsQueue.empty() && cs.pendingText.size() == 1);
            if (settleAll) {
                // First owning-window frame acknowledges the offscreen draw;
                // the next idle frame calls finishPendingValues(..., true).
                events().onRender.notify(); events().onRender.notify();
            } else {
                d::clockSkew += std::chrono::seconds(3);
                events().afterFrame.notify();
            }
            check(settleAll ? "settleAll retains only the consumed tool presses' releases" :
                                 "expiry retains only the consumed tool presses' releases",
                cs.pendingText.empty() && ctx->InputEventsQueue.Size == 2);
            draw(); draw();
            check("cancelled partial injection releases Ctrl and mouse and drains the queue",
                !ctx->IO.KeyCtrl && !ctx->IO.MouseDown[0] && ctx->InputEventsQueue.empty());

            // A native-held Ctrl/button suppresses the tool's down events.
            // Cancelling must never manufacture releases for those native holds.
            ImGui::SetCurrentContext(ctx);
            auto& io = ImGui::GetIO();
            const auto mod = mac ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(mod, true); io.AddMouseButtonEvent(0, true);
            draw();
            auto nativeHeld = request("idle text", "unused");
            check("text request with native-held inputs is accepted", replyHas(nativeHeld, "ok"));
            if (settleAll) {
                events().onRender.notify(); events().onRender.notify();
            } else {
                d::clockSkew += std::chrono::seconds(3); events().afterFrame.notify();
            }
            draw(); draw();
            check("cancellation preserves native-held Ctrl and mouse",
                ctx->IO.KeyCtrl && ctx->IO.MouseDown[0] && ctx->InputEventsQueue.empty() && cs.pendingText.empty());
            ImGui::SetCurrentContext(ctx);
            io.AddKeyEvent(mod, false); io.AddMouseButtonEvent(0, false);
            draw(); draw();
        }
    }
    ctx->IO.ConfigMacOSXBehaviors = false;

    const auto beforeLarge = d::rejectedPendingValues;
    auto large = request("idle text", std::string(d::kMaxPendingBytesPerContext, 'x'));
    check("one oversized text is rejected before inserting any events",
        replyHas(large, "queue full") && ctx->InputEventsQueue.empty() && cs.pendingText.empty() &&
        d::rejectedPendingValues == beforeLarge + 1);
    auto huge = request("idle text", std::string(d::kMaxPendingBytesPerContext + 1, 'x'));
    check("text above the byte budget is also counted without insertion",
        replyHas(huge, "queue full") && ctx->InputEventsQueue.empty() && d::rejectedPendingValues == beforeLarge + 2);

    // Exhaust total byte/event reservations with room in each context's edit count.
    std::vector<ImGuiContext*> textContexts;
    WidgetInfo textWidget; textWidget.rect = ImRect({0,0}, {20,20});
    bool textAccepted = true;
    const size_t chars = d::kMaxPendingBytesPerContext / sizeof(ImGuiInputEvent) - 11;
    for (int i = 0; i < 4; ++i) {
        auto* extra = ImGui::CreateContext(); textContexts.push_back(extra);
        ImGui::SetCurrentContext(extra);
        textAccepted &= inputText(textWidget, std::string(chars, 'a'));
    }
    ImGui::SetCurrentContext(ctx);
    auto totalText = request("idle text");
    check("total input byte budget rejects text in a context with spare capacity",
        textAccepted && replyHas(totalText, "queue full") && cs.pendingText.empty() && ctx->InputEventsQueue.empty());
    auto totalValue = d::queueValue(ctx, 1, ImGuiTcValueKind_Input, ImGuiDataType_Double, 1,
        std::vector<unsigned char>(sizeof(double)), d::kPendingValueLifetime, d::kValueCheckDeadline, nullptr);
    check("fixed-size edits share the total byte budget", !totalValue && cs.pendingValues.empty());
    for (auto* extra : textContexts) { forgetContext(extra); ImGui::DestroyContext(extra); }
    ImGui::SetCurrentContext(ctx);
    check("context destruction releases input reservations", d::pendingValueCount() == 0);

    auto shutdownText = request("idle text");
    check("text is admitted after aggregate cleanup", replyHas(shutdownText, "ok"));
    // Raw contexts above are selected only for injection. Restore the live
    // window backend/context pair before testing teardown and another manager.
    simgui_tc_set_context(windowBackend);
    auto beforeShutdown = request(); offscreen.shutdown();
    check("shutdown settles remaining edits", ready(beforeShutdown));
    events().afterFrame.notify(); events().onRender.notify();
    d::clockSkew = {};
    ImGuiContext* raiiContext = nullptr;
    {
        ImGuiManager raii; raii.setupOffscreen();
        fbo.begin(0,0,0,0);
        { auto scope = raii.scoped(); raiiContext = ImGui::GetCurrentContext(); }
        fbo.end(); internal::endGpuFrame();
        check("owned RAII manager has a live registry entry", d::contexts().count(raiiContext) == 1);
    }
    check("ordinary owned RAII destruction erases its registry entry", d::contexts().count(raiiContext) == 0);
    imguiShutdown();
}
}
extern "C" void __wrap_sapp_set_mouse_cursor(sapp_mouse_cursor next) { ++cursorCalls; cursor = next; }
extern "C" sapp_mouse_cursor __wrap_sapp_get_mouse_cursor() { ++cursorCalls; return cursor; }
extern "C" bool __wrap_sapp_keyboard_shown() { ++keyboardQueries; return keyboard; }
extern "C" void __wrap_sapp_show_keyboard(bool shown) { ++keyboardCalls; keyboard = shown; }
int main(int argc, char** argv) {
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (!eglInitialize(display,nullptr,nullptr) || !eglBindAPI(EGL_OPENGL_API)) return 2;
    EGLint attrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
    EGLConfig config; EGLint count = 0;
    if (!eglChooseConfig(display,attrs,&config,1,&count) || !count) return 3;
    EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,1,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    EGLContext context = eglCreateContext(display,config,EGL_NO_CONTEXT,ca);
    if (!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context)) return 4;
    sg_desc gd = {}; gd.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    gd.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL; gd.environment.defaults.sample_count = 1;
    gd.logger.func = internal::sokolLog; gd.pipeline_pool_size = 1024; gd.shader_pool_size = 1024; sg_setup(gd);
    sgl_desc_t ld = {}; ld.pipeline_pool_size = 256; ld.logger.func = internal::sokolLog; sgl_setup(ld);
    auto& w = internal::currentWindowContext();
    // Keep framebuffer metrics independent of sapp (no native window exists).
    w.isMain = false; w.fbWidth = w.fbHeight = 256; w.dpiScale = 1; w.updateDeltaTime = 1.0/60.0;
    w.swapchainTarget.resources().context = sgl_default_context();
    w.swapchainTarget.width = w.swapchainTarget.height = 256;
    w.swapchainTarget.colorFormat = SG_PIXELFORMAT_RGBA8;
    if (argc == 2 && std::string(argv[1]) == "--static-exit") staticExitRegression();
    regressions();
    internal::drainPendingGpuDestroys(); sgl_shutdown(); sg_shutdown();
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context); eglTerminate(display);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
