#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    // Use system font
    std::string fontPath = "/System/Library/Fonts/Geneva.ttf";

    font.load(fontPath, 24);
    fontSmall.load(fontPath, 14);
    fontLarge.load(fontPath, 48);

    tcLogNotice("tcApp") << "Font loaded: size=" << font.getSize()
                         << ", glyphs=" << font.getLoadedGlyphCount()
                         << ", memory=" << font.getMemoryUsage() << " bytes";
}

void tcApp::draw() {
    clear(colors::white);

    float w = getWindowWidth();
    float h = getWindowHeight();

    // =========================================================================
    // TTF Alignment demo
    // =========================================================================
    setColor(0.16f);

    // Horizontal alignment (top of screen)
    // Use setTextAlign + pushStyle/popStyle for style management
    float y = 80;
    float centerX = w / 2;

    // Draw reference line
    setColor(0.78f);
    drawLine(centerX, 40, centerX, 150);
    drawLine(50, y, w - 50, y);

    pushStyle();
    setColor(0.16f);

    setTextAlign(Left, Top);
    font.drawString("Left", centerX, y);

    setTextAlign(Center, Top);
    font.drawString("Center", centerX, y + 30);

    setTextAlign(Right, Top);
    font.drawString("Right", centerX, y + 60);

    popStyle();

    // Vertical alignment
    float x = 120;
    y = 220;

    // Draw reference line (extended)
    setColor(0.78f);
    drawLine(50, y, w - 50, y);
    drawLine(x, y - 30, x, y + 30);

    setColor(0.16f);
    font.drawString("Top", x, y, Left, Top);
    font.drawString("Center", x + 80, y, Left, Center);
    font.drawString("Bottom", x + 180, y, Left, Bottom);
    font.drawString("Baseline", x + 290, y, Left, Baseline);

    // =========================================================================
    // setLineHeight demo (multiline text)
    // =========================================================================
    y = 310;
    std::string multiLine = "Line 1\nLine 2\nLine 3";

    // Default line height
    setColor(0.4f);
    drawBitmapString("Default (1.0em):", 50, y - 20);
    setColor(0.16f);
    font.resetLineHeight();
    font.drawString(multiLine, 50, y);

    // em unit (narrower)
    setColor(0.4f);
    drawBitmapString("0.8em:", 220, y - 20);
    setColor(0.16f);
    font.setLineHeightEm(0.8);
    font.drawString(multiLine, 220, y);

    // em unit (wider)
    setColor(0.4f);
    drawBitmapString("1.5em:", 350, y - 20);
    setColor(0.16f);
    font.setLineHeightEm(1.5);
    font.drawString(multiLine, 350, y);

    // Pixel specified
    setColor(0.4f);
    drawBitmapString("50px:", 500, y - 20);
    setColor(0.16f);
    font.setLineHeight(50);
    font.drawString(multiLine, 500, y);

    font.resetLineHeight();

    // =========================================================================
    // Center point marker + Center,Center
    // =========================================================================
    float cx = w / 2;
    float cy = 520;

    // Cross marker
    setColor(colors::red);
    drawLine(cx - 20, cy, cx + 20, cy);
    drawLine(cx, cy - 20, cx, cy + 20);
    drawCircle(cx, cy, 5);

    // Center text with Center,Center
    setColor(0.16f);
    fontLarge.drawString("Centered!", cx, cy, Center, Center);

    // =========================================================================
    // BitmapFont + setTextAlign / pushStyle demo
    // =========================================================================
    y = 620;

    setColor(0.78f);
    drawLine(centerX, y - 10, centerX, y + 60);

    // Use setTextAlign() to set default alignment
    // Use pushStyle/popStyle to save/restore styles
    pushStyle();
    setColor(0.3f);

    setTextAlign(Left, Top);
    drawBitmapString("BitmapFont Left", centerX, y);

    setTextAlign(Center, Top);
    drawBitmapString("BitmapFont Center", centerX, y + 18);

    setTextAlign(Right, Top);
    drawBitmapString("BitmapFont Right", centerX, y + 36);

    popStyle();  // Restores color and alignment

    // getBBox demo
    y = 710;
    std::string boxText = "BoundingBox";
    Rect bbox = font.getBBox(boxText);

    // Draw position
    float bx = 100, by = y;
    setColor(colors::lightBlue);
    drawRect(bx + bbox.x, by + bbox.y, bbox.width, bbox.height);
    setColor(0.16f);
    font.drawString(boxText, bx, by);

    // Info display
    setColor(0.4f);
    std::string info = "Glyphs: " + std::to_string(font.getLoadedGlyphCount());
    info += " | Memory: " + std::to_string(Font::getTotalCacheMemoryUsage() / 1024) + " KB";
    info += " | FPS: " + std::to_string((int)getFrameRate());
    drawBitmapString(info, 10, h - 20);
}
