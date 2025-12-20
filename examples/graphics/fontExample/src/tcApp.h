#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;
#include "tc/graphics/tcFont.h"

// =============================================================================
// tcApp - TrueType font & alignment sample
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
