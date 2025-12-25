#include "tcApp.h"

void tcApp::setup() {}

void tcApp::draw() {
    clear(0.12f);

    // Left side - Presets
    setColor(1.0f);
    drawBitmapString("Clipboard Example", 20, 30);

    setColor(0.6f);
    drawBitmapString("1-3: Select  C: Copy  V: Paste", 20, 55);

    float y = 100;
    for (int i = 0; i < 3; i++) {
        if (selected_ == i + 1) {
            drawBitmapStringHighlight(presets_[i], 20, y, colors::yellow, colors::black);
        } else {
            setColor(0.8f);
            drawBitmapString(presets_[i], 20, y);
        }
        y += 25;
    }

    // Right side - Clipboard state
    setColor(1.0f);
    drawBitmapString("Clipboard:", 500, 100);
    setColor(0.6f, 0.9f, 0.6f);
    string clip = getClipboardString();
    drawBitmapString(clip.empty() ? "(empty)" : clip, 500, 125);

    // Pasted lines
    setColor(1.0f);
    drawBitmapString("Pasted:", 500, 180);
    setColor(0.7f);
    y = 205;
    for (auto& line : pastedLines_) {
        drawBitmapString(line, 500, y);
        y += 20;
    }
}

void tcApp::keyPressed(int key) {
    if (key >= '1' && key <= '3') {
        selected_ = key - '0';
    }
    else if ((key == 'c' || key == 'C') && selected_ > 0) {
        setClipboardString(presets_[selected_ - 1]);
    }
    else if (key == 'v' || key == 'V') {
        string clip = getClipboardString();
        if (!clip.empty()) {
            pastedLines_.push_back(clip);
        }
    }
}
