// =============================================================================
// colorToolExample - Convert a hex color to TrussC's normalized RGB values.
//
// Run ./bin/colorToolExample ff8000 to print RGB values and exit (0 on success,
// 1 for invalid arguments). With no arguments, open a small GUI; Escape exits.
// main.cpp only forwards argc/argv; argument handling stays here because TrussC
// has no app argument accessor. Color::fromHex does the conversion without a
// window or runtime. The no-arguments GUI pattern also appears in
// tools/src/main.cpp, in trusscli's main(). See README.md for build commands.
// =============================================================================

#include "tcApp.h"
#include <charconv>
#include <iostream>
#include <string_view>

int tcApp::run(int argc, char* argv[]) {
    if (argc == 1) {
        WindowSettings settings;
        settings.setSize(640, 200);
        settings.setTitle("Color tool");
        return TC_RUN_APP(tcApp, settings);
    }

    uint32_t hex = 0;
    if (argc == 2) {
        const string_view text(argv[1]);
        const auto result = from_chars(text.data(), text.data() + text.size(), hex, 16);
        if (text.size() == 6 && result.ec == errc{} && result.ptr == text.data() + text.size()) {
            const Color color = Color::fromHex(hex);
            cout << color.r << ' ' << color.g << ' ' << color.b << '\n';
            return cout ? 0 : 1;
        }
    }
    cerr << "Usage: colorToolExample RRGGBB (six hex digits), or no arguments for GUI\n";
    return 1;
}

void tcApp::draw() {
    clear(Color::fromHex(0xff8000));
    setColor(0.0f);
    drawBitmapString("CLI: colorToolExample ff8000\nPrints: 1 0.501961 0\nEscape exits.", 20, 30);
}

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) exitApp();
}
