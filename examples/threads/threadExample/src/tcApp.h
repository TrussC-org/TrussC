#pragma once

#include <TrussC.h>
#include "threadedObject.h"
using namespace std;
using namespace tc;

// =============================================================================
// tcApp - Thread sample application
// =============================================================================
//
// Implementation based on oF's threadExample.
// Generates data in a thread, draws in the main thread.
//
// Controls:
//   a: Start thread
//   s: Stop thread
//   l: Switch to update with lock
//   n: Switch to update without lock (tearing occurs)
//
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;

    // Thread object
    ThreadedObject threadedObject;

    // Whether to use lock
    bool doLock = false;
};
