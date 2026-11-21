#include "tcApp.h"
#include <cstdio>

#if defined(__linux__) && !defined(__ANDROID__)

void tcApp::setup() {
    for (int i = 0; i < playerCount; ++i) {
        auto player = std::make_unique<tc::VideoPlayer>();
        player->setUseHwAccel(hardware);
        player->setDecodeThreads(threads);
        player->setAutoPoster(false);
        player->setVolume(0);
        if (!player->load(clip)) {
            std::fprintf(stderr, "Player %d failed to load\n", i);
            ++failures;
            tc::exitApp();
            return;
        }
        player->setLoop(true);
        std::printf("player=%d requested_threads=%d isUsingHwAccel=%d getHwAccelName=%s fps=%.3f\n",
            i, player->getDecodeThreads(), int(player->isUsingHwAccel()),
            player->getHwAccelName().c_str(), double(player->getFrameRate()));
        players.push_back(std::move(player));
    }
    delivered.resize(players.size());
    started = std::chrono::steady_clock::now();
    for (auto& player : players) player->play();
}

void tcApp::update() {
    if (finished || players.size() != size_t(playerCount)) return;
    for (size_t i = 0; i < players.size(); ++i) {
        players[i]->update();
        if (players[i]->isFrameNew()) ++delivered[i];
        if (players[i]->hasError()) {
            std::fprintf(stderr, "Player %zu playback error: %s\n", i,
                players[i]->getErrorMessage().c_str());
            ++failures;
            finish();
            return;
        }
    }
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >= seconds)
        finish();
}

void tcApp::draw() {
    tc::clear(0);
    // Keep every player on screen and upload/render every delivered frame.
    const int columns = int(std::ceil(std::sqrt(double(playerCount))));
    for (size_t i = 0; i < players.size(); ++i)
        players[i]->draw(float(i % columns) * 320, float(i / columns) * 180, 320, 180);
}

void tcApp::cleanup() {
    for (auto& player : players) player->close();
}

void tcApp::finish() {
    finished = true;
    completed = true;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("elapsed_seconds=%.3f\n", elapsed);
    for (size_t i = 0; i < players.size(); ++i)
        std::printf("player=%zu delivered=%llu expected=%.1f isUsingHwAccel=%d getHwAccelName=%s\n",
            i, static_cast<unsigned long long>(delivered[i]), elapsed * players[i]->getFrameRate(),
            int(players[i]->isUsingHwAccel()), players[i]->getHwAccelName().c_str());
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
        if (line.rfind("VmHWM:", 0) == 0) std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    tc::exitApp();
}

#endif
