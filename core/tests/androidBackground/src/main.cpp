#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcBackgroundAudio.h"
#include <thread>
using namespace tc;
namespace {
bool smoke = false;
class BackgroundProbe : public App {
    EventListener visibility;
    std::chrono::steady_clock::time_point nextLog;
    uint64_t counter = 0;
    bool updatesEnabled = true;
    bool serviceEnabled = true;
    bool backgroundProbeStarted = false;
    SoundBuffer nextTrack;
    EventListener trackLoaded;
    fs::path wav;
public:
    void setup() override {
        setAndroidForegroundService(true);
        setUpdateWhileHidden(true);
        wav = getTempPath("androidBackground.wav");
        tcCoreTest::writeBackgroundWav(wav);
        trackLoaded = nextTrack.onLoad.listen([](LoadResult& result) {
            logNotice("androidBackground") << "loadAsync onLoad ok=" << bool(result) << " hidden=" << isWindowOccluded();
        });
        nextLog = std::chrono::steady_clock::now();
        visibility = events().occludedChanged.listen([](OccludedEventArgs& event) {
            logNotice("androidBackground") << "occluded=" << event.occluded;
        });
        logNotice("androidBackground") << "ready; tap left: toggle updates; tap right or S: toggle service";
    }
    void update() override {
        ++counter;
        if (!isWindowOccluded()) backgroundProbeStarted = false;
        else if (!backgroundProbeStarted) {
            backgroundProbeStarted = true;
            nextTrack.loadAsync(wav);
            std::thread([] {
                runOnMainThread([] {
                    logNotice("androidBackground") << "runOnMainThread hidden=" << isWindowOccluded();
                });
            }).detach();
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextLog) {
            logNotice("androidBackground") << "tick=" << counter << " hidden=" << isWindowOccluded();
            nextLog = now + std::chrono::seconds(1);
        }
        if (smoke) exitApp();
    }
    void draw() override {
        drawBitmapString("Tap left: toggle hidden updates", 12, 30);
        drawBitmapString("Tap right: toggle foreground service", 12, 55);
    }
    void mousePressed(const MouseEventArgs& event) override {
        if (event.pos.x >= getWindowWidth() * 0.5f) {
            toggleService();
            return;
        }
        updatesEnabled = !updatesEnabled;
        setUpdateWhileHidden(updatesEnabled);
        logNotice("androidBackground") << "updates=" << updatesEnabled;
    }
    void keyPressed(const KeyEventArgs& event) override {
        if (event.key == 's' || event.key == 'S') toggleService();
    }
    void toggleService() {
        serviceEnabled = !serviceEnabled;
        setAndroidForegroundService(serviceEnabled);
        logNotice("androidBackground") << "service=" << serviceEnabled;
    }
    void cleanup() override {
        setAndroidForegroundService(false);
        fs::remove(wav);
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifndef __ANDROID__
    const std::string mode = argc > 1 ? argv[1] : "";
    smoke = mode == "--smoke" || mode == "--headless";
    if (mode == "--headless") return runHeadlessApp<BackgroundProbe>();
    if (!smoke && mode != "--device") return 0;
#else
    (void)argc;
    (void)argv;
#endif
    return runApp<BackgroundProbe>();
}
