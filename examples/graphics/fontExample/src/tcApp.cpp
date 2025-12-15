#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    tc::setVsync(true);

    // システムフォントを使用（.ttf ファイル）
    // .ttc (TrueType Collection) は stb_truetype で追加処理が必要なので .ttf を使用
    std::string fontPath = "/System/Library/Fonts/Geneva.ttf";

    // 複数サイズでロード（SharedFontCache で共有される）
    font.load(fontPath, 24);
    fontSmall.load(fontPath, 14);
    fontLarge.load(fontPath, 48);

    printf("Font loaded: size=%d, glyphs=%zu, memory=%zu bytes\n",
           font.getSize(),
           font.getLoadedGlyphCount(),
           font.getMemoryUsage());
}

void tcApp::draw() {
    tc::clear(tc::colors::white);
    tc::setColor(40);

    float y = 50;

    // 大きいフォント
    if (fontLarge.isLoaded()) {
        fontLarge.drawString("TrussC Font", 50, y);
        y += fontLarge.getLineHeight() + 20;
    }

    // 通常フォント
    if (font.isLoaded()) {
        font.drawString(testTextEn, 50, y);
        y += font.getLineHeight() + 10;

        // 日本語テキスト（Geneva.ttf には日本語グリフがないので豆腐になる）
        // font.drawString(testTextJp, 50, y);
        // y += font.getLineHeight() + 10;

        // 複数行
        font.drawString("Line 1\nLine 2\nLine 3", 50, y);
        y += font.getLineHeight() * 3 + 20;
    }

    // 小さいフォント
    if (fontSmall.isLoaded()) {
        fontSmall.drawString("Small text: The quick brown fox jumps over the lazy dog.", 50, y);
        y += fontSmall.getLineHeight() + 20;
    }

    // メモリ使用量を表示
    tc::setColor(100);
    std::string info = "Glyphs: " + std::to_string(font.getLoadedGlyphCount());
    info += " | Memory: " + std::to_string(tc::Font::getTotalCacheMemoryUsage() / 1024) + " KB";
    info += " | FPS: " + std::to_string((int)tc::getFrameRate());
    tc::drawBitmapString(info, 50, 730);
}
