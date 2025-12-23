// =============================================================================
// imageLoaderExample - Image Loading Demo
// =============================================================================
// Demonstrates loading and displaying images.
//
// Supported formats (via stb_image):
//   - JPEG (.jpg, .jpeg)
//   - PNG (.png) - with alpha transparency
//   - GIF (.gif) - static only, first frame
//   - BMP (.bmp)
//   - TGA (.tga)
//   - PSD (.psd) - composited view only
//   - HDR (.hdr) - radiance format
//   - PIC (.pic)
//   - PNM (.ppm, .pgm)
//
// Note: TIFF is NOT supported.
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // Load images from data/images folder
    if (!tower.load(getDataPath("images/transmission_tower.jpg"))) {
        tcLogWarning("tcApp") << "Failed to load transmission_tower.jpg";
    }

    if (!transparency.load(getDataPath("images/transparency.png"))) {
        tcLogWarning("tcApp") << "Failed to load transparency.png";
    }
}

void tcApp::draw() {
    clear(100);

    setColor(1.0f);

    // Draw tower centered
    if (tower.isAllocated()) {
        float x = (getWindowWidth() - tower.getWidth()) / 2;
        float y = (getWindowHeight() - tower.getHeight()) / 2;
        tower.draw(x, y);
    }

    // Draw transparency image swaying left/right
    if (transparency.isAllocated()) {
        float wave = sin(getElapsedTime() * 2) * 50;
        float x = (getWindowWidth() - transparency.getWidth()) / 2 + wave;
        float y = 20;
        transparency.draw(x, y);
    }
}
