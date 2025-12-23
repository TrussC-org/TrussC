#include "TrussC.h"
#include "tcApp.h"
#include <sstream>
#include <algorithm>

// Get file name (last part of path)
static string getFileName(const string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos) return path;
    return path.substr(pos + 1);
}

// Get extension
static string getExtension(const string& path) {
    size_t pos = path.find_last_of('.');
    if (pos == string::npos) return "";
    string ext = path.substr(pos + 1);
    // Convert to lowercase
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// Check if image extension
static bool isImageExtension(const string& ext) {
    return ext == "png" || ext == "jpg" || ext == "jpeg" ||
           ext == "gif" || ext == "bmp" || ext == "tga";
}

void tcApp::setup() {
    setWindowTitle("dragDropExample");
}

void tcApp::draw() {
    clear(0.16f);

    int w = getWindowWidth();
    int h = getWindowHeight();

    // Title
    setColor(1.0f);
    drawBitmapString("=== Drag & Drop Demo ===", 20, 20);

    // Status message
    setColor(0.78f, 0.78f, 0.4f);
    drawBitmapString(statusMessage, 20, 50);

    // Drop area border (dashed style)
    setColor(0.4f);
    noFill();
    drawRect(10, 70, w - 20, h - 80);
    fill();

    // Display list of dropped files
    float y = 100;
    int maxDisplay = 10;  // Maximum display count
    int count = 0;

    for (const auto& file : droppedFiles) {
        if (count >= maxDisplay) {
            setColor(0.6f);
            drawBitmapString("... and more", 30, y);
            break;
        }

        // Icon
        if (file.isImage) {
            setColor(0.4f, 0.78f, 0.4f);  // Green for images
            drawBitmapString("[IMG]", 30, y);
        } else {
            setColor(0.4f, 0.6f, 0.78f);  // Blue for others
            drawBitmapString("[FILE]", 30, y);
        }

        // File name
        setColor(1.0f);
        drawBitmapString(file.name, 90, y);

        y += 20;
        count++;
    }

    // Image preview
    if (hasPreview && previewImage.isAllocated()) {
        float previewX = w - 250;
        float previewY = 100;
        float maxSize = 200;

        float imgW = previewImage.getWidth();
        float imgH = previewImage.getHeight();
        float scale = std::min(maxSize / imgW, maxSize / imgH);

        float drawW = imgW * scale;
        float drawH = imgH * scale;

        // Border
        setColor(0.3f);
        drawRect(previewX - 5, previewY - 5, drawW + 10, drawH + 10);

        // Image
        setColor(1.0f);
        previewImage.draw(previewX, previewY, drawW, drawH);

        // Size information
        setColor(0.6f);
        stringstream ss;
        ss << (int)imgW << " x " << (int)imgH;
        drawBitmapString(ss.str(), previewX, previewY + drawH + 15);
    }

    // Instructions
    setColor(0.47f);
    drawBitmapString("Drag and drop files onto this window", 20, h - 25);
}

void tcApp::filesDropped(const vector<string>& files) {
    // Clear list
    droppedFiles.clear();
    hasPreview = false;

    // Collect file information
    for (const auto& path : files) {
        DroppedFile df;
        df.path = path;
        df.name = getFileName(path);
        df.extension = getExtension(path);
        df.isImage = isImageExtension(df.extension);

        droppedFiles.push_back(df);

        // Load for preview if image (last image)
        if (df.isImage) {
            if (previewImage.load(path)) {
                hasPreview = true;
            }
        }
    }

    // Update status
    stringstream ss;
    ss << files.size() << " file(s) dropped";
    statusMessage = ss.str();
}
