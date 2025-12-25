#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// AnalysisThread - Worker thread example using ThreadChannel
// =============================================================================
//
// Implementation based on oF's threadChannelExample.
// Uses two channels for bidirectional communication:
//   toAnalyze: Main -> Worker (analysis request)
//   analyzed:  Worker -> Main (analysis result)
//
// Pattern generation data is processed by the worker thread, and results are drawn by the main thread.
//
class AnalysisThread : public Thread {
public:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 48;
    static constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    AnalysisThread() : newFrame_(false) {
        pixels_.resize(TOTAL_PIXELS, 0.0f);
        // Start thread when class is created
        // Does not use CPU until data arrives
        startThread();
    }

    ~AnalysisThread() {
        // Close channels and wait for thread to finish
        toAnalyze_.close();
        analyzed_.close();
        waitForThread(true);
    }

    // Send analysis request (called from main thread)
    void analyze(const std::vector<float>& pixels) {
        toAnalyze_.send(pixels);
    }

    // Receive analysis result (called from main thread)
    void update() {
        newFrame_ = false;
        // If multiple frames have accumulated, use only the latest one
        while (analyzed_.tryReceive(pixels_)) {
            newFrame_ = true;
        }
    }

    // Check if there is a new frame
    bool isFrameNew() const {
        return newFrame_;
    }

    // Draw analysis result
    void draw(float x, float y, float scale = 4.0f) {
        if (pixels_.empty()) {
            setColor(1.0f);
            drawBitmapString("No frames analyzed yet", x + 20, y + 20);
            return;
        }

        for (int py = 0; py < HEIGHT; py++) {
            for (int px = 0; px < WIDTH; px++) {
                int i = py * WIDTH + px;
                float value = pixels_[i];
                setColor(value, value, value);
                drawRect(x + px * scale, y + py * scale, scale, scale);
            }
        }
    }

    // Number of processed frames
    int getAnalyzedCount() const {
        return analyzedCount_;
    }

protected:
    void threadedFunction() override {
        std::vector<float> pixels;

        // receive() blocks until data arrives (no CPU usage)
        while (toAnalyze_.receive(pixels)) {
            // Analysis processing (simple threshold processing here)
            for (auto& p : pixels) {
                if (p > 0.5f) {
                    p = 1.0f;
                } else {
                    p = 0.0f;
                }
            }

            analyzedCount_++;

            // Send result to main thread (optimized with move)
            analyzed_.send(std::move(pixels));
        }
    }

private:
    ThreadChannel<std::vector<float>> toAnalyze_;  // Main -> Worker
    ThreadChannel<std::vector<float>> analyzed_;   // Worker -> Main
    std::vector<float> pixels_;
    bool newFrame_;
    int analyzedCount_ = 0;
};
