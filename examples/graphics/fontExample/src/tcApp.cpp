#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setVsync(true);

    // システムフォントを使用
    std::string fontPath = "/System/Library/Fonts/Geneva.ttf";

    font.load(fontPath, 24);
    fontSmall.load(fontPath, 14);
    fontLarge.load(fontPath, 48);

    printf("Font loaded: size=%d, glyphs=%zu, memory=%zu bytes\n",
           font.getSize(),
           font.getLoadedGlyphCount(),
           font.getMemoryUsage());
}

void tcApp::draw() {
    clear(colors::white);

    float w = getWindowWidth();
    float h = getWindowHeight();

    // =========================================================================
    // TTF アラインメントデモ
    // =========================================================================
    setColor(40);

    // 水平アラインメント（画面上部）
    float y = 80;
    float centerX = w / 2;

    // 基準線を描画
    setColor(200);
    drawLine(centerX, 40, centerX, 150);
    drawLine(50, y, w - 50, y);

    setColor(40);
    font.drawString("Left", centerX, y, Left, Top);
    font.drawString("Center", centerX, y + 30, Center, Top);
    font.drawString("Right", centerX, y + 60, Right, Top);

    // 垂直アラインメント
    float x = 120;
    y = 220;

    // 基準線を描画（長くした）
    setColor(200);
    drawLine(50, y, w - 50, y);
    drawLine(x, y - 30, x, y + 30);

    setColor(40);
    font.drawString("Top", x, y, Left, Top);
    font.drawString("Center", x + 80, y, Left, Center);
    font.drawString("Bottom", x + 180, y, Left, Bottom);
    font.drawString("Baseline", x + 290, y, Left, Baseline);

    // =========================================================================
    // setLineHeight デモ（改行テキスト）
    // =========================================================================
    y = 310;
    std::string multiLine = "Line 1\nLine 2\nLine 3";

    // デフォルト行高さ
    setColor(100);
    drawBitmapString("Default (1.0em):", 50, y - 20);
    setColor(40);
    font.resetLineHeight();
    font.drawString(multiLine, 50, y);

    // em単位（狭め）
    setColor(100);
    drawBitmapString("0.8em:", 220, y - 20);
    setColor(40);
    font.setLineHeightEm(0.8);
    font.drawString(multiLine, 220, y);

    // em単位（広め）
    setColor(100);
    drawBitmapString("1.5em:", 350, y - 20);
    setColor(40);
    font.setLineHeightEm(1.5);
    font.drawString(multiLine, 350, y);

    // ピクセル指定
    setColor(100);
    drawBitmapString("50px:", 500, y - 20);
    setColor(40);
    font.setLineHeight(50);
    font.drawString(multiLine, 500, y);

    font.resetLineHeight();

    // =========================================================================
    // 中心点マーカー + Center,Center
    // =========================================================================
    float cx = w / 2;
    float cy = 520;

    // 十字マーカー
    setColor(colors::red);
    drawLine(cx - 20, cy, cx + 20, cy);
    drawLine(cx, cy - 20, cx, cy + 20);
    drawCircle(cx, cy, 5);

    // Center,Center でテキストを中央配置
    setColor(40);
    fontLarge.drawString("Centered!", cx, cy, Center, Center);

    // =========================================================================
    // BitmapFont アラインメントデモ
    // =========================================================================
    y = 620;

    setColor(200);
    drawLine(centerX, y - 10, centerX, y + 60);

    setColor(80);
    drawBitmapString("BitmapFont Left", centerX, y, Left, Top);
    drawBitmapString("BitmapFont Center", centerX, y + 18, Center, Top);
    drawBitmapString("BitmapFont Right", centerX, y + 36, Right, Top);

    // getBBox デモ
    y = 710;
    std::string boxText = "BoundingBox";
    Rectangle bbox = font.getBBox(boxText);

    // 描画位置
    float bx = 100, by = y;
    setColor(colors::lightBlue);
    drawRect(bx + bbox.x, by + bbox.y, bbox.width, bbox.height);
    setColor(40);
    font.drawString(boxText, bx, by);

    // 情報表示
    setColor(100);
    std::string info = "Glyphs: " + std::to_string(font.getLoadedGlyphCount());
    info += " | Memory: " + std::to_string(Font::getTotalCacheMemoryUsage() / 1024) + " KB";
    info += " | FPS: " + std::to_string((int)getFrameRate());
    drawBitmapString(info, 10, h - 20);
}
