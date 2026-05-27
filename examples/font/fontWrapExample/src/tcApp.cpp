#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);
    fontH.load(TC_FONT_SANS_JA, 20);
    fontHen.load(TC_FONT_SANS, 18);
    fontV.load(TC_FONT_SANS_JA, 22);
    fontV.setWritingMode(WritingMode::VerticalRL);
    fontLabel.load(TC_FONT_SANS, 13);
}

void tcApp::draw() {
    clear(colors::white);
    const float W = getWindowWidth();
    const float H = getWindowHeight();

    setColor(0.18f);
    fontH.drawString("Line-wrap demo", 24, 24, Left, Top);
    setColor(0.55f);
    fontLabel.drawString(
        "Top: horizontal wrap with varying kinsoku / hyphenation modes. "
        "Bottom: same wrap API applied to vertical text (maxLineLength = column height).",
        24, 50, Left, Top);

    // ====================================================
    //  Horizontal wrap row — 4 panels
    // ====================================================
    const float hY = 86;
    const float hH = 260;
    const float hPanelW = (W - 80) / 4.f;
    const float wrapAt  = hPanelW - 36;

    const string longJa =
        "TrussCはoFに似たC++のクリエイティブコーディング・"
        "フレームワークです、よろしくね。";
    const string longEn =
        "TrussC is a lightweight creative-coding framework "
        "inspired by openFrameworks and built on sokol.";

    auto drawHPanel = [&](int idx, const char* label, Font& f, const string& txt) {
        float px = 40 + hPanelW * idx;
        setColor(0.95f);
        drawRect(px, hY, hPanelW - 16, hH);
        // wrap-edge guideline
        setColor(0.83f);
        drawLine(px + 16 + wrapAt, hY + 6, px + 16 + wrapAt, hY + hH - 6);
        setColor(0.10f);
        f.drawString(txt, px + 16, hY + 16, Left, Top);
        setColor(0.35f);
        fontLabel.drawString(label, px + (hPanelW - 16) / 2,
                             hY + hH + 6, Center, Top);
    };

    // (A) Wrap basic — Kinsoku Off (no 行頭禁則)
    Font fH_b = fontH;
    fH_b.enableWrap(true);
    fH_b.setMaxLineLength(wrapAt);
    drawHPanel(0, "Wrap (kinsoku off)", fH_b, longJa);

    // (B) Wrap + kinsoku punctuation-only
    Font fH_kp = fontH;
    fH_kp.enableWrap(true);
    fH_kp.setMaxLineLength(wrapAt);
    fH_kp.setKinsoku(KinsokuLevel::PunctuationOnly);
    fH_kp.setHangingPunctuation(true);
    drawHPanel(1, "Kinsoku PunctOnly + hang", fH_kp, longJa);

    // (C) English hyphenation
    Font fH_en = fontHen;
    fH_en.enableWrap(true);
    fH_en.setMaxLineLength(wrapAt);
    fH_en.setLatinHyphenation(true);
    drawHPanel(2, "EN wrap + hyphenation", fH_en, longEn);

    // (D) No wrap — overflows. Placed last so it spills into empty space at
    // the right of the window instead of obscuring the wrapped samples.
    Font fH_no = fontH;
    drawHPanel(3, "No wrap (overflows)", fH_no, longJa);

    // ====================================================
    //  Vertical wrap row — 3 panels
    // ====================================================
    const float vY = hY + hH + 50;
    const float vH = H - vY - 40;
    const float vPanelW = (W - 80) / 3.f;
    // Tight column budget so wrap points land on kinsoku-sensitive chars
    // (、 。 」 ）). Without this, breaks fall on regular characters and the
    // off / on panels render identically.
    const float colBudget = 8.f * 22.f;  // ~8 cells at fontV's 22pt size

    const string longJaV =
        "日本発、「縦書き」可能なフレームワーク。\n"
        "《商用利用》可能（MITライセンス）。";

    auto drawVPanel = [&](int idx, const char* label, Font& f, const string& txt) {
        float px = 40 + vPanelW * idx;
        setColor(0.95f);
        drawRect(px, vY, vPanelW - 16, vH);
        float anchorX = px + vPanelW - 32;
        float anchorY = vY + 16;
        setColor(0.10f);
        f.drawString(txt, anchorX, anchorY, Right, Top);
        setColor(colors::red);
        drawCircle(anchorX, anchorY, 3);
        // column-budget guideline (bottom of the budget region)
        setColor(0.83f);
        drawLine(px + 12, anchorY + colBudget,
                 px + vPanelW - 24, anchorY + colBudget);
        setColor(0.35f);
        fontLabel.drawString(label, px + (vPanelW - 16) / 2,
                             vY + vH + 6, Center, Top);
    };

    // (V-A) Vertical no-wrap — overflows below panel
    Font fV_no = fontV;
    drawVPanel(0, "Vertical no wrap (overflows)", fV_no, longJaV);

    // (V-B) Vertical wrap, kinsoku off
    Font fV_b = fontV;
    fV_b.enableWrap(true);
    fV_b.setMaxLineLength(colBudget);
    drawVPanel(1, "Vertical wrap (kinsoku off)", fV_b, longJaV);

    // (V-C) Vertical wrap, kinsoku standard + hanging
    Font fV_k = fontV;
    fV_k.enableWrap(true);
    fV_k.setMaxLineLength(colBudget);
    fV_k.setKinsoku(KinsokuLevel::Standard);
    fV_k.setHangingPunctuation(true);
    drawVPanel(2, "Vertical wrap + kinsoku + hang", fV_k, longJaV);

    // Footer
    setColor(0.5f);
    fontLabel.drawString(
        string("FPS: ") + to_string((int)getFrameRate()) +
        "   |   Cache: " + to_string(Font::getTotalCacheMemoryUsage() / 1024) + " KB",
        24, H - 22, Left, Top);
}
