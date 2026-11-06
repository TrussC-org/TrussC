// #916: bitmap string bounds, height and drawn position must agree.
//
// Headless part (always runs): getBitmapStringBounds / Width / Height follow
// setBitmapLineHeight, and the vertical alignment offsets use the whole text
// height. Pixel part (--gpu-check, Xvfb works on Linux): draws into an Fbo and
// compares the lit glyph pixels with the measured size, for the plain draw,
// the Direction overload, the scale overload and drawBitmapStringHighlight.
// `--gpu-check --dump <dir>` also writes one PNG per case, for before/after
// comparisons (see tools usage in core/tests/renderRegression/README.md).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
bool completed = false;
filesystem::path dumpDir;

void check(const string& name, bool ok) {
    printf("%-72s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

const vector<string> texts = {"Hi", "Hi\nthere", "A\nBB\nCCC"};
// Even values only, so that half a height stays an integer pixel count.
const vector<float> lineHeights = {16.0f, 20.0f, 26.0f, 10.0f};
const Direction hs[] = {Direction::Left, Direction::Center, Direction::Right};
const Direction vs[] = {Direction::Top, Direction::Center, Direction::Bottom, Direction::Baseline};

const char* name(Direction d) {
    switch (d) {
        case Direction::Left: return "L";
        case Direction::Center: return "C";
        case Direction::Right: return "R";
        case Direction::Top: return "T";
        case Direction::Bottom: return "B";
        case Direction::Baseline: return "Base";
        default: return "?";
    }
}

int lineCount(const string& text) {
    int n = 1;
    for (char c : text) if (c == '\n') ++n;
    return n;
}

// The documented offset from the anchor to the text's top-left corner, built
// from the measured size only (independent of the drawing code).
Vec2 expectedOffset(const string& text, Direction h, Direction v) {
    float w, hgt;
    getBitmapStringBounds(text, w, hgt);
    Vec2 o;
    o.x = h == Direction::Left ? 0 : h == Direction::Center ? -w / 2 : -w;
    switch (v) {
        case Direction::Top: o.y = 0; break;
        case Direction::Center: o.y = -hgt / 2; break;
        case Direction::Bottom: o.y = -hgt; break;
        default: o.y = -(float)bitmapfont::CHAR_HEIGHT; break; // first line's baseline
    }
    return o;
}

void headlessChecks() {
    const float savedLineHeight = getBitmapLineHeight();
    for (float lh : lineHeights) {
        setBitmapLineHeight(lh);
        for (const string& text : texts) {
            const string tag = "lh " + to_string((int)lh) + " lines " + to_string(lineCount(text)) + ": ";
            float w = -1, h = -1;
            getBitmapStringBounds(text, w, h);
            const float expectedH = bitmapfont::CHAR_TEX_HEIGHT + (lineCount(text) - 1) * lh;
            check(tag + "bounds height follows the line height", h == expectedH);
            check(tag + "bounds height == getBitmapStringHeight", h == getBitmapStringHeight(text));
            check(tag + "bounds width == getBitmapStringWidth", w == getBitmapStringWidth(text));
            const Rect box = getBitmapStringBBox(text);
            check(tag + "bbox size == bounds", box.width == w && box.height == h);

            for (Direction hd : hs) for (Direction vd : vs) {
                const Vec2 e = expectedOffset(text, hd, vd);
                const Vec2 o = internal::bitmapAlignOffset(w, h, hd, vd);
                check(tag + name(hd) + "/" + name(vd) + " align offset", o.x == e.x && o.y == e.y);
            }
        }
    }
    // The default line height keeps the old single-line numbers.
    setBitmapLineHeight(bitmapfont::CHAR_TEX_HEIGHT);
    float w, h;
    getBitmapStringBounds("Hi", w, h);
    check("single line: bounds 16 x 16", w == 16 && h == bitmapfont::CHAR_TEX_HEIGHT);
    check("single line: Center offset -8", internal::bitmapAlignOffset(w, h, Direction::Left, Direction::Center).y == -8);
    check("single line: Bottom offset -16", internal::bitmapAlignOffset(w, h, Direction::Left, Direction::Bottom).y == -16);
    check("single line: Baseline offset -13", internal::bitmapAlignOffset(w, h, Direction::Left, Direction::Baseline).y == -13);
    setBitmapLineHeight(savedLineHeight);
}

struct Box {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    bool empty() const { return x1 < 0; }
    bool operator==(const Box& o) const { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
    void add(int x, int y) {
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x > x1) x1 = x;
        if (y > y1) y1 = y;
    }
    string str() const {
        return "[" + to_string(x0) + "," + to_string(y0) + " - " + to_string(x1) + "," + to_string(y1) + "]";
    }
};

constexpr int kW = 320, kH = 240;
constexpr float kAx = 160, kAy = 120;  // anchor, away from every edge

enum class Mode { Plain, Direction, Scale1, Highlight };
const char* modeName(Mode m) {
    switch (m) {
        case Mode::Plain: return "plain";
        case Mode::Direction: return "direction";
        case Mode::Scale1: return "scale1";
        default: return "highlight";
    }
}

class BoundsApp : public App {
public:
    void setup() override { fbo_.allocate(kW, kH); }

    void draw() override {
        if (completed) return;
        pixelChecks();
        completed = true;
        exitApp();
    }

private:
    // Lit white pixels and (for the highlight) red background pixels.
    void render(const string& text, float lh, Direction h, Direction v, Mode mode,
                Box& white, Box& red) {
        setBitmapLineHeight(lh);
        fbo_.begin(0, 0, 0, 1);
        setTextAlign(h, v);
        setColor(1, 1, 1);
        switch (mode) {
            case Mode::Plain: drawBitmapString(text, kAx, kAy); break;
            case Mode::Direction: drawBitmapString(text, kAx, kAy, h, v); break;
            case Mode::Scale1: drawBitmapString(text, kAx, kAy, 1.0f); break;
            case Mode::Highlight:
                drawBitmapStringHighlight(text, kAx, kAy, Color(1, 0, 0), Color(1, 1, 1));
                break;
        }
        fbo_.end();
        setTextAlign(Direction::Left, Direction::Top);

        vector<unsigned char> px(kW * kH * 4);
        white = Box();
        red = Box();
        if (!fbo_.readPixels(px.data())) { check("readPixels", false); return; }
        for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
            const unsigned char* p = &px[(y * kW + x) * 4];
            if (p[0] > 128 && p[1] > 128 && p[2] > 128) white.add(x, y);
            else if (p[0] > 128 && p[1] < 64 && p[2] < 64) red.add(x, y);
        }
        if (!dumpDir.empty()) {
            string file = string(modeName(mode)) + "_lh" + to_string((int)lh) + "_n" + to_string(lineCount(text)) +
                          "_" + string(name(h)) + string(name(v)) + ".png";
            if (!fbo_.save(dumpDir / file).ok()) check("dump " + file, false);
        }
    }

    void pixelChecks() {
        const float savedLineHeight = getBitmapLineHeight();
        for (float lh : lineHeights) for (const string& text : texts) {
            setBitmapLineHeight(lh);
            float w, hgt;
            getBitmapStringBounds(text, w, hgt);
            // Glyph extents relative to the Left/Top draw: the reference.
            Box ref, none;
            render(text, lh, Direction::Left, Direction::Top, Mode::Plain, ref, none);
            const string tag = "lh " + to_string((int)lh) + " lines " + to_string(lineCount(text)) + " ";
            check(tag + "reference draw lit pixels", !ref.empty());
            if (ref.empty()) continue;
            // The glyph rows must stay inside the measured height.
            check(tag + "ink inside measured height", ref.y1 - (int)kAy < (int)hgt);

            for (Direction hd : hs) for (Direction vd : vs) {
                const Vec2 o = expectedOffset(text, hd, vd);
                Box expected;
                expected.x0 = ref.x0 + (int)o.x; expected.x1 = ref.x1 + (int)o.x;
                expected.y0 = ref.y0 + (int)o.y; expected.y1 = ref.y1 + (int)o.y;

                for (Mode m : {Mode::Plain, Mode::Direction, Mode::Scale1}) {
                    Box white, red;
                    render(text, lh, hd, vd, m, white, red);
                    check(tag + string(modeName(m)) + " " + name(hd) + "/" + name(vd) + " ink " + white.str() +
                          " expect " + expected.str(), white == expected);
                }

                // Highlight: same text position as the plain draw, and the
                // background box is exactly the measured size plus 4 px padding.
                Box white, red;
                render(text, lh, hd, vd, Mode::Highlight, white, red);
                const string htag = tag + "highlight " + name(hd) + "/" + name(vd);
                check(htag + " text position == drawBitmapString " + white.str(), white == expected);
                Box box;
                box.x0 = (int)(kAx + o.x) - 4; box.x1 = (int)(kAx + o.x + w) + 4 - 1;
                box.y0 = (int)(kAy + o.y);     box.y1 = (int)(kAy + o.y + hgt) - 1;
                check(htag + " box == measured size " + red.str() + " expect " + box.str(), red == box);
            }
        }
        setBitmapLineHeight(savedLineHeight);
    }

    Fbo fbo_;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    headlessChecks();
    bool gpu = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--gpu-check") == 0) gpu = true;
        else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) dumpDir = argv[++i];
    }
    if (!gpu) {
        printf("SKIP pixel checks: bitmapStringBounds needs --gpu-check and a display\n");
        return failures ? 1 : 0;
    }
    if (!dumpDir.empty()) filesystem::create_directories(dumpDir);
    WindowSettings settings;
    settings.setSize(kW, kH);
    settings.setHighDpi(false);
    return tcCoreTest::runDisplayTest<BoundsApp>(settings, failures, [] {
        check("pixel checks completed", completed);
    });
}
