#pragma once

// =============================================================================
// imguiHarness.h - headless Dear ImGui driver for the tcxImGui tests
//
// An ImGui context with no renderer and no window: the font atlas is built on
// the CPU (the backend claims RendererHasTextures and never uploads), frames
// are run by hand, and input goes through ImGuiIO the way a backend would feed
// it. Collection is on, so the hooks in tcImGuiHooks.h fill the frame registry
// and the touched record exactly as in an app.
//
// A test draws its UI in a callback, runs frames with it, and clicks widgets
// by label: the rect comes from the frame registry of the last frame, as the
// MCP tools see it.
// =============================================================================

#include <tcxImGui.h>

#include <cstdio>
#include <functional>
#include <future>
#include <string>

namespace harness {

inline int g_pass = 0, g_fail = 0;

inline void check(const std::string& name, bool ok) {
    std::printf("%-64s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);  // flush each line so CI logs survive a later crash
    ok ? ++g_pass : ++g_fail;
}

inline int summary() {
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

class ImGuiHarness {
public:
    using Ui = std::function<void()>;

    ImGuiHarness() {
        prev_ = ImGui::GetCurrentContext();
        ctx_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024, 768);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;   // no imgui.ini next to the test binary
        io.LogFilename = nullptr;
        // The atlas is built on the CPU as glyphs are needed; nothing uploads it.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        tcx::imgui::enableCollection();
    }

    ~ImGuiHarness() {
        tcx::imgui::forgetContext(ctx_);
        ImGui::DestroyContext(ctx_);
        ImGui::SetCurrentContext(prev_);
    }

    ImGuiHarness(const ImGuiHarness&) = delete;
    ImGuiHarness& operator=(const ImGuiHarness&) = delete;

    ImGuiContext* context() const { return ctx_; }

    // The UI every frame draws until the next setUi()
    void setUi(Ui ui) { ui_ = std::move(ui); }

    // Whether the app runs imgui in a window frame (checked at the start of
    // each frame). Null: every frame. Emulates `if (showGui) { imguiBegin();
    // ... imguiEnd(); }`.
    void setImGuiWhen(std::function<bool()> when) { imguiWhen_ = std::move(when); }

    // Whether the window renders a frame at all (checked at the start of each
    // frame). Null: every frame. A frame where it is false does nothing, as
    // for a window that is throttled very low or stopped rendering; callTool()
    // still runs the main window's afterFrame work after it.
    void setRenderWhen(std::function<bool()> when) { renderWhen_ = std::move(when); }

    // One window frame, as tcxImGui runs it: imguiBegin() ... imguiEnd() +
    // render; or, in a frame where the app runs no imgui, what ImGuiManager's
    // render listener does then.
    void frame() {
        if (renderWhen_ && !renderWhen_()) return;
        ImGui::SetCurrentContext(ctx_);
        if (imguiWhen_ && !imguiWhen_()) {
            tcx::imgui::detail::settleWithoutImGuiFrame(ctx_);
            return;
        }
        ImGui::NewFrame();
        tcx::imgui::beginFrame();
        if (ui_) ui_();
        tcx::imgui::swapFrames();
        ImGui::Render();
    }

    void frames(int n) { for (int i = 0; i < n; i++) frame(); }

    // The widget with this label in the last frame (optionally in one ImGui
    // window), or null if there is none or more than one.
    const tcx::imgui::WidgetInfo* find(const std::string& label, const std::string& window = "") const {
        const tcx::imgui::WidgetInfo* found = nullptr;
        for (auto& w : tcx::imgui::detail::contexts()[ctx_].lastFrame) {
            if (w.label != label) continue;
            if (!window.empty() && w.windowName != window) continue;
            if (found) return nullptr;
            found = &w;
        }
        return found;
    }

    // Mouse input, one event per frame (imgui trickles events across frames
    // anyway; hover needs a frame between the move and the press).
    void mouseMove(ImVec2 p) {
        ImGui::SetCurrentContext(ctx_);
        ImGui::GetIO().AddMousePosEvent(p.x, p.y);
        frame();
    }

    void mouseButton(bool down) {
        ImGui::SetCurrentContext(ctx_);
        ImGui::GetIO().AddMouseButtonEvent(0, down);
        frame();
    }

    // Left click at a point: move, press, release, then one more frame so a
    // popup that the click opened or closed has settled.
    void clickAt(ImVec2 p) {
        mouseMove(p);
        mouseButton(true);
        mouseButton(false);
        frame();
    }

    // Click the centre of a widget found by label. False if it is not drawn.
    bool click(const std::string& label, const std::string& window = "") {
        const tcx::imgui::WidgetInfo* w = find(label, window);
        if (!w) {
            std::printf("  (widget '%s' not found in the last frame)\n", label.c_str());
            return false;
        }
        clickAt(w->rect.GetCenter());
        return true;
    }

private:
    ImGuiContext* ctx_ = nullptr;
    ImGuiContext* prev_ = nullptr;
    Ui ui_;
    std::function<bool()> imguiWhen_;
    std::function<bool()> renderWhen_;
};

// Calls an MCP tool the way tc::mcp::processHttpQueue() does, on this thread.
// A tool that defers its reply is stashed with a promise, as processHttpQueue()
// stashes it, and frames are run by `h` until the reply is produced: after
// each frame the main window's afterFrame work runs (tcxImGui's settling of
// overdue values, then the core's drain), and a deferral aimed at another
// target is answered when its owner drains it (up to `maxFrames`).
// Returns the tool's result (the JSON in its text content). `deferred` tells
// whether it deferred, `frames` how many frames the reply took.
// `beforeFrame(i)` runs before frame i (0 = between the call and the first).
inline nlohmann::json callTool(ImGuiHarness& h, const std::string& name, const nlohmann::json& args,
                               bool* deferred = nullptr, int* frames = nullptr, int maxFrames = 10,
                               const std::function<void(int)>& beforeFrame = nullptr) {
    namespace md = tc::mcp::detail;
    tcx::imgui::registerImGuiTools();   // idempotent
    nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                          {"params", {{"name", name}, {"arguments", args}}}};
    auto& ds = md::deferralState();
    ds.hasEnvelope = false;
    ds.target = nullptr;
    ds.owner = nullptr;
    std::string reply = tc::mcp::Server::instance().processMessage(req.dump());
    if (deferred) *deferred = ds.hasEnvelope;
    if (frames) *frames = 0;
    if (ds.hasEnvelope) {
        md::DeferredResponse d;
        d.id = std::move(ds.id);
        d.response = std::make_shared<std::promise<md::ReplyThunk>>();
        auto future = d.response->get_future();
        d.makeEnvelope = std::move(ds.envelope);
        d.target = ds.target;
        d.deadline = std::chrono::steady_clock::now() + md::kTargetedDeferralTimeout;
        md::deferredResponses().push_back(std::move(d));
        ds.hasEnvelope = false;
        ds.target = nullptr;
        reply.clear();
        for (int i = 0; i < maxFrames; i++) {
            if (beforeFrame) beforeFrame(i);
            h.frame();
            // The main window's afterFrame: ImGuiManager's overdue listener
            // (BeforeApp), then the core's drain
            tcx::imgui::detail::settleOverdueValues(h.context());
            tc::mcp::drainDeferredResponses();
            if (frames) *frames = i + 1;
            if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                reply = future.get()();
                break;
            }
        }
        if (reply.empty()) return {{"rpcReply", "no reply within the frames run"}};
    }
    nlohmann::json r = nlohmann::json::parse(reply, nullptr, false);
    if (r.is_discarded() || !r.contains("result")) return {{"rpcReply", reply}};
    return nlohmann::json::parse(r["result"]["content"][0]["text"].get<std::string>(), nullptr, false);
}

// Touched record entries, by label (context and window not checked)
inline const tcx::imgui::TouchedWidget* touched(const std::string& label) {
    for (auto& t : tcx::imgui::getTouched()) {
        if (t.label == label) return &t;
    }
    return nullptr;
}

// A touched entry as tcx_imgui_get_touched prints it (null json if absent)
inline nlohmann::json touchedJson(const std::string& label) {
    for (auto& e : tcx::imgui::detail::touchedWidgetsJson()) {
        if (e.value("label", "") == label) return e;
    }
    return nullptr;
}

} // namespace harness
