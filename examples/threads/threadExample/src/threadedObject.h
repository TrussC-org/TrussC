#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// ThreadedObject - Object that performs calculations in a thread
// Example of using Thread by inheritance
// =============================================================================
//
// Implementation based on oF's threadExample.
// Generates pixel data (noise pattern) in a thread,
// and retrieves it as drawing data in the main thread.
//
// To demonstrate the importance of data protection using Mutex,
// both update() and updateNoLock() are provided.
//
class ThreadedObject : public Thread {
public:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 48;
    static constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    // Destructor - Wait for thread to finish
    ~ThreadedObject() {
        stop();
        waitForThread(false);
    }

    // Initialize
    void setup() {
        pixelData.resize(TOTAL_PIXELS);
        displayData.resize(TOTAL_PIXELS);
        for (int i = 0; i < TOTAL_PIXELS; i++) {
            pixelData[i] = 0.0f;
            displayData[i] = 0.0f;
        }
        start();
    }

    // Start thread
    void start() {
        // If previous thread remains, stop and wait
        if (isThreadRunning()) {
            stop();
        }
        waitForThread(false);  // Wait for old thread to completely finish
        startThread();
    }

    // Stop thread
    // Notify condition_variable to release thread's wait
    void stop() {
        std::unique_lock<std::mutex> lck{dataMutex_};
        stopThread();
        condition.notify_all();
    }

    // Processing executed in thread
    void threadedFunction() override {
        logNotice("Thread") << "[threadedFunction] thread started";

        while (isThreadRunning()) {
            // Increment thread frame count
            threadFrameNum++;

            // Update pixel data (after locking)
            {
                std::unique_lock<std::mutex> lk{dataMutex_};

                // Simple pattern generation (sin wave + time variation)
                float t = threadFrameNum * 0.05f;
                for (int i = 0; i < TOTAL_PIXELS; i++) {
                    float ux = (i % WIDTH) / (float)WIDTH;
                    float uy = (i / WIDTH) / (float)HEIGHT;

                    // Simple noise-like pattern
                    float value = std::sin(ux * 10.0f + t) * std::sin(uy * 10.0f + t * 0.7f);
                    value = (value + 1.0f) * 0.5f;  // Normalize to 0-1
                    pixelData[i] = value;
                }

                // Wait until main thread retrieves data
                // However, exit if stop signal arrives
                condition.wait(lk, [this]{ return !isThreadRunning() || dataReady; });
                dataReady = false;
            }
        }

        logNotice("Thread") << "[threadedFunction] thread stopped";
    }

    // Update (with lock) - No tearing
    void update() {
        std::unique_lock<std::mutex> lk{dataMutex_};
        // Copy pixel data to drawing buffer
        displayData = pixelData;
        dataReady = true;
        condition.notify_all();
    }

    // Update (without lock) - Tearing may occur
    void updateNoLock() {
        // Tearing may occur because no lock is used
        displayData = pixelData;
        dataReady = true;
        condition.notify_all();
    }

    // Draw
    void draw(float x, float y, float scale = 4.0f) {
        // Draw rectangles using displayData
        for (int py = 0; py < HEIGHT; py++) {
            for (int px = 0; px < WIDTH; px++) {
                int i = py * WIDTH + px;
                float value = displayData[i];
                setColor(value, value, value);
                drawRect(x + px * scale, y + py * scale, scale, scale);
            }
        }
    }

    // Get thread frame count
    int getThreadFrameNum() const {
        return threadFrameNum;
    }

protected:
    // Pixel data for calculation (updated in thread)
    std::vector<float> pixelData;

    // Pixel data for drawing (used in main thread)
    std::vector<float> displayData;

    // For synchronization
    std::condition_variable condition;

    // Data ready flag (for spurious wakeup protection)
    bool dataReady = false;

    // Thread frame count
    int threadFrameNum = 0;
};
