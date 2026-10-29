// Production display regression: no presentation/capture/encoder test seams.
#pragma once
#include "../../common/tcDisplayTest.h"
#include <array>

namespace {
class AfterFrameScreenshotApp : public App {
    struct Capture {
        fs::path path;
        std::array<unsigned char, 3> rgb;
        int width, height;
    };
    struct FrameState {
        int frame = 0;
        int verified = 0;
        std::vector<Capture> pending;
        std::vector<EventListener> listeners;
    } mainState, secondaryState;
    std::shared_ptr<tc::Window> secondary;
    fs::path directory;

    static std::array<unsigned char, 3> colour(int frame, bool main) {
        // Primary/complementary colours avoid transfer-function ambiguity.
        std::array<unsigned char, 3> rgb{};
        rgb[(frame - 1) % 3] = 255;
        if (!main) for (auto& component : rgb) component = 255 - component;
        return rgb;
    }
    void bind(CoreEvents& target, FrameState& state, bool main) {
        state.listeners.push_back(target.draw.listen([&state, main] {
            const auto rgb = colour(++state.frame, main);
            clear(rgb[0] / 255.f, rgb[1] / 255.f, rgb[2] / 255.f, 1.f);
        }));
        // Install in deliberately reversed priority order during App::setup().
        // Every listener must queue before the host snapshots the drain.
        for (int priority : {200, 0, 100}) {
            state.listeners.push_back(target.afterFrame.listen([this, &state, main, priority] {
                if (priority == 0) {
                    for (const auto& capture : state.pending) {
                        Pixels pixels;
                        const bool loaded = pixels.load(capture.path).ok();
                        const bool valid = loaded && pixels.getWidth() == capture.width && pixels.getHeight() == capture.height
                            && pixels.getChannels() >= 3;
                        check("afterFrame PNG decodes at rendered dimensions", valid);
                        bool matches = valid;
                        if (matches) {
                            const auto* data = pixels.getData();
                            std::printf("TRACE readback %s expected=%u,%u,%u actual=%u,%u,%u\n",
                                capture.path.filename().string().c_str(),
                                capture.rgb[0], capture.rgb[1], capture.rgb[2], data[0], data[1], data[2]);
                            for (size_t i = 0; i < static_cast<size_t>(capture.width * capture.height * pixels.getChannels()); i += pixels.getChannels())
                                for (int channel = 0; channel < 3; ++channel)
                                    matches &= data[i + channel] == capture.rgb[channel];
                        }
                        const std::string label = std::string("afterFrame THAT frame ") +
                            (main ? "main " : "secondary ") + capture.path.filename().string();
                        check(label.c_str(), matches);
                        ++state.verified;
                    }
                    state.pending.clear();
                }
                if (state.frame > 3) return;
                const auto path = directory / (std::string(main ? "main-" : "secondary-") +
                    std::to_string(state.frame) + "-" + std::to_string(priority) + ".png");
                check("afterFrame destination does not exist before queueing", !fs::exists(path));
                check("saveScreenshot queued from afterFrame", saveScreenshot(path));
                // Native WMs may clamp the requested size when positioning.
                // Record this frame's actual framebuffer, before the drain.
                const auto& context = internal::currentWindowContext();
                state.pending.push_back({path, colour(state.frame, main), context.fbWidth, context.fbHeight});
                std::printf("TRACE afterFrame %s frame=%d priority=%d owner=%llu\n",
                    main ? "main" : "secondary", state.frame, priority,
                    static_cast<unsigned long long>(internal::currentWindowContext().swapchainTarget.ownerId));
            }, priority));
        }
    }
public:
    void setup() override {
        directory = fs::absolute("after-frame-screenshots");
        fs::remove_all(directory);
        fs::create_directories(directory);
        bind(events(), mainState, true);
        check("display backend supports a secondary", internal::supportsSecondaryWindows());
        if (internal::supportsSecondaryWindows()) {
            WindowSettings settings;
            settings.setSize(40, 30).setHighDpi(false).setTitle("afterFrame screenshot secondary");
            secondary = createWindow(settings);
            check("afterFrame secondary created", secondary && secondary->isOpen());
            if (secondary) {
                // Keep the windows apart so native occlusion cannot stop ticks.
                secondary->setPosition(300, 300);
                bind(secondary->events(), secondaryState, false);
            }
        }
    }
    void update() override {
        // Bound (test termination): stop after 120 main draws if a secondary stalls; cleanup fails incomplete coverage.
        if ((mainState.verified == 9 && secondaryState.verified == 9) || mainState.frame > 120)
            exitApp();
    }
    void cleanup() override {
        check("main verified all nine afterFrame captures", mainState.verified == 9);
        check("secondary verified all nine afterFrame captures", secondaryState.verified == 9);
        mainState.listeners.clear();
        secondaryState.listeners.clear();
        if (secondary) secondary->close();
        secondary.reset();
        fs::remove_all(directory);
    }
};
}
