#include "tcApp.h"
#include <iostream>

using namespace std;

void tcApp::setup() {
    cout << "imageLoaderExample: Image Loading Demo" << endl;
    cout << "  - Loading multiple images" << endl;
    cout << "  - Drawing with different sizes" << endl;
    cout << "  - Reading pixel colors" << endl;

    // Load images
    if (!bikers.load(getDataPath("images/bikers.jpg"))) {
        cout << "Failed to load bikers.jpg" << endl;
    }

    if (!gears.load(getDataPath("images/gears.gif"))) {
        cout << "Failed to load gears.gif" << endl;
    }

    if (!poster.load(getDataPath("images/poster.jpg"))) {
        cout << "Failed to load poster.jpg" << endl;
    }

    if (!transparency.load(getDataPath("images/transparency.png"))) {
        cout << "Failed to load transparency.png" << endl;
    }

    if (!icon.load(getDataPath("images/icon.png"))) {
        cout << "Failed to load icon.png" << endl;
    }
}

void tcApp::update() {
    // Nothing to do
}

void tcApp::draw() {
    clear(1.0f);  // White background

    setColor(1.0f);  // White (display image colors as-is)

    // Draw bikers at top-left
    if (bikers.isAllocated()) {
        bikers.draw(0, 0);
    }

    // Draw gears at top-right
    if (gears.isAllocated()) {
        gears.draw(600, 0);
    }

    // Draw poster at bottom-right
    if (poster.isAllocated()) {
        poster.draw(600, 300, 200, 300);  // Specify size
    }

    // Draw transparency while moving
    if (transparency.isAllocated()) {
        float wave = sin(getElapsedTime());
        transparency.draw(500 + wave * 50, 20);
    }

    // Read icon pixels and draw circles
    if (icon.isAllocated()) {
        int w = icon.getWidth();
        int h = icon.getHeight();
        float diameter = 8;
        float offsetX = 20;
        float offsetY = 500;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Color c = icon.getColor(x, y);
                // Determine size based on brightness
                float brightness = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
                float size = (1.0f - brightness) * diameter / 2;

                setColor(c);
                drawCircle(offsetX + x * diameter, offsetY + y * diameter, size + 1);
            }
        }

        // Also display original icon
        setColor(1.0f);
        icon.draw(offsetX + w * diameter + 20, offsetY);
    }

    // Display information (with semi-transparent background)
    drawBitmapStringHighlight("imageLoaderExample", 10, 20,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
    drawBitmapStringHighlight("Loaded images: bikers.jpg, gears.gif, poster.jpg, transparency.png, icon.png", 10, 40,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
    drawBitmapStringHighlight("Bottom: pixel colors from icon.png visualized as circles", 10, 60,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
}
