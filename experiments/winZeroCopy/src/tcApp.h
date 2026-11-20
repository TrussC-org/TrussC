#pragma once
#include <TrussC.h>
#include <chrono>
#include <fstream>
#include <memory>

struct Options {
    std::vector<tc::fs::path> clips;
    int players = 1;
    double seconds = 60;
    double warmup = 5;
    int openclose = 0;
    tc::fs::path output = "winZeroCopy.csv";
};
extern Options options;

class tcApp : public tc::App {
public:
    void setup() override;
    void update() override;
    void draw() override;
private:
    using Clock = std::chrono::steady_clock;
    enum class Phase { OpenClose, OpenCloseDrain, Warmup, Screenshot, Measure, Finished };
    std::vector<std::unique_ptr<tc::VideoPlayer>> videos_;
    std::vector<uint64_t> frames_;
    Phase phase_ = Phase::Warmup;
    Clock::time_point phaseStart_;
    double cpuStart_ = 0;
    uint64_t draws_ = 0;
    int cycles_ = 0;
    int drainFrames_ = 0;
    std::string privateWarm_, privateMid_, privateAfter_;
    bool liveDrawn_ = false;
    bool openPlayers();
    void finish(double elapsed);
};
