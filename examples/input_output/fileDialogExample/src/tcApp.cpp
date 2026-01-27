// =============================================================================
// tcApp.cpp - File Dialog Sample
// =============================================================================

#include "tcApp.h"

using namespace std;

void tcApp::setup() {
    logNotice("tcApp") << "=== File Dialog Example ===";
    logNotice("tcApp") << "O: Open file dialog";
    logNotice("tcApp") << "F: Open folder dialog";
    logNotice("tcApp") << "S: Save dialog";
    logNotice("tcApp") << "A: Alert dialog";
    logNotice("tcApp") << "===========================";
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.16f);

    float y = 40;

    // Title
    setColor(1.0f);
    drawBitmapString("File Dialog Example", 40, y);
    y += 30;

    // Instructions
    setColor(0.7f);
    drawBitmapString("O: Open file   F: Open folder   S: Save   A: Alert", 40, y);
    y += 40;

    // Status
    setColor(0.4f, 0.78f, 1.0f);
    drawBitmapString("Status: " + statusMessage, 40, y);
    y += 40;

    // Display result
    if (lastResult.success) {
        setColor(0.4f, 1.0f, 0.4f);
        drawBitmapString("Success!", 40, y);
        y += 25;

        setColor(0.86f);
        drawBitmapString("File: " + lastResult.fileName, 40, y);
        y += 20;
        drawBitmapString("Path: " + lastResult.filePath, 40, y);
        y += 40;

        // Display if image is loaded
        if (hasImage && loadedImage.isAllocated()) {
            setColor(1.0f);
            drawBitmapString("Loaded Image:", 40, y);
            y += 25;

            // Display image at appropriate size
            float maxW = getWindowWidth() - 80;
            float maxH = getWindowHeight() - y - 40;
            float imgW = loadedImage.getWidth();
            float imgH = loadedImage.getHeight();
            float scale = std::min<float>(maxW / imgW, maxH / imgH);
            if (scale > 1.0f) scale = 1.0f;

            loadedImage.draw(40, y, imgW * scale, imgH * scale);
        }
    } else if (!lastResult.filePath.empty()) {
        setColor(1.0f, 0.4f, 0.4f);
        drawBitmapString("Cancelled", 40, y);
    }
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'O':
        case 'o': {
            // File selection dialog
            statusMessage = "Opening file dialog...";
            lastResult = loadDialog("Select a file", "");
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "File selected";
                logNotice("tcApp") << "Selected: " << lastResult.filePath;

                // Try to load if image file
                string path = lastResult.filePath;
                if (path.find(".png") != string::npos ||
                    path.find(".jpg") != string::npos ||
                    path.find(".jpeg") != string::npos ||
                    path.find(".PNG") != string::npos ||
                    path.find(".JPG") != string::npos ||
                    path.find(".JPEG") != string::npos) {
                    if (loadedImage.load(path)) {
                        hasImage = true;
                        logNotice("tcApp") << "Image loaded: " << loadedImage.getWidth() << "x" << loadedImage.getHeight();
                    }
                }
            } else {
                statusMessage = "File dialog cancelled";
            }
            break;
        }

        case 'F':
        case 'f': {
            // Folder selection dialog
            statusMessage = "Opening folder dialog...";
            lastResult = loadDialog("Select a folder", "", "", true);
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "Folder selected";
                logNotice("tcApp") << "Selected folder: " << lastResult.filePath;
            } else {
                statusMessage = "Folder dialog cancelled";
            }
            break;
        }

        case 'S':
        case 's': {
            // Save dialog
            statusMessage = "Opening save dialog...";
            lastResult = saveDialog("Save File", "Save your file", "", "untitled.txt");
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "Save location selected";
                logNotice("tcApp") << "Save to: " << lastResult.filePath;
            } else {
                statusMessage = "Save dialog cancelled";
            }
            break;
        }

        case 'A':
        case 'a': {
            // Alert dialog
            statusMessage = "Showing alert...";
            alertDialog("Alert", "This is a test alert from TrussC!");
            statusMessage = "Alert closed";
            break;
        }
    }
}
