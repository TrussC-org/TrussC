// Exercise actual stb outline/raster allocation failures without C++ exceptions.
#include "../common/tcTestFailure.h"
#include "impl/tcFontScratch.h"
#include "stb/stb_truetype.h"
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
using namespace std;
namespace {
#include "../asyncLoads/src/fontFixture.h"
}
// The same implementation TU also contains image hooks; this test doesn't use images.
namespace trussc::internal {
bool imageDecodeAllocationAllowed(size_t) { return true; }
void imageDecodeAllocationFailed() {}
}
int main() {
    const auto font = makeFont();
    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, font.data(), 0)) return 1;
    vector<unsigned char> bitmap(128 * 128);
    bool sawFailure = false, sawSuccess = false;
    for (int successes = 0; successes < 12; ++successes) {
        trussc::internal::FontScratchScope scratch(font.size(), bitmap.size());
        const size_t remaining = trussc::internal::fontScratchRemaining();
        trussc::internal::failFontScratchAllocationAfterForTests(successes);
        fill(bitmap.begin(), bitmap.end(), static_cast<unsigned char>(0));
        stbtt_MakeGlyphBitmap(&info, bitmap.data(), 128, 128, 128,
                             0.128f, 0.128f, stbtt_FindGlyphIndex(&info, 'A'));
        sawFailure |= scratch.failed();
        sawSuccess |= !scratch.failed() && any_of(bitmap.begin(), bitmap.end(), [](unsigned char pixel) { return pixel != 0; });
        if (trussc::internal::fontScratchRemaining() != remaining) {
            std::printf("scratch retained an allocation at failure step %d\n", successes);
            return 1;
        }
    }
    trussc::internal::failFontScratchAllocationAfterForTests(-1);
    auto malformed = font;
    const size_t glyphStart = size_t(info.glyf);
    malformed[glyphStart] = 0x7f; malformed[glyphStart + 1] = 0xff;
    stbtt_fontinfo malformedInfo{};
    if (!stbtt_InitFont(&malformedInfo, malformed.data(), 0)) return 1;
    {
        trussc::internal::FontScratchScope scratch(malformed.size());
        trussc::internal::failFontScratchAllocationAfterForTests(-1);
        stbtt_vertex* vertices = nullptr;
        const int count = stbtt_GetGlyphShape(&malformedInfo, 1, &vertices);
        if (vertices) stbtt_FreeShape(&malformedInfo, vertices);
        if (count || !scratch.failed() || trussc::internal::fontScratchLargestAllocationForTests()) return 1;
    }
    auto compound = font;
    compound[glyphStart] = compound[glyphStart + 1] = 0xff;
    compound[glyphStart + 10] = 0; compound[glyphStart + 11] = 0x22;
    compound[glyphStart + 12] = 0; compound[glyphStart + 13] = 1;
    compound[glyphStart + 14] = compound[glyphStart + 15] = 0;
    compound[glyphStart + 16] = 0; compound[glyphStart + 17] = 2;
    compound[glyphStart + 18] = 0; compound[glyphStart + 19] = 1;
    compound[glyphStart + 20] = compound[glyphStart + 21] = 0;
    if (!stbtt_InitFont(&malformedInfo, compound.data(), 0)) return 1;
    {
        trussc::internal::FontScratchScope scratch(compound.size());
        stbtt_vertex* vertices = nullptr;
        const int count = stbtt_GetGlyphShape(&malformedInfo, 1, &vertices);
        if (vertices) stbtt_FreeShape(&malformedInfo, vertices);
        if (count || !scratch.failed()) return 1;
    }
    // Reuse the round-1 generated compound fixture, with an empty leaf.
    vector<Bytes> doubled(21);
    for (size_t i = 0; i + 1 < doubled.size(); ++i) {
        auto& bytes = doubled[i];
        put16(bytes, 0xffff);
        for (int j = 0; j < 4; ++j) put16(bytes, 0);
        put16(bytes, 0x22); put16(bytes, uint32_t(i + 1)); put16(bytes, 0);
        put16(bytes, 0x02); put16(bytes, uint32_t(i + 1)); put16(bytes, 0);
    }
    doubled.back().resize(10);
    const auto repeated = makeFont(doubled);
    if (!stbtt_InitFont(&malformedInfo, repeated.data(), 0)) return 1;
    bool bounded = false;
    {
        trussc::internal::FontScratchScope scratch(repeated.size());
        stbtt_vertex* vertices = nullptr;
        const int count = stbtt_GetGlyphShape(&malformedInfo, 0, &vertices);
        if (vertices) stbtt_FreeShape(&malformedInfo, vertices);
        bounded = !count && scratch.failed();
    }
    std::printf("fontScratch: total compound work rejected: %s\n", bounded ? "PASS" : "FAIL");
    // Flat nonempty components exercise accumulated append copies, not visits.
    Bytes flat;
    put16(flat, 0xffff);
    for (int i = 0; i < 4; ++i) put16(flat, 0);
    for (int i = 0; i < 2048; ++i) {
        put16(flat, i == 2047 ? 0x02 : 0x22);
        put16(flat, 1); put16(flat, 0);
    }
    const auto flatFont = makeFont({Bytes{}, boxGlyph(0, 0, 100, 100), flat, Bytes{}});
    if (!stbtt_InitFont(&malformedInfo, flatFont.data(), 0)) return 1;
    bool copiesBounded = false;
    {
        trussc::internal::FontScratchScope scratch(flatFont.size());
        const auto remaining = trussc::internal::fontScratchRemaining();
        stbtt_vertex* vertices = nullptr;
        const int count = stbtt_GetGlyphShape(&malformedInfo, 2, &vertices);
        copiesBounded = !count && !vertices && scratch.failed() &&
            trussc::internal::fontScratchRemaining() == remaining;
        if (vertices) stbtt_FreeShape(&malformedInfo, vertices);
    }
    std::printf("fontScratch: accumulated compound copies rejected and released: %s\n", copiesBounded ? "PASS" : "FAIL");
    // A normal compound with two distinct simple glyphs still rasterizes.
    Bytes pair;
    put16(pair, 0xffff);
    put16(pair, 0); put16(pair, uint16_t(-200)); put16(pair, 2000); put16(pair, 800);
    put16(pair, 0x22); put16(pair, 1); put16(pair, 0);
    put16(pair, 0x02); put16(pair, 2); put16(pair, 0);
    const auto normal = makeFont({Bytes{}, boxGlyph(0, -200, 1000, 800), boxGlyph(0, 0, 100, 100), pair});
    if (!stbtt_InitFont(&malformedInfo, normal.data(), 0)) return 1;
    bool compoundRendered = false;
    {
        trussc::internal::FontScratchScope scratch(normal.size(), bitmap.size());
        fill(bitmap.begin(), bitmap.end(), static_cast<unsigned char>(0));
        stbtt_MakeGlyphBitmap(&malformedInfo, bitmap.data(), 128, 128, 128, 0.128f, 0.128f, 3);
        compoundRendered = !scratch.failed() && any_of(bitmap.begin(), bitmap.end(), [](unsigned char p) { return p != 0; });
    }
    std::printf("fontScratch: normal compound renders: %s\n", compoundRendered ? "PASS" : "FAIL");
    std::printf("fontScratch: malformed records and compound depth reject before allocation\n");
    std::printf("fontScratch: clean failures=%d, complete render=%d, no retained workspace\n", sawFailure, sawSuccess);
    return sawFailure && sawSuccess && bounded && copiesBounded && compoundRendered ? 0 : 1;
}
