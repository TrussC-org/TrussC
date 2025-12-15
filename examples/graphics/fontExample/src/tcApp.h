#pragma once

#include "tcBaseApp.h"
#include "tc/graphics/tcFont.h"

// =============================================================================
// tcApp - TrueType フォントサンプル
// =============================================================================

class tcApp : public tc::App {
public:
    void setup() override;
    void draw() override;

private:
    tc::Font font;
    tc::Font fontSmall;
    tc::Font fontLarge;

    // 日本語テスト用（フォントが対応していれば）
    std::string testTextJp = "こんにちは世界！";
    std::string testTextEn = "Hello, TrussC!";
};
