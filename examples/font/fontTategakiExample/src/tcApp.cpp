#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    fontH.load(TC_FONT_JP, 24);

    fontV.load(TC_FONT_JP, 24);
    fontV.setWritingMode(WritingMode::VerticalRL);
    // Defaults: Latin = Rotate, digits = Rotate (tcyDigitMax_ = 0)

    fontVcombineDigit.load(TC_FONT_JP, 24);
    fontVcombineDigit.setWritingMode(WritingMode::VerticalRL);
    // 1-2 digit runs → Combine (squeezed into one cell); 3+ → Rotate
    fontVcombineDigit.setTcyDigits(2, TcyMode::Combine, TcyMode::Rotate);

    fontVupright.load(TC_FONT_JP, 24);
    fontVupright.setWritingMode(WritingMode::VerticalRL);
    fontVupright.setTcyLatin(TcyMode::Upright);
    fontVupright.setTcyDigits(99, TcyMode::Upright, TcyMode::Upright);

    fontLabel.load(TC_FONT_SANS, 13);

    logNotice("tcApp") << "Fonts loaded; vertical glyphs: "
                       << fontV.getLoadedGlyphCount();
}

void tcApp::draw() {
    clear(colors::white);

    const float W = getWindowWidth();
    const float H = getWindowHeight();

    // ---- Title ----
    setColor(0.18f);
    fontH.drawString("縦書き (tategaki) demo", 24, 24, Left, Top);

    setColor(0.55f);
    fontLabel.drawString(
        "Default vertical text: CJK upright, Latin/digit runs rotated 90 CW. "
        "Brackets & punctuation use Unicode vertical-form glyphs (FE10-FE4F).",
        24, 56, Left, Top);

    // ---- 4-panel layout ----
    const float panelTopY = 110;
    const float panelHeight = H - panelTopY - 60;
    const float panelW = (W - 80) / 4.f;

    auto drawPanel = [&](int idx, const char* label, Font& f, const string& txt) {
        float panelX = 40 + panelW * idx;
        // Panel frame
        setColor(0.95f);
        drawRect(panelX, panelTopY, panelW - 16, panelHeight);

        // Vertical text — anchor: top-right corner of panel, slightly inset.
        float anchorX = panelX + panelW - 32;
        float anchorY = panelTopY + 24;
        setColor(0.10f);
        f.drawString(txt, anchorX, anchorY, Right, Top);

        // Anchor marker
        setColor(colors::red);
        drawCircle(anchorX, anchorY, 3);

        // Label below panel
        setColor(0.35f);
        fontLabel.drawString(label, panelX + (panelW - 16) / 2,
                             panelTopY + panelHeight + 14, Center, Top);
    };

    drawPanel(0, "CJK + brackets/punct", fontV,
              "日本発、「縦書き」可能なフレームワーク（プログラミングの基盤となる技術）。"
              "開発者は著書『TrussCをはじめよう』を《MITライセンス》で公開。");

    drawPanel(1, "Latin = Rotate (default)", fontV,
              "TrussCで縦書きAPIを実装中、2026年5月。");

    drawPanel(2, "TCY Combine (digits<=2)", fontVcombineDigit,
              "令和9年6月28日に出版された150ページの本");

    drawPanel(3, "Upright Latin/digits", fontVupright,
              "ABCあいう\n123かきく");

    // Footer info
    setColor(0.5f);
    fontLabel.drawString(
        string("Glyphs loaded: ") + to_string(fontV.getLoadedGlyphCount()) +
        "   |   Cache memory: " + to_string(Font::getTotalCacheMemoryUsage() / 1024) + " KB" +
        "   |   FPS: " + to_string((int)getFrameRate()),
        24, H - 22, Left, Top);
}
