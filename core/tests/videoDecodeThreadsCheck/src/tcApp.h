#pragma once

#include <TrussC.h>

#if defined(__linux__) && !defined(__ANDROID__)
class tcApp : public tc::App {
public:
    inline static tc::fs::path clip;
    inline static int playerCount = 1, threads = 0, failures = 0;
    inline static bool hardware = true, completed = false;
    inline static double seconds = 4;

    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

private:
    std::vector<std::unique_ptr<tc::VideoPlayer>> players;
    std::vector<uint64_t> delivered;
    std::chrono::steady_clock::time_point started;
    bool finished = false;
    void finish();
};
#endif
