#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// clipboardExample - Clipboard API demo
// =============================================================================
// Test clipboard read/write with keyboard input
//
// Controls:
//   1-5: Copy preset text to clipboard
//   V:   Paste (get from clipboard)
//   C:   Copy current text to clipboard
//
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    string currentText_ = "Hello, World!";
    string lastAction_ = "Press 1-5 to copy preset, V to paste";
};
