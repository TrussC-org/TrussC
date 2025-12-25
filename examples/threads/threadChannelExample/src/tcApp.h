#pragma once

#include <TrussC.h>
#include "AnalysisThread.h"
using namespace std;
using namespace tc;

// =============================================================================
// tcApp - ThreadChannel sample application
// =============================================================================
//
// Demo of inter-thread communication using ThreadChannel.
// Generates patterns in the main thread, processes analysis in the worker thread.
//
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    // Data for pattern generation
    std::vector<float> sourcePixels_;
    int frameNum_ = 0;

    // Analysis thread
    AnalysisThread analyzer_;
};
