#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "tc/graphics/tcFont.h"

// =============================================================================
// tcApp - TrueType フォントサンプル
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    Font font;
    Font fontSmall;
    Font fontLarge;

    // 日本語テスト用（フォントが対応していれば）
    std::string testTextJp = "こんにちは世界！";
    std::string testTextEn = "Hello, TrussC!";
};
