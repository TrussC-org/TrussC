#include "tcApp.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>

int main(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (argc == 1) {
        std::puts("SKIP: manual real-GPU check; supply clip players threads hw-on(1)/off(0) [seconds]");
        return 77;
    }
    try {
        if (argc != 5 && argc != 6) throw std::invalid_argument("arguments");
        tcApp::clip = tc::fs::absolute(argv[1]);
        const auto integer = [](const char* value) {
            size_t used = 0;
            const int parsed = std::stoi(value, &used);
            if (value[used] != '\0') throw std::invalid_argument("integer");
            return parsed;
        };
        tcApp::playerCount = integer(argv[2]);
        tcApp::threads = integer(argv[3]);
        const int hw = integer(argv[4]);
        tcApp::hardware = hw == 1;
        if (argc == 6) {
            size_t used = 0;
            tcApp::seconds = std::stod(argv[5], &used);
            if (argv[5][used] != '\0') throw std::invalid_argument("seconds");
        }
        if (tcApp::playerCount <= 0 || (hw != 0 && hw != 1) || !std::isfinite(tcApp::seconds) || tcApp::seconds <= 0)
            throw std::invalid_argument("range");
    } catch (const std::exception&) {
        std::fprintf(stderr, "Usage: videoDecodeThreadsCheck clip players threads hw-on(1)/off(0) [seconds>0]\n");
        return 2;
    }
    tc::WindowSettings settings;
    const int columns = int(std::ceil(std::sqrt(double(tcApp::playerCount))));
    settings.setSize(columns * 320, ((tcApp::playerCount - 1) / columns + 1) * 180);
    settings.setHighDpi(false);
    return tcCoreTest::runDisplayTest<tcApp>(settings, tcApp::failures, [] {
        if (!tcApp::completed) {
            std::fprintf(stderr, "Check did not complete\n");
            ++tcApp::failures;
        }
    });
#else
    (void)argc;
    (void)argv;
    std::puts("SKIP: Linux-only manual decoder check");
    return 77;
#endif
}
