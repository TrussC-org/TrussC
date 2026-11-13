// =============================================================================
// clipboardExample - Copy text and receive platform paste events.
//
// Use Cmd/Ctrl+V to receive clipboardPasted, including browser paste where
// on-demand clipboard reads are restricted.
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // Listen for the platform paste gesture instead of polling a key ourselves.
    // This fires on Cmd+V (macOS) / Ctrl+V (Win/Linux) / browser paste (Web),
    // and works on the Web where reading the clipboard on demand is blocked.
    pasteListener_ = events().clipboardPasted.listen(this, &tcApp::onClipboardPasted);
}

void tcApp::onClipboardPasted(ClipboardPastedEventArgs& e) {
    if (!e.text.empty()) {
        pastedLines_.push_back(e.text);
    }
}

void tcApp::draw() {
    clear(0.12f);

    // Left side - Presets
    setColor(1.0f);
    drawBitmapString("Clipboard Example", 20, 30);

    setColor(0.6f);
    drawBitmapString("1-3: Select  C: Copy  Cmd/Ctrl+V: Paste", 20, 55);

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
    else if (key == 'C' && selected_ > 0) {
        setClipboardString(presets_[selected_ - 1]);
    }
    // Paste is handled by the clipboardPasted event (Cmd+V / Ctrl+V), not here.
}
