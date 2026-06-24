#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("cursorExample");

    // All cursor types
    entries = {
        { Cursor::Default,    "Default" },
        { Cursor::Arrow,      "Arrow" },
        { Cursor::IBeam,      "IBeam" },
        { Cursor::Crosshair,  "Crosshair" },
        { Cursor::Hand,       "Hand" },
        { Cursor::ResizeEW,   "ResizeEW" },
        { Cursor::ResizeNS,   "ResizeNS" },
        { Cursor::ResizeNWSE, "ResizeNWSE" },
        { Cursor::ResizeNESW, "ResizeNESW" },
        { Cursor::ResizeAll,  "ResizeAll" },
        { Cursor::NotAllowed, "NotAllowed" },
        { Cursor::Custom0,    "Custom0 (Image file)" },
#ifndef __EMSCRIPTEN__
        { Cursor::Custom1,    "Custom1 (Fbo)" },
#endif
    };

    // --- Custom0: load image file ---
    cursorImage.load("cross_arrow.png");
    bindCursorImage(Cursor::Custom0, cursorImage,
                    cursorImage.getWidth() / 2, cursorImage.getHeight() / 2);

#ifndef __EMSCRIPTEN__
    // --- Custom1: draw into Fbo, then convert to Image ---
    // (Fbo readPixels is not yet supported on WebGPU)
    int sz = 32;
    fbo.allocate(sz, sz);
    fbo.begin();
    clear(0, 0, 0, 0);
    setColor(0.2f, 0.8f, 1.0f);
    pushMatrix();
    translate(sz / 2, sz / 2);
    for (int i = 0; i < 5; i++) {
        float a1 = TAU * i / 5.0f - QUARTER_TAU;
        float a2 = TAU * (i + 2) / 5.0f - QUARTER_TAU;
        drawLine(cosf(a1) * 12, sinf(a1) * 12, cosf(a2) * 12, sinf(a2) * 12);
    }
    setColor(1.0f, 1.0f, 0.3f);
    drawCircle(0, 0, 3);
    popMatrix();
    fbo.end();
    fbo.copyTo(fboImage);
    bindCursorImage(Cursor::Custom1, fboImage, sz / 2, sz / 2);
#endif
}

void tcApp::draw() {
    clear(0.15f);

    float mx = getMouseX();
    float my = getMouseY();

    float rowH = 30;
    float startY = 45;
    float boxW = 300;
    float startX = (getWidth() - boxW) / 2;

    // Title
    setColor(1.0f);
    drawBitmapString("=== Cursor Example ===", startX, 15);
    setColor(0.6f);
    drawBitmapString("Hover each row to change cursor", startX, 30);

    hoveredIndex = -1;

    for (int i = 0; i < (int)entries.size(); i++) {
        float y = startY + i * rowH;
        bool hovered = (mx >= startX && mx <= startX + boxW &&
                        my >= y && my <= y + rowH);
        if (hovered) hoveredIndex = i;

        setColor(hovered ? Color(0.3f, 0.3f, 0.4f) : Color(0.2f, 0.2f, 0.25f));
        drawRect(startX, y, boxW, rowH - 2);

        setColor(hovered ? Color(1.0f) : Color(0.7f));
        drawBitmapString(entries[i].name, startX + 15, y + 9);

        if (hovered) setCursor(entries[i].cursor);
    }

    if (hoveredIndex == -1) setCursor(Cursor::Default);

    // Preview custom cursors
    float previewY = startY + entries.size() * rowH + 15;
    setColor(0.6f);
    drawBitmapString("Custom cursor previews:", startX, previewY);
    previewY += 18;

    setColor(1.0f);
    cursorImage.draw(startX, previewY, 48, 48);
    drawBitmapString("Image file", startX, previewY + 52);

#ifndef __EMSCRIPTEN__
    fboImage.draw(startX + 100, previewY, 48, 48);
    drawBitmapString("Fbo->Image", startX + 100, previewY + 52);
#endif
}
