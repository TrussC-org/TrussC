#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

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
