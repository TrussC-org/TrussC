// Synthetic input is validated before event delivery or state changes.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <limits>
#include <set>

using namespace tc;

namespace {
int failures = 0;
int checks = 0;
int eventCalls = 0;
int appCalls = 0;
size_t maxReplyBytes = 0;

void check(const char* name, bool ok) {
    ++checks;
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

json call(const char* name, const json& args) {
    auto raw = mcp::internal::Server::instance().processMessage(
        json{{"jsonrpc", "2.0"}, {"id", "input-test"}, {"method", "tools/call"},
             {"params", {{"name", name}, {"arguments", args}}}}.dump());
    maxReplyBytes = std::max(maxReplyBytes, raw.size());
    return json::parse(raw);
}

bool succeeded(const json& reply) {
    return reply.contains("result") && !reply.at("result").value("isError", false);
}

json content(const json& reply) {
    if (!succeeded(reply)) return nullptr;
    return json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>(),
                       nullptr, false);
}

bool rejectedUnchanged(const char* name, const json& args) {
    auto& ctx = internal::currentWindowContext();
    const auto keys = ctx.keysPressed;
    const auto x = ctx.mouseX, y = ctx.mouseY;
    const auto button = ctx.mouseButton;
    const auto pressed = ctx.mousePressed;
    const int eventsBefore = eventCalls, appBefore = appCalls;
    const auto reply = call(name, args);
    return reply.value("id", "") == "input-test" && reply.contains("error") &&
        reply.at("error").value("code", 0) == -32602 &&
        !reply.at("error").value("message", "").empty() &&
        ctx.keysPressed == keys && ctx.mouseX == x && ctx.mouseY == y &&
        ctx.mouseButton == button && ctx.mousePressed == pressed &&
        eventCalls == eventsBefore && appCalls == appBefore;
}

std::set<int> keyDomain() {
    std::set<int> keys{32, 39, 59, 61, 96, 161, 162};
    for (auto range : {std::pair{44, 57}, {65, 93}, {256, 269}, {280, 284},
                       {290, 314}, {320, 336}, {340, 348}})
        for (int key = range.first; key <= range.second; ++key) keys.insert(key);
    return keys;
}
struct InputApp : App {
    void keyPressed(const KeyEventArgs&) override { ++appCalls; }
    void keyReleased(const KeyEventArgs&) override { ++appCalls; }
    void mousePressed(const MouseEventArgs&) override { ++appCalls; }
    void mouseReleased(const MouseEventArgs&) override { ++appCalls; }
    void mouseMoved(const MouseMoveEventArgs&) override { ++appCalls; }
    void mouseDragged(const MouseDragEventArgs&) override { ++appCalls; }
    void mouseScrolled(const ScrollEventArgs&) override { ++appCalls; }
};
} // namespace

TC_CORE_TEST_MAIN() {
#if defined(_WIN32)
    _putenv_s("TRUSSC_MCP", "0");
#else
    setenv("TRUSSC_MCP", "0", 1);
#endif
    mcp::setToolLevel(mcp::ToolLevel::Control);
    auto& ctx = internal::currentWindowContext();
    ctx.keysPressed.clear();
    ctx.mouseX = 17;
    ctx.mouseY = 23;
    std::vector<EventListener> listeners;
    listeners.push_back(events().keyPressed.listen([](const KeyEventArgs&) { ++eventCalls; }));
    listeners.push_back(events().keyReleased.listen([](const KeyEventArgs&) { ++eventCalls; }));
    listeners.push_back(events().mousePressed.listen([](const MouseEventArgs&) { ++eventCalls; }));
    listeners.push_back(events().mouseReleased.listen([](const MouseEventArgs&) { ++eventCalls; }));
    listeners.push_back(events().mouseMoved.listen([](const MouseMoveEventArgs&) { ++eventCalls; }));
    listeners.push_back(events().mouseDragged.listen([](const MouseDragEventArgs&) { ++eventCalls; }));
    listeners.push_back(events().mouseScrolled.listen([](const ScrollEventArgs&) { ++eventCalls; }));
    getMainThreadId();
    auto app = std::make_shared<InputApp>();
    internal::registerAppEventListeners([app] { return app; });

    const auto domain = keyDomain();
    check("key domain contains 120 codes", domain.size() == 120);
    auto pressed = content(call("tc_key_press", {{"key", SAPP_KEYCODE_A}, {"shift", true}}));
    check("valid key and modifier are held and reported",
          pressed.is_object() && pressed.value("heldKeys", json()) == json::array({SAPP_KEYCODE_A, KEY_LEFT_SHIFT}) &&
          isKeyPressed(SAPP_KEYCODE_A) && isShiftPressed() && eventCalls == 2 && appCalls == 2);
    auto released = content(call("tc_key_release", {{"key", SAPP_KEYCODE_A}, {"shift", true}}));
    check("valid key and modifier release are reported",
          released.is_object() && released.value("heldKeys", json()) == json::array() &&
          ctx.keysPressed.empty() && eventCalls == 4 && appCalls == 4);

    bool invalidDomain = true;
    for (int key = -1; key <= 512; ++key) {
        if (domain.count(key)) continue;
        for (auto tool : {"tc_key_press", "tc_key_release"})
            invalidDomain &= rejectedUnchanged(tool, {{"key", key}, {"shift", true},
                {"ctrl", true}, {"alt", true}, {"super", true}});
    }
    check("undefined key codes are rejected without changes", invalidDomain);
    ctx.keysPressed.clear();

    bool invalidTypes = true;
    for (const json& key : {json(-2147483648LL), json(2147483647LL), json(4294967361ULL),
                            json(std::numeric_limits<uint64_t>::max()), json(65.5), json(65.0),
                            json("65"), json(true), json(nullptr), json::array(), json::object()})
        for (auto tool : {"tc_key_press", "tc_key_release"})
            invalidTypes &= rejectedUnchanged(tool, {{"key", key}, {"shift", true}});
    for (auto tool : {"tc_key_press", "tc_key_release"})
        invalidTypes &= rejectedUnchanged(tool, json::object());
    check("invalid key types and numeric limits are rejected", invalidTypes);
    ctx.keysPressed.clear();

    bool flagsValid = true;
    for (auto tool : {"tc_key_press", "tc_key_release"}) {
        for (auto flag : {"shift", "ctrl", "alt", "super"}) {
            call("tc_key_press", {{"key", SAPP_KEYCODE_A}, {"shift", true}, {"ctrl", true},
                                  {"alt", true}, {"super", true}});
            json args{{"key", SAPP_KEYCODE_A}, {"shift", true}, {"ctrl", true}, {"alt", true}, {"super", true}};
            args[flag] = "invalid";
            flagsValid &= rejectedUnchanged(tool, args);
            ctx.keysPressed.clear();
            flagsValid &= rejectedUnchanged(tool, args);
            ctx.keysPressed.clear();
        }
    }
    check("invalid modifier flags leave no partial input", flagsValid);

    bool allKeys = true;
    for (int key : domain) {
        auto reply = content(call("tc_key_press", {{"key", key}}));
        allKeys &= reply.is_object() && reply.value("heldKeys", json()).size() == ctx.keysPressed.size() &&
            isKeyPressed(key) && ctx.keysPressed.size() <= domain.size();
    }
    check("every valid key can be held within the domain limit", allKeys && ctx.keysPressed.size() == 120);
    auto full = content(call("tc_key_press", {{"key", SAPP_KEYCODE_A}}));
    check("held keys are sorted and duplicate presses do not grow state",
          full.is_object() && full.value("heldKeys", json()) == json(std::vector<int>(domain.begin(), domain.end())));
    int rejections = 0;
    for (int i = 0; i < 1024; ++i)
        rejections += rejectedUnchanged("tc_key_press", {{"key", 1000 + i}, {"shift", true}});
    check("sequential invalid keys preserve the full held set", rejections == 1024 && ctx.keysPressed.size() == 120);
    std::printf("sequential rejections=%d held=%zu max_reply_bytes=%zu\n", rejections, ctx.keysPressed.size(), maxReplyBytes);
    check("key reply size stays within 1024 bytes", maxReplyBytes <= 1024);
    ctx.keysPressed.clear();
    bool releasedAll = true;
    for (int key : domain) {
        call("tc_key_press", {{"key", key}});
        auto reply = content(call("tc_key_release", {{"key", key}}));
        releasedAll &= reply.is_object() && reply.value("heldKeys", json()) == json::array() && !isKeyPressed(key);
    }
    check("every valid key can be released", releasedAll);

    bool mouseInvalid = true, mouseValid = true;
    for (auto tool : {"tc_mouse_move", "tc_mouse_press", "tc_mouse_release", "tc_mouse_click"}) {
        for (const json& button : {json(-2), json(3), json(4294967296ULL), json(1.5), json(1.0), json("0"), json(nullptr)})
            mouseInvalid &= rejectedUnchanged(tool, {{"x", 1}, {"y", 2}, {"button", button}});
        if (std::string(tool) != "tc_mouse_move")
            mouseInvalid &= rejectedUnchanged(tool, {{"x", 1}, {"y", 2}, {"button", -1}});
        for (auto coordinate : {"x", "y"}) {
            json args{{"x", 1}, {"y", 2}};
            args[coordinate] = 1e100;
            mouseInvalid &= rejectedUnchanged(tool, args);
            args[coordinate] = true;
            mouseInvalid &= rejectedUnchanged(tool, args);
            args.erase(coordinate);
            mouseInvalid &= rejectedUnchanged(tool, args);
        }
        for (int button = 0; button <= 2; ++button)
            mouseValid &= succeeded(call(tool, {{"x", 1.5}, {"y", -2}, {"button", button}}));
        mouseValid &= succeeded(call(tool, {{"x", 1}, {"y", 2}}));
    }
    mouseValid &= succeeded(call("tc_mouse_move", {{"x", 1}, {"y", 2}, {"button", -1}}));
    check("invalid mouse input leaves callbacks and state unchanged", mouseInvalid);
    check("valid mouse buttons and default arguments still work", mouseValid);
    bool clickFlags = true;
    for (auto flag : {"shift", "ctrl", "alt", "super"})
        clickFlags &= rejectedUnchanged("tc_mouse_click", {{"x", 1}, {"y", 2}, {flag, 1}});
    check("invalid click modifier flags are rejected", clickFlags);
    check("finite scroll deltas still work", succeeded(call("tc_mouse_scroll", {{"dx", -1.5}, {"dy", 2}})));
    bool scrollInvalid = true;
    for (auto coordinate : {"dx", "dy"}) {
        json args{{"dx", 1}, {"dy", 2}};
        args[coordinate] = 1e100;
        scrollInvalid &= rejectedUnchanged("tc_mouse_scroll", args);
        args[coordinate] = false;
        scrollInvalid &= rejectedUnchanged("tc_mouse_scroll", args);
        args.erase(coordinate);
        scrollInvalid &= rejectedUnchanged("tc_mouse_scroll", args);
    }
    check("invalid scroll input leaves callbacks and state unchanged", scrollInvalid);
    std::printf("checks=%d failures=%d\n", checks, failures);
    internal::mainWindowContext().appEventListeners.clear();
    return failures ? 1 : 0;
}
