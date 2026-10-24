#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <cmath>
#include <limits>
#include "stb/stb_truetype.h"

namespace trussc::internal {
// Bound (abi): stb_truetype uses signed offsets with room for intermediate sums. Overflow rejects the whole font and is counted.
inline constexpr uint64_t kFontBackendSize = 0x40000000u;
inline constexpr const char* kFontBackendSizeReason = "font data exceeds the stb_truetype addressing range";
// Validate one glyph's encoded records before stb reads counts or allocates an
// outline. Compound children are checked on entry by the decoder as well.
inline bool fontGlyphRangeFits(const stbtt_fontinfo* font, size_t bytes, int glyph) {
    if (glyph < 0 || glyph >= font->numGlyphs) return false;
    // CFF uses bounded buffers plus a total-interpreter-instruction budget in run_charstring,
    // including its metrics/counting passes; this record check is TrueType-only.
    if (bytes == SIZE_MAX || font->cff.size) return true;
    const auto fits = [bytes](size_t at, size_t count) { return at <= bytes && count <= bytes - at; };
    const auto u16 = [&](size_t at) { return (size_t(font->data[at]) << 8) | font->data[at + 1]; };
    const auto u32 = [&](size_t at) { return (u16(at) << 16) | u16(at + 2); };
    const size_t stride = font->indexToLocFormat ? 4 : 2;
    const size_t location = size_t(font->loca) + size_t(glyph) * stride;
    if (!fits(location, 2 * stride)) return false;
    const size_t first = stride == 4 ? u32(location) : 2 * u16(location);
    const size_t last = stride == 4 ? u32(location + stride) : 2 * u16(location + stride);
    if (first > last || !fits(size_t(font->glyf), last)) return false;
    if (first == last) return true;
    size_t at = size_t(font->glyf) + first;
    const size_t end = size_t(font->glyf) + last;
    const auto take = [&](size_t count) { if (count > end - at) return false; at += count; return true; };
    // Bound (security): contour, flag, coordinate and component records must fit the remaining glyph section before outline allocation. Overflow rejects the whole glyph and is counted.
    if (!take(10)) return false;
    const size_t contours = u16(at - 10);
    if (contours == 0) return true;
    if (contours < 0x8000) {
        if (!take(contours * 2)) return false;
        const size_t points = u16(at - 2) + 1;
        // End-point indices must increase, or stb's output count can exceed its allocation.
        size_t previous = 0;
        for (size_t index = 0; index < contours; ++index) {
            const size_t point = u16(at - contours * 2 + index * 2);
            if (index && point <= previous) return false;
            previous = point;
        }
        if (!take(2)) return false;
        const size_t instructions = u16(at - 2);
        if (!take(instructions)) return false;
        size_t seen = 0, xBytes = 0, yBytes = 0;
        while (seen < points) {
            if (!take(1)) return false;
            const unsigned flag = font->data[at - 1];
            size_t count = 1;
            if (flag & 8) { if (!take(1)) return false; count += font->data[at - 1]; }
            if (count > points - seen) return false;
            seen += count;
            xBytes += count * ((flag & 2) ? 1 : (flag & 16) ? 0 : 2);
            yBytes += count * ((flag & 4) ? 1 : (flag & 32) ? 0 : 2);
        }
        return take(xBytes) && take(yBytes);
    }
    unsigned flags = 0;
    do {
        if (!take(4)) return false;
        flags = unsigned(u16(at - 4));
        if (u16(at - 2) >= size_t(font->numGlyphs)) return false;
        // stb supports XY component arguments, not point-matching arguments.
        if (!(flags & 2) || !take((flags & 1) ? 4 : 2)) return false;
        if (!take((flags & 8) ? 2 : (flags & 64) ? 4 : (flags & 128) ? 8 : 0)) return false;
    } while (flags & 32);
    if (flags & 256) { if (!take(2)) return false; if (!take(u16(at - 2))) return false; }
    return true;
}

inline bool fontGlyphScaleFits(const stbtt_fontinfo* font, int glyph, float scaleX, float scaleY) {
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0) return false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBox(font, glyph, &x0, &y0, &x1, &y1);
    // Bound (abi): scaled metrics must leave room for signed bitmap differences and padding. Overflow rejects the whole glyph and is counted.
    const double coordinateLimit = double(std::numeric_limits<int>::max()) / 8;
    return std::abs(double(x0) * scaleX) < coordinateLimit &&
           std::abs(double(x1) * scaleX) < coordinateLimit &&
           std::abs(double(y0) * scaleY) < coordinateLimit &&
           std::abs(double(y1) * scaleY) < coordinateLimit;
}

inline bool checkFontSfnt(const uint8_t* data, size_t size, int fontIndex,
                                  std::string& reason) {
        auto u16 = [data](uint64_t o) -> uint64_t {
            return ((uint64_t)data[o] << 8) | data[o + 1];
        };
        auto u32 = [data](uint64_t o) -> uint64_t {
            return ((uint64_t)data[o] << 24) | ((uint64_t)data[o + 1] << 16) |
                   ((uint64_t)data[o + 2] << 8) | data[o + 3];
        };
        auto tag = [](const char* t) -> uint64_t {
            return ((uint64_t)(uint8_t)t[0] << 24) | ((uint64_t)(uint8_t)t[1] << 16) |
                   ((uint64_t)(uint8_t)t[2] << 8) | (uint8_t)t[3];
        };
        auto tagName = [](uint64_t t) {
            std::string s(4, ' ');
            for (int i = 0; i < 4; i++) {
                const char c = (char)((t >> (24 - 8 * i)) & 0xff);
                s[i] = (c >= 0x20 && c < 0x7f) ? c : '?';
            }
            return s;
        };

        const uint64_t n = size;
        if (n < 12) {
            reason = "data is " + std::to_string(size) + " bytes, shorter than a font header";
            return false;
        }
        // The stb backend handles fonts below 1 GiB.
        if (n >= kFontBackendSize) {
            reason = kFontBackendSizeReason;
            return false;
        }

        // Font collection (.ttc): pick the font at fontIndex.
        uint64_t fontStart = 0;
        if (u32(0) == tag("ttcf")) {
            const uint64_t version = u32(4);
            if (version != 0x00010000u && version != 0x00020000u) {
                reason = "unsupported font collection version";
                return false;
            }
            const uint64_t numFonts = u32(8);
            if (numFonts == 0 || 12 + 4 * numFonts > n) {
                reason = "font collection header is truncated";
                return false;
            }
            if (fontIndex < 0 || (uint64_t)fontIndex >= numFonts) {
                reason = "font index " + std::to_string(fontIndex) + " is out of range";
                return false;
            }
            fontStart = u32(12 + 4 * (uint64_t)fontIndex);
            if (fontStart + 12 > n) {
                reason = "font offset in the collection is outside the data";
                return false;
            }
        } else if (fontIndex != 0) {
            reason = "font index " + std::to_string(fontIndex) + " is out of range";
            return false;
        }

        // The sfnt versions stb_truetype accepts.
        const uint64_t sfntVersion = u32(fontStart);
        if (sfntVersion != 0x00010000u && sfntVersion != tag("1\0\0\0") &&
            sfntVersion != tag("true") && sfntVersion != tag("typ1") &&
            sfntVersion != tag("OTTO")) {
            reason = "not a TrueType / OpenType font";
            return false;
        }

        // Table directory. As in stbtt_InitFont, the first entry with a given
        // tag is used, and an entry at offset 0 counts as missing.
        const uint64_t numTables = u16(fontStart + 4);
        const uint64_t dirStart = fontStart + 12;
        if (dirStart + 16 * numTables > n) {
            reason = "table directory is truncated";
            return false;
        }
        struct Table { uint64_t offset = 0, length = 0; bool seen = false, found = false; };
        Table cmap, head, hhea, hmtx, maxp, glyf, loca, cff;
        const std::pair<uint64_t, Table*> wanted[] = {
            {tag("cmap"), &cmap}, {tag("head"), &head}, {tag("hhea"), &hhea},
            {tag("hmtx"), &hmtx}, {tag("maxp"), &maxp}, {tag("glyf"), &glyf},
            {tag("loca"), &loca}, {tag("CFF "), &cff},
        };
        for (uint64_t i = 0; i < numTables; i++) {
            const uint64_t rec = dirStart + 16 * i;
            const uint64_t t = u32(rec);
            const uint64_t offset = u32(rec + 8);
            const uint64_t length = u32(rec + 12);
            if (offset + length > n) {
                reason = "table '" + tagName(t) + "' is outside the data";
                return false;
            }
            for (const auto& w : wanted) {
                if (w.first == t && !w.second->seen) {
                    *w.second = Table{offset, length, true, offset != 0};
                }
            }
        }

        // Required tables, and the fixed fields stb reads from them.
        const bool trueType = glyf.found;  // same rule as stbtt_InitFont
        const std::pair<const char*, const Table*> required[] = {
            {"cmap", &cmap}, {"head", &head}, {"hhea", &hhea}, {"hmtx", &hmtx},
            {"maxp", &maxp}, {trueType ? "loca" : "CFF ", trueType ? &loca : &cff},
        };
        for (const auto& r : required) {
            if (!r.second->found) {
                reason = std::string("missing required table '") + r.first + "'";
                return false;
            }
        }
        if (head.length < 54 || hhea.length < 36 || maxp.length < 6 || cmap.length < 4) {
            reason = "head / hhea / maxp / cmap table is too short";
            return false;
        }

        // cmap encoding records, and the offsets of the subtables stb may pick
        // (Microsoft Unicode BMP / full, or any Unicode platform record).
        const uint64_t numSubtables = u16(cmap.offset + 2);
        if (4 + 8 * numSubtables > cmap.length) {
            reason = "cmap encoding records are truncated";
            return false;
        }
        bool usableCmap = false;
        for (uint64_t i = 0; i < numSubtables; i++) {
            const uint64_t rec = cmap.offset + 4 + 8 * i;
            const uint64_t platform = u16(rec);
            const uint64_t encoding = u16(rec + 2);
            if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) {
                if (u32(rec + 4) + 2 > cmap.length) {
                    reason = "cmap subtable offset is outside the cmap table";
                    return false;
                }
                // Match the vendored stbtt_InitFont selection. Read the format
                // only after checking this Unicode record's subtable offset.
                const uint64_t format = u16(cmap.offset + u32(rec + 4));
                const uint64_t sub = cmap.offset + u32(rec + 4);
                const uint64_t available = cmap.length - u32(rec + 4);
                // Bound (security): cmap subtable counts and search ranges must fit their actual section before lookup. Overflow rejects the whole font and is counted.
                if (format == 0 || format == 4 || format == 6 || format == 12 || format == 13) {
                    const uint64_t headerBytes = format >= 12 ? 16 : format == 4 ? 16 : format == 6 ? 10 : 6;
                    if (available < headerBytes) { reason = "cmap subtable header is truncated"; return false; }
                    const uint64_t length = format >= 12 ? u32(sub + 4) : u16(sub + 2);
                    if (length < headerBytes || length > available) { reason = "cmap subtable length is outside the table"; return false; }
                    if (format == 0 && length != 262) { reason = "cmap byte map is truncated"; return false; }
                    if (format == 6 && u16(sub + 8) > (length - 10) / 2) { reason = "cmap glyph count exceeds the subtable"; return false; }
                    if (format >= 12 && u32(sub + 12) > (length - 16) / 12) { reason = "cmap group count exceeds the subtable"; return false; }
                    if (format == 4) {
                        const uint64_t segments = u16(sub + 6) / 2;
                        if (!segments || (u16(sub + 6) & 1) || segments > (length - 16) / 8) {
                            reason = "cmap segment count exceeds the subtable"; return false;
                        }
                        uint64_t power = 1, selector = 0;
                        while (power <= segments / 2) { power *= 2; ++selector; }
                        if (u16(sub + 8) != power * 2 || u16(sub + 10) != selector || u16(sub + 12) != segments * 2 - power * 2) {
                            reason = "cmap segment search range is invalid"; return false;
                        }
                        uint64_t previousEnd = 0;
                        for (uint64_t segment = 0; segment < segments; ++segment) {
                            const uint64_t endCode = u16(sub + 14 + segment * 2);
                            const uint64_t startCode = u16(sub + 16 + segments * 2 + segment * 2);
                            const uint64_t offsetAt = 16 + segments * 6 + segment * 2;
                            const uint64_t rangeOffset = u16(sub + offsetAt);
                            if (startCode > endCode || (segment && endCode < previousEnd) ||
                                (rangeOffset && (rangeOffset > length - offsetAt ||
                                    (endCode - startCode + 1) > (length - offsetAt - rangeOffset) / 2))) {
                                reason = "cmap segment range is outside the subtable"; return false;
                            }
                            previousEnd = endCode;
                        }
                    }
                }
                if (format == 0 || format == 4 || format == 6 || format == 12 || format == 13)
                    usableCmap = true;
            }
        }
        if (!usableCmap) {
            reason = "no usable Unicode character map (stb_truetype supports cmap formats 0, 4, 6, 12 and 13)";
            return false;
        }

        // hmtx: numberOfHMetrics long entries, then one short entry per
        // remaining glyph.
        const uint64_t numGlyphs = u16(maxp.offset + 4);
        const uint64_t numLong = u16(hhea.offset + 34);
        if (numLong < 1 || numLong > numGlyphs) {
            reason = "hhea numberOfHMetrics is out of range";
            return false;
        }
        if (4 * numLong + 2 * (numGlyphs - numLong) > hmtx.length) {
            reason = "hmtx table is too short";
            return false;
        }

        // loca: numGlyphs + 1 entries, in non-decreasing order, each within
        // glyf (a glyph's data runs from its entry to the next one).
        if (trueType) {
            const uint64_t locaFormat = u16(head.offset + 50);
            if (locaFormat > 1) {
                reason = "unsupported loca format";
                return false;
            }
            const uint64_t entrySize = (locaFormat == 0) ? 2 : 4;
            if ((numGlyphs + 1) * entrySize > loca.length) {
                reason = "loca table is too short";
                return false;
            }
            uint64_t previous = 0;
            for (uint64_t i = 0; i <= numGlyphs; i++) {
                const uint64_t at = loca.offset + i * entrySize;
                const uint64_t glyphOffset = (locaFormat == 0) ? u16(at) * 2 : u32(at);
                if (glyphOffset > glyf.length) {
                    reason = "loca entry points past the end of glyf";
                    return false;
                }
                if (glyphOffset < previous) {
                    reason = "loca entries are not in increasing order";
                    return false;
                }
                previous = glyphOffset;
            }
        }
        return true;
    }

}
