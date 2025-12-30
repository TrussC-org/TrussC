#include "tcApp.h"

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    logNotice("tcApp") << "colorExample: Color Space Demo";
    logNotice("tcApp") << "  - Space: Switch mode";
    logNotice("tcApp") << "  - ESC: Exit";
    logNotice("tcApp") << "";
    logNotice("tcApp") << "Modes:";
    logNotice("tcApp") << "  0: Lerp comparison (RGB/Linear/HSB/OKLab/OKLCH)";
    logNotice("tcApp") << "  1: Hue wheel (HSB vs OKLCH)";
    logNotice("tcApp") << "  2: Lightness uniformity (OKLab feature)";
    logNotice("tcApp") << "  3: Gradient comparison";

    // Initialize ImGui
    imguiSetup();
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void tcApp::update() {
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.15f, 0.15f, 0.18f);

    switch (mode_) {
        case 0: drawLerpComparison(); break;
        case 1: drawHueWheel(); break;
        case 2: drawLightnessDemo(); break;
        case 3: drawGradientDemo(); break;
    }

    // ImGui
    imguiBegin();
    drawGui();
    imguiEnd();
}

// ---------------------------------------------------------------------------
// cleanup
// ---------------------------------------------------------------------------
void tcApp::cleanup() {
    imguiShutdown();
}

// ---------------------------------------------------------------------------
// ImGui panel
// ---------------------------------------------------------------------------
void tcApp::drawGui() {
    // Fixed position and size for GUI panel
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(260, getWindowHeight() - 40), ImGuiCond_Always);

    ImGui::Begin("Color Settings", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    // Mode selector
    ImGui::SetNextItemWidth(-1);
    const char* modeNames[] = {
        "Lerp Comparison",
        "Hue Wheel",
        "Lightness Demo",
        "Gradient Demo"
    };
    ImGui::Combo("##mode", &mode_, modeNames, NUM_MODES);
    ImGui::Separator();

    // Inline color pickers
    ImGui::Text("Start Color");
    ImGui::ColorPicker3("##start", color1_,
        ImGuiColorEditFlags_NoSidePreview |
        ImGuiColorEditFlags_NoSmallPreview |
        ImGuiColorEditFlags_PickerHueBar);

    ImGui::Spacing();
    ImGui::Text("End Color");
    ImGui::ColorPicker3("##end", color2_,
        ImGuiColorEditFlags_NoSidePreview |
        ImGuiColorEditFlags_NoSmallPreview |
        ImGuiColorEditFlags_PickerHueBar);

    ImGui::Separator();
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Lerp method comparison
// ---------------------------------------------------------------------------
void tcApp::drawLerpComparison() {
    // Color definition methods:
    //   Color c = Color(1.0f, 0.5f, 0.0f);           // float (0-1)
    //   Color c = Color::fromBytes(255, 127, 0);    // int (0-255)
    //   Color c = Color::fromHex(0xFF7F00);         // HEX code
    //   Color c = colors::orange;                   // Predefined color

    // Use colors from ImGui color picker
    Color c1(color1_[0], color1_[1], color1_[2], color1_[3]);
    Color c2(color2_[0], color2_[1], color2_[2], color2_[3]);

    // Layout (avoid GUI panel on left)
    float margin = 20;
    float guiWidth = 280;  // GUI panel width + margin
    float startX = guiWidth + margin;
    float endX = getWindowWidth() - margin;
    float barHeight = 45;

    // Vertically center the content
    float totalHeight = 5 * barHeight + 4 * 40;  // 5 bars + 4 gaps (including labels)
    float y = (getWindowHeight() - totalHeight) / 2;
    float gap = 80;
    int steps = 256;

    const char* labels[] = {
        "lerpRGB (sRGB space - not recommended)",
        "lerpLinear (Linear space - physically correct)",
        "lerpHSB (HSB space)",
        "lerpOKLab (OKLab space - default)",
        "lerpOKLCH (OKLCH space - preserves hue)"
    };

    for (int mode = 0; mode < 5; mode++) {
        // Draw gradient bar
        float stepWidth = (endX - startX) / steps;

        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c;

            switch (mode) {
                case 0: c = c1.lerpRGB(c2, t); break;
                case 1: c = c1.lerpLinear(c2, t); break;
                case 2: c = c1.lerpHSB(c2, t); break;
                case 3: c = c1.lerpOKLab(c2, t); break;
                case 4: c = c1.lerpOKLCH(c2, t); break;
            }

            setColor(c);
            drawRect(startX + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        // Label
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(labels[mode], startX, y + barHeight + 8);

        y += gap;
    }
}

// ---------------------------------------------------------------------------
// Hue wheel HSB vs OKLCH
// ---------------------------------------------------------------------------
void tcApp::drawHueWheel() {
    // Layout (avoid GUI panel on left)
    float margin = 20;
    float guiWidth = 280;
    float availWidth = getWindowWidth() - guiWidth - margin * 2;
    float centerX1 = guiWidth + margin + availWidth * 0.25f;
    float centerX2 = guiWidth + margin + availWidth * 0.75f;
    float centerY = getWindowHeight() / 2;
    float radius = std::min(availWidth * 0.22f, (getWindowHeight() - margin * 4) * 0.42f);
    int segments = 360;

    // HSB hue wheel
    for (int i = 0; i < segments; i++) {
        float angle1 = (float)i / segments * TAU;
        float angle2 = (float)(i + 1) / segments * TAU;

        // HSB: Use hue directly
        Color c = ColorHSB(angle1, 1.0f, 1.0f).toRGB();
        setColor(c);

        // Draw fan shape
        float x1 = centerX1 + cos(angle1) * radius;
        float y1 = centerY + sin(angle1) * radius;
        float x2 = centerX1 + cos(angle2) * radius;
        float y2 = centerY + sin(angle2) * radius;
        drawTriangle(centerX1, centerY, x1, y1, x2, y2);
    }

    // OKLCH hue wheel
    for (int i = 0; i < segments; i++) {
        float angle1 = (float)i / segments * TAU;
        float angle2 = (float)(i + 1) / segments * TAU;

        // OKLCH: Equalize saturation with L=0.7, C=0.15
        Color c = ColorOKLCH(0.7f, 0.15f, angle1).toRGB().clamped();
        setColor(c);

        float x1 = centerX2 + cos(angle1) * radius;
        float y1 = centerY + sin(angle1) * radius;
        float x2 = centerX2 + cos(angle2) * radius;
        float y2 = centerY + sin(angle2) * radius;
        drawTriangle(centerX2, centerY, x1, y1, x2, y2);
    }

    // Label (semi-transparent black background)
    drawBitmapStringHighlight("HSB", centerX1 - 12, centerY - 6,
        Color(0, 0, 0, 0.5f), Color(1, 1, 1));
    drawBitmapStringHighlight("OKLCH", centerX2 - 20, centerY - 6,
        Color(0, 0, 0, 0.5f), Color(1, 1, 1));
}

// ---------------------------------------------------------------------------
// Lightness uniformity demo
// ---------------------------------------------------------------------------
void tcApp::drawLightnessDemo() {
    // Layout (avoid GUI panel on left)
    float margin = 20;
    float guiWidth = 280;
    float startX = guiWidth + margin;
    float barWidth = getWindowWidth() - startX - margin;
    float barHeight = 45;
    int segments = 360;
    float segmentWidth = barWidth / segments;

    // Vertically center the 4 bars with more padding
    float gap = 70;
    float totalHeight = 4 * barHeight + 3 * gap;
    float baseY = (getWindowHeight() - totalHeight) / 2 + 10;

    // HSB: Same brightness (B=1) but perceptual lightness varies
    float y1 = baseY;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorHSB(hue, 1.0f, 1.0f).toRGB();
        setColor(c);
        drawRect(startX + i * segmentWidth, y1, segmentWidth + 1, barHeight);
    }

    // Convert HSB to grayscale to check lightness
    float y2 = baseY + barHeight + gap;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorHSB(hue, 1.0f, 1.0f).toRGB();
        // Luminance calculation (sRGB)
        float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        setColor(luma, luma, luma);
        drawRect(startX + i * segmentWidth, y2, segmentWidth + 1, barHeight);
    }

    // OKLCH: Perceptually uniform lightness with same L
    float y3 = baseY + 2 * (barHeight + gap);
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorOKLCH(0.7f, 0.15f, hue).toRGB().clamped();
        setColor(c);
        drawRect(startX + i * segmentWidth, y3, segmentWidth + 1, barHeight);
    }

    // Convert OKLCH to grayscale
    float y4 = baseY + 3 * (barHeight + gap);
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorOKLCH(0.7f, 0.15f, hue).toRGB().clamped();
        float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        setColor(luma, luma, luma);
        drawRect(startX + i * segmentWidth, y4, segmentWidth + 1, barHeight);
    }

    // Labels
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("HSB (B=1.0, S=1.0)", startX, y1 - 20);
    drawBitmapString("HSB -> Grayscale", startX, y2 - 20);
    drawBitmapString("OKLCH (L=0.7, C=0.15)", startX, y3 - 20);
    drawBitmapString("OKLCH -> Grayscale", startX, y4 - 20);
}

// ---------------------------------------------------------------------------
// Gradient comparison
// ---------------------------------------------------------------------------
void tcApp::drawGradientDemo() {
    // Compare with multiple color pairs
    struct ColorPair {
        Color c1, c2;
        const char* name;
    };

    // Use custom colors from ImGui
    Color customC1(color1_[0], color1_[1], color1_[2], color1_[3]);
    Color customC2(color2_[0], color2_[1], color2_[2], color2_[3]);

    ColorPair pairs[] = {
        { customC1, customC2, "Custom (from picker)" },
        { colors::red, colors::blue, "Red -> Blue" },
        { colors::yellow, colors::magenta, "Yellow -> Magenta" },
        { Color(0.2f, 0.8f, 0.2f), Color(0.8f, 0.2f, 0.8f), "Green -> Purple" },
    };

    // Layout (avoid GUI panel on left)
    float margin = 20;
    float guiWidth = 280;
    float availWidth = getWindowWidth() - guiWidth - margin * 2;
    float startX = guiWidth + margin;
    float barWidth = availWidth * 0.45f;
    float barHeight = 18;
    int steps = 64;
    float stepWidth = barWidth / steps;

    // Vertically center with tighter spacing
    float pairGap = 30;  // Gap between two bars in a pair
    float groupGap = 55; // Gap between pairs
    float totalHeight = 4 * (barHeight * 2 + pairGap) + 3 * groupGap;
    float y = (getWindowHeight() - totalHeight) / 2 + 15;
    float colGap = availWidth * 0.5f;

    for (int p = 0; p < 4; p++) {
        auto& pair = pairs[p];

        // Left column: OKLab (default)
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpOKLab(pair.c2, t);
            setColor(c);
            drawRect(startX + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        // Right column: RGB comparison
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpRGB(pair.c2, t);
            setColor(c);
            drawRect(startX + colGap + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        y += pairGap;

        // HSB
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpHSB(pair.c2, t);
            setColor(c);
            drawRect(startX + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        // OKLCH
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpOKLCH(pair.c2, t);
            setColor(c);
            drawRect(startX + colGap + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        y += barHeight + groupGap;
    }

    // Legend
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("OKLab / HSB", startX, 25);
    drawBitmapString("RGB / OKLCH", startX + colGap, 25);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == KEY_SPACE) {
        mode_ = (mode_ + 1) % NUM_MODES;
        logNotice("tcApp") << "Mode: " << mode_;
    }
}
