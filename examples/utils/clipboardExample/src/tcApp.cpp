#include "tcApp.h"

void tcApp::setup() {
    // Nothing to initialize
}

void tcApp::draw() {
    clear(30);

    // Title
    setColor(1.0f);
    drawBitmapString("Clipboard Example", 20, 30);

    // Controls
    setColor(0.7f);
    drawBitmapString("Keys:", 20, 70);
    drawBitmapString("  1-5: Copy preset text to clipboard", 20, 90);
    drawBitmapString("  V:   Paste (get from clipboard)", 20, 110);
    drawBitmapString("  C:   Copy current text to clipboard", 20, 130);

    // Preset list
    setColor(0.5f);
    drawBitmapString("Presets:", 20, 170);
    drawBitmapString("  1: Hello, World!", 20, 190);
    drawBitmapString("  2: TrussC Framework", 20, 210);
    drawBitmapString("  3: 12345", 20, 230);
    drawBitmapString("  4: Special chars: @#$%&*!", 20, 250);
    drawBitmapString("  5: Multi word test string", 20, 270);

    // Current text
    setColor(0.4f, 1.0f, 0.4f);
    drawBitmapString("Current text:", 20, 320);
    setColor(1.0f);
    drawBitmapString("  \"" + currentText_ + "\"", 20, 340);

    // Last action
    setColor(1.0f, 1.0f, 0.4f);
    drawBitmapString("Last action: " + lastAction_, 20, 380);
}

void tcApp::keyPressed(int key) {
    string presets[] = {
        "Hello, World!",
        "TrussC Framework",
        "12345",
        "Special chars: @#$%&*!",
        "Multi word test string"
    };

    if (key >= '1' && key <= '5') {
        int index = key - '1';
        currentText_ = presets[index];
        setClipboardString(currentText_);
        lastAction_ = "Copied preset " + to_string(index + 1) + " to clipboard";
    }
    else if (key == 'v' || key == 'V') {
        currentText_ = getClipboardString();
        lastAction_ = "Pasted from clipboard (" + to_string(currentText_.size()) + " bytes)";
    }
    else if (key == 'c' || key == 'C') {
        if (!currentText_.empty()) {
            setClipboardString(currentText_);
            lastAction_ = "Copied current text to clipboard";
        } else {
            lastAction_ = "Nothing to copy";
        }
    }
}
