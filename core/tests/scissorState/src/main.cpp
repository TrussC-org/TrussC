// #914: float-only calls, empty pops and frame-boundary recovery.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>

using namespace tc;
namespace {
static_assert(std::is_same_v<decltype(&setScissor), void (*)(float, float, float, float)>);
int failures = 0;
int emptyWarnings = 0, leakWarnings = 0;
bool depthNamed = false, completed = false;
// Keep observing if a native runApp() returns before the display loop finishes.
EventListener warningListener;
void check(const char* label, bool ok) {
    std::printf("scissorState: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void observe(const LogEventArgs& e) {
    if (e.level != LogLevel::Warning) return;
    if (e.message == "[Scissor] popScissor() called with an empty stack; clipping reset") ++emptyWarnings;
    if (e.message.rfind("[Scissor] frame start discarded scissor state", 0) == 0) {
        ++leakWarnings;
        depthNamed = e.message.find("leaked depth 2)") != std::string::npos;
    }
}
void cpuChecks() {
    internal::WindowContext first, second;
    auto& target = first.swapchainTarget;
    auto& other = second.swapchainTarget;
    other.currentScissor = {3, 4, 5, 6, true};
    other.scissorStack.push_back({});
    auto leak = [&] {
        target.currentScissor = {1, 2, 3, 4, true};
        target.scissorStack.resize(2);
        first.resetScissorAtFrameStart();
    };
    leak();
    check("frame start clears active scissor", !target.currentScissor.active);
    check("frame start clears stack", target.scissorStack.empty());
    check("leak warns and names depth", leakWarnings == 1 && depthNamed);
    check("other window retains its scissor and stack", other.currentScissor.active && other.scissorStack.size() == 1);
    // OnceGate has no injected clock. The Decision permits one wait over 1 s.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    target.currentScissor.active = true;
    first.resetScissorAtFrameStart();
    check("active scissor without stack is reset", !target.currentScissor.active);
    check("active scissor without stack does not warn", leakWarnings == 1);
    leak();
    check("continuing leak warns again after interval", leakWarnings == 2 && depthNamed);
    first.resetScissorAtFrameStart();
    check("clean frame does not warn", leakWarnings == 2);
}

class ScissorApp : public App {
    EventListener after_;
    int frame_ = 0;
public:
    void setup() override {
        after_ = events().afterFrame.listen([this] {
            if (frame_ == 2) {
                Pixels pixels;
                const bool captured = grabScreen(pixels);
                check("next frame capture succeeds", captured);
                if (captured) {
                    const Color color = pixels.getColor(32, 32);
                    check("next frame draws outside leaked scissor", color.g > 0.95f && color.r < 0.05f);
                }
                completed = true;
                exitApp();
            }
        });
    }
    void draw() override {
        auto& target = internal::currentTarget();
        if (frame_++ == 0) {
            setScissor(0, 0, 100, 50); // compile check: all ints
            setScissor(0, 0, 100.5f, 50); // compile check: mixed ints/floats
            check("mixed call retains fractional width", target.currentScissor.w == 100.5f);
            Fbo fbo;
            fbo.allocate(16, 16);
            fbo.begin(1, 0, 0);
            setupScreenOrtho();
            auto& fboTarget = internal::currentTarget();
            for (int i = 0; i < 2; ++i) {
                setScissor(1, 2, 3, 4);
                popScissor();
                check("empty pop resets clipping", !fboTarget.currentScissor.active && fboTarget.scissorStack.empty());
            }
            check("repeated empty pops warn once per interval", emptyWarnings == 1);
            setColor(0, 1, 0);
            drawRect(0, 0, 16, 16);
            fbo.end();
            Pixels pixels;
            pixels.allocate(16, 16, 4);
            const bool read = fbo.readPixels(pixels.getData());
            check("empty-pop readback succeeds", read);
            if (read) check("empty pop resets GPU clipping", pixels.getColor(8, 8).g > 0.95f);
            pushScissor(1, 2, 3, 4);
            pushScissor(1, 2, 1, 1);
            drawRect(0, 0, 64, 64);
        } else {
            check("real frame start clears leaked stack and scissor",
                  target.scissorStack.empty() && !target.currentScissor.active);
            setupScreenOrtho();
            clear(1, 0, 0);
            setColor(0, 1, 0);
            drawRect(0, 0, 64, 64);
        }
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    warningListener = getLogger().onLog.listen(observe);
    cpuChecks();
    if (argc <= 1 || std::strcmp(argv[1], "--gpu-check") != 0) return failures ? 1 : 0;
    WindowSettings settings;
    settings.setSize(64, 64);
    settings.setHighDpi(false);
    return tcCoreTest::runDisplayTest<ScissorApp>(settings, failures, [] {
        check("GPU cases completed", completed);
    });
}
