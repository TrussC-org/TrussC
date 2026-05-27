// =============================================================================
// bitmapStringExtendedExample
// -----------------------------------------------------------------------------
// Demonstrates the extension points around drawBitmapString:
//   - UTF-8 + halfwidth/fullwidth aware rendering
//   - tofu (missing-glyph) fallback for codepoints not in the registry
//   - registerGlyph() lets you plug in custom bitmaps for any codepoint
//   - compile16x13 / compile8x13 turn ASCII art into packed bytes at compile
//     time, so you can hand-author glyphs without a tool
//   - updateGlyph() lets you swap glyph data each frame for animation
// =============================================================================

#include "tcApp.h"

using namespace std;
using namespace tc;

// ---------------------------------------------------------------------------
// Hand-authored glyphs (compile-time)
// ---------------------------------------------------------------------------
namespace {

// A heart, fullwidth (16x13).
constexpr auto HEART = bitmapfont::compile16x13({
    "................",
    "..###.....###...",
    ".#####...#####..",
    ".###############",
    ".###############",
    "..#############.",
    "...###########..",
    "....#########...",
    ".....#######....",
    "......#####.....",
    ".......###......",
    "........#.......",
    "................"
});

// A star, fullwidth.
constexpr auto STAR = bitmapfont::compile16x13({
    "................",
    "........#.......",
    "........#.......",
    ".......###......",
    ".......###......",
    "..#############.",
    "...###########..",
    "....#########...",
    "....##.###.##...",
    "...##...#...##..",
    "..##.........##.",
    "................",
    "................"
});

// A tiny pixel character, fullwidth — 2 walk-cycle frames for the animation.
constexpr auto CHAR_FRAME_0 = bitmapfont::compile16x13({
    "................",
    "......####......",
    "......#..#......",
    "......####......",
    ".....######.....",
    ".#...######...#.",
    ".#...######...#.",
    ".#####....#####.",
    ".....######.....",
    "......#..#......",
    "......#..#......",
    "......#..#......",
    ".....##..##....."
});
constexpr auto CHAR_FRAME_1 = bitmapfont::compile16x13({
    "................",
    "......####......",
    "......#..#......",
    "......####......",
    ".....######.....",
    "..#..######..#..",
    "...#.######.#...",
    "....##....##....",
    ".....######.....",
    "......#..#......",
    "......#..#......",
    ".....##..##.....",
    "....##....##...."
});

// Halfwidth custom glyph: a tiny smiley to drop into ASCII strings.
constexpr auto SMILEY = bitmapfont::compile8x13({
    "........",
    "........",
    "........",
    "........",
    "..####..",
    ".#....#.",
    "#.#..#.#",
    "#......#",
    "#.#..#.#",
    "#..##..#",
    ".#....#.",
    "..####..",
    "........"
});

}  // namespace

void tcApp::setup() {
    setFps(60);

    // Register custom glyphs. Codepoints in the Private Use Area (U+E000..)
    // never collide with real characters — perfect for ad-hoc additions.
    static constexpr bitmapfont::Glyph GLYPHS[] = {
        { 0x2665, HEART.data(),   bitmapfont::Width::Fullwidth },   // ♥
        { 0x2605, STAR.data(),    bitmapfont::Width::Fullwidth },   // ★
        { 0xE000, CHAR_FRAME_0.data(), bitmapfont::Width::Fullwidth }, // animated
        { 0xE100, SMILEY.data(),  bitmapfont::Width::Halfwidth },  // smiley (PUA)
    };
    bitmapfont::registerGlyphs(GLYPHS);
}

void tcApp::draw() {
    clear(0.07f, 0.08f, 0.10f);
    setColor(1.0f);

    drawBitmapString("drawBitmapString  --  extensible bitmap font", 24, 24, 2.0f);
    setColor(Color(0.5f));
    drawBitmapString("ASCII is built in. Other glyphs are added with registerGlyph().",
                     24, 60);

    setColor(Color(0.55f, 0.65f, 0.80f)); drawBitmapString("scale 1  ASCII",   24,  90);
    setColor(Color(1.0f));
    drawBitmapString("The quick brown fox jumps over the lazy dog 0123456789", 24, 110);

    setColor(Color(0.55f, 0.65f, 0.80f)); drawBitmapString("scale 2  ASCII",   24, 140);
    setColor(Color(1.0f));
    drawBitmapString("Hello, world!", 24, 160, 2.0f);

    setColor(Color(0.55f, 0.65f, 0.80f)); drawBitmapString("scale 4  ASCII",   24, 210);
    setColor(Color(1.0f));
    drawBitmapString("RETRO", 24, 230, 4.0f);

    // Registered custom glyphs.
    setColor(Color(0.55f, 0.65f, 0.80f));
    drawBitmapString("registered glyphs  (PUA + symbols)", 24, 340);
    setColor(Color(1.0f));
    drawBitmapString(
        "\xee\x80\x80 walks past \xe2\x98\x85\xe2\x99\xa5\xee\x84\x80",  // [walker] walks past ★♥[smiley]
        24, 360, 2.0f);

    // Tofu: codepoints we never registered show as a hollow square.
    setColor(Color(0.55f, 0.65f, 0.80f));
    drawBitmapString("out-of-LUT  (tofu)", 24, 420);
    setColor(Color(1.0f));
    drawBitmapString("Kanji: \xe6\xbc\xa2\xe5\xad\x97 / Hangul: \xed\x95\x9c",
                     24, 440, 2.0f);

    // Right column: explainer.
    setColor(Color(0.55f, 0.65f, 0.80f));
    drawBitmapString("How extending works:", 640, 90);
    setColor(1.0f);
    drawBitmapString("registerGlyph({ cp, data, width })", 640, 110);
    drawBitmapString("- compile8x13 / compile16x13 build", 640, 130);
    drawBitmapString("  packed bytes at compile time.",    640, 144);
    drawBitmapString("- Or hand-write hex arrays.",        640, 158);
    drawBitmapString("- Atlas is rebuilt automatically",   640, 178);
    drawBitmapString("  the next time you draw.",          640, 192);
    drawBitmapString("- Unknown codepoints fall back to",  640, 212);
    drawBitmapString("  the TOFU glyph (Cell 95).",        640, 226);

    setColor(Color(0.55f, 0.65f, 0.80f));
    drawBitmapString("Animation via updateGlyph:", 640, 270);
    setColor(1.0f);
    drawBitmapString("Walker swaps frame each ~150 ms by",   640, 290);
    drawBitmapString("calling updateGlyph(0xE000, FRAME_N).", 640, 304);

    // ---- Animate by swapping the U+E000 cell's data pointer ----
    int frame = (int)(getElapsedTime() * 6.0f) & 1;  // ~6 Hz
    bitmapfont::updateGlyph(0xE000,
        frame ? CHAR_FRAME_1.data() : CHAR_FRAME_0.data());

    // FPS
    setColor(Color(0.5f));
    char fps[64];
    std::snprintf(fps, sizeof(fps), "%.1f fps", getFps());
    setTextAlign(Direction::Right, Direction::Bottom);
    drawBitmapString(fps, (float)getWindowWidth() - 12, (float)getWindowHeight() - 8);
    setTextAlign(Direction::Left, Direction::Top);
}
