#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "tc/graphics/tcFont.h"

// =============================================================================
// tcApp - TrueType フォント＆アラインメントサンプル
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    Font font;
    Font fontSmall;
    Font fontLarge;

    std::string testText = "Hello, TrussC!";
};
