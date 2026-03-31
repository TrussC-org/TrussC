// =============================================================================
// tcApp.cpp - Async Dialog Example
// Works on all platforms including iOS
// =============================================================================

#include "tcApp.h"

using namespace std;

void tcApp::setup() {
    logNotice("tcApp") << "=== Async Dialog Example ===";
    logNotice("tcApp") << "A: Alert dialog";
    logNotice("tcApp") << "C: Confirm dialog";
    logNotice("tcApp") << "O: Open file dialog";
    logNotice("tcApp") << "S: Save dialog";
    logNotice("tcApp") << "============================";

    setupButtons();
}

void tcApp::setupButtons() {
    buttons.clear();
    float bw = 200, bh = 50;
    float startX = 40, startY = 80;
    float gap = 15;

    buttons.push_back({startX, startY, bw, bh, "Alert (A)"});
    buttons.push_back({startX, startY + (bh + gap), bw, bh, "Confirm (C)"});
    buttons.push_back({startX, startY + (bh + gap) * 2, bw, bh, "Open File (O)"});
    buttons.push_back({startX, startY + (bh + gap) * 3, bw, bh, "Save File (S)"});
}

int tcApp::hitTest(float x, float y) {
    for (int i = 0; i < (int)buttons.size(); i++) {
        auto& b = buttons[i];
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
            return i;
        }
    }
    return -1;
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.16f);

    // Title
    setColor(1.0f);
    drawBitmapString("Async Dialog Example", 40, 40);

    // Draw buttons
    fill();
    for (auto& b : buttons) {
        setColor(0.25f, 0.5f, 0.8f);
        drawRect(b.x, b.y, b.w, b.h);
        setColor(1.0f);
        drawBitmapString(b.label, b.x + 15, b.y + 30);
    }

    // Status
    float infoY = 360;
    setColor(0.4f, 0.78f, 1.0f);
    drawBitmapString("Status: " + statusMessage, 40, infoY);
    infoY += 35;

    // Show result
    if (lastResult.success) {
        setColor(0.4f, 1.0f, 0.4f);
        drawBitmapString("Success!", 40, infoY);
        infoY += 25;

        setColor(0.86f);
        drawBitmapString("File: " + lastResult.fileName, 40, infoY);
        infoY += 20;
        drawBitmapString("Path: " + lastResult.filePath, 40, infoY);
        infoY += 35;

        if (hasImage && loadedImage.isAllocated()) {
            setColor(1.0f);
            float maxW = getWindowWidth() - 80;
            float maxH = getWindowHeight() - infoY - 40;
            float imgW = loadedImage.getWidth();
            float imgH = loadedImage.getHeight();
            float scale = min(maxW / imgW, maxH / imgH);
            if (scale > 1.0f) scale = 1.0f;
            loadedImage.draw(40, infoY, imgW * scale, imgH * scale);
        }
    } else if (!statusMessage.empty() && statusMessage.find("Cancelled") != string::npos) {
        setColor(1.0f, 0.4f, 0.4f);
        drawBitmapString("Cancelled", 40, infoY);
    }
}

void tcApp::triggerAction(int index) {
    switch (index) {
        case 0: {
            // Alert
            statusMessage = "Showing alert...";
            alertDialogAsync("Hello!", "This is an async alert from TrussC.", [this]() {
                statusMessage = "Alert dismissed";
            });
            break;
        }
        case 1: {
            // Confirm
            statusMessage = "Showing confirm...";
            confirmDialogAsync("Confirm", "Do you like TrussC?", [this](bool yes) {
                statusMessage = yes ? "You said Yes!" : "You said No...";
            });
            break;
        }
        case 2: {
            // Open file
            statusMessage = "Opening file picker...";
            hasImage = false;
            loadDialogAsync("Select a file", "", "", false,
                [this](const FileDialogResult& result) {
                lastResult = result;
                if (result.success) {
                    statusMessage = "File selected: " + result.fileName;

                    // Try to load as image
                    string path = result.filePath;
                    string lower = path;
                    for (auto& c : lower) c = tolower(c);

                    if (lower.find(".png") != string::npos ||
                        lower.find(".jpg") != string::npos ||
                        lower.find(".jpeg") != string::npos) {
                        if (loadedImage.load(path)) {
                            hasImage = true;
                        }
                    }
                } else {
                    statusMessage = "Cancelled";
                }
            });
            break;
        }
        case 3: {
            // Save file
            statusMessage = "Opening save dialog...";
            hasImage = false;
            saveDialogAsync("Save File", "Choose save location", "", "untitled.txt",
                [this](const FileDialogResult& result) {
                lastResult = result;
                if (result.success) {
                    statusMessage = "Save location: " + result.fileName;
                } else {
                    statusMessage = "Cancelled";
                }
            });
            break;
        }
    }
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'A': case 'a': triggerAction(0); break;
        case 'C': case 'c': triggerAction(1); break;
        case 'O': case 'o': triggerAction(2); break;
        case 'S': case 's': triggerAction(3); break;
    }
}

void tcApp::mousePressed(Vec2 pos, int button) {
    int hit = hitTest(pos.x, pos.y);
    if (hit >= 0) {
        triggerAction(hit);
    }
}
