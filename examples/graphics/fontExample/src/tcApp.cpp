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

    // 垂直アラインメント（画面左側）
    float x = 150;
    y = 250;

    // 基準線を描画
    setColor(200);
    drawLine(50, y, 250, y);
    drawLine(x, y - 50, x, y + 50);

    setColor(40);
    font.drawString("Top", x, y, Left, Top);
    font.drawString("Center", x + 100, y, Left, Center);
    font.drawString("Bottom", x + 200, y, Left, Bottom);
    font.drawString("Baseline", x + 330, y, Left, Baseline);

    // =========================================================================
    // 中心点マーカー + 全組み合わせ
    // =========================================================================
    float cx = w / 2;
    float cy = 400;

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
    y = 550;

    setColor(200);
    drawLine(centerX, y - 10, centerX, y + 80);

    setColor(80);
    drawBitmapString("BitmapFont Left", centerX, y, Left, Top);
    drawBitmapString("BitmapFont Center", centerX, y + 20, Center, Top);
    drawBitmapString("BitmapFont Right", centerX, y + 40, Right, Top);

    // getBBox デモ
    y = 680;
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
