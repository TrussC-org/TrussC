#pragma once
// Same deterministic sfnt fixture as fontAtlasLimit (no system font dependency).
using Bytes = vector<uint8_t>;
static void put16(Bytes& b, uint32_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
static void put32(Bytes& b, uint32_t v) { put16(b, v >> 16); put16(b, v & 0xffff); }

static Bytes boxGlyph(int x0, int y0, int x1, int y1) {
    Bytes b;
    put16(b, 1);                                   // one contour
    put16(b, (uint16_t)x0); put16(b, (uint16_t)y0);
    put16(b, (uint16_t)x1); put16(b, (uint16_t)y1);
    put16(b, 3);                                   // end point of the contour
    put16(b, 0);                                   // no instructions
    for (int i = 0; i < 4; i++) b.push_back(1);    // on-curve, int16 deltas
    const int xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    int prev = 0;
    for (int x : xs) { put16(b, (uint16_t)(x - prev)); prev = x; }
    prev = 0;
    for (int y : ys) { put16(b, (uint16_t)(y - prev)); prev = y; }
    while (b.size() % 4) b.push_back(0);
    return b;
}

static Bytes makeFont() {
    const vector<Bytes> glyphs = {Bytes{}, boxGlyph(0, -200, 1000, 800),
                                  boxGlyph(0, -200, 2000, 800), boxGlyph(0, 0, 100, 100)};
    Bytes glyf, loca;
    for (auto& g : glyphs) { put32(loca, (uint32_t)glyf.size()); glyf.insert(glyf.end(), g.begin(), g.end()); }
    put32(loca, (uint32_t)glyf.size());

    Bytes head;
    put32(head, 0x00010000); put32(head, 0x00010000); put32(head, 0); put32(head, 0x5F0F3CF5);
    put16(head, 0); put16(head, 1000);
    for (int i = 0; i < 16; i++) head.push_back(0);
    put16(head, 0); put16(head, (uint16_t)-200); put16(head, 2000); put16(head, 800);  // bbox
    put16(head, 0); put16(head, 8); put16(head, 2);
    put16(head, 1);  // long loca
    put16(head, 0);

    Bytes hhea;
    put32(hhea, 0x00010000); put16(hhea, 800); put16(hhea, (uint16_t)-200); put16(hhea, 0);
    put16(hhea, 2000);
    for (int i = 0; i < 11; i++) put16(hhea, 0);
    put16(hhea, 4);  // numberOfHMetrics

    Bytes maxp;
    put32(maxp, 0x00005000); put16(maxp, 4);

    Bytes hmtx;
    const int adv[4] = {500, 1000, 2000, 100};
    for (int a : adv) { put16(hmtx, (uint32_t)a); put16(hmtx, 0); }

    // cmap format 4: 'A' -> 1, 'W' -> 2, 'a' -> 3.
    struct Seg { uint16_t c, g; };
    const vector<Seg> segs = {{'A', 1}, {'W', 2}, {'a', 3}, {0xFFFF, 0}};
    const uint32_t n = (uint32_t)segs.size();
    Bytes sub;
    put16(sub, 4); put16(sub, 16 + 8 * n); put16(sub, 0);
    put16(sub, n * 2); put16(sub, 4); put16(sub, 1); put16(sub, n * 2 - 4);
    for (auto& s : segs) put16(sub, s.c);
    put16(sub, 0);
    for (auto& s : segs) put16(sub, s.c);
    for (auto& s : segs) put16(sub, (uint16_t)(s.c == 0xFFFF ? 1 : s.g - s.c));
    for (size_t i = 0; i < segs.size(); i++) put16(sub, 0);
    Bytes cmap;
    put16(cmap, 0); put16(cmap, 1); put16(cmap, 3); put16(cmap, 1); put32(cmap, 12);
    cmap.insert(cmap.end(), sub.begin(), sub.end());

    const vector<pair<const char*, Bytes>> tables = {
        {"cmap", cmap}, {"glyf", glyf}, {"head", head}, {"hhea", hhea},
        {"hmtx", hmtx}, {"loca", loca}, {"maxp", maxp}};
    Bytes out;
    put32(out, 0x00010000); put16(out, (uint32_t)tables.size());
    put16(out, 0); put16(out, 0); put16(out, 0);
    size_t offset = 12 + 16 * tables.size();
    vector<size_t> offsets;
    for (auto& t : tables) { offsets.push_back(offset); offset = (offset + t.second.size() + 3) & ~size_t(3); }
    for (size_t i = 0; i < tables.size(); i++) {
        const char* t = tables[i].first;
        put32(out, (uint32_t(uint8_t(t[0])) << 24) | (uint32_t(uint8_t(t[1])) << 16) |
                   (uint32_t(uint8_t(t[2])) << 8) | uint8_t(t[3]));
        put32(out, 0);
        put32(out, (uint32_t)offsets[i]);
        put32(out, (uint32_t)tables[i].second.size());
    }
    for (size_t i = 0; i < tables.size(); i++) {
        while (out.size() < offsets[i]) out.push_back(0);
        out.insert(out.end(), tables[i].second.begin(), tables[i].second.end());
    }
    return out;
}


// Two-face collection; face 1 gives A a different advance. Reuses the same
// sfnt bytes and relocation rule as fontFaceIndex.
[[maybe_unused]] static Bytes makeTwoFaceCollection() {
    auto u16 = [](const Bytes& bytes, size_t at) {
        return (uint32_t(bytes[at]) << 8) | bytes[at + 1];
    };
    auto u32 = [](const Bytes& bytes, size_t at) {
        return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
               (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
    };
    auto set32 = [](Bytes& bytes, size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes[at + i] = uint8_t(value >> (24 - i * 8));
    };
    Bytes first = makeFont();
    while (first.size() % 4) first.push_back(0);
    Bytes second = first;
    const size_t tables = u16(first, 4);
    for (size_t i = 0; i < tables; ++i) {
        const size_t record = 12 + 16 * i;
        if (u32(second, record) == 0x686d7478) { // hmtx, glyph 1
            const auto offset = u32(second, record + 8) + 4;
            second[offset] = 0x03; second[offset + 1] = 0x84; // advance 900
        }
    }
    Bytes result;
    put32(result, 0x74746366); put32(result, 0x00010000); put32(result, 2);
    put32(result, 20); put32(result, 20 + (uint32_t)first.size());
    for (auto face : {first, second}) {
        const uint32_t start = (uint32_t)result.size();
        for (size_t i = 0; i < tables; ++i) {
            const size_t at = 12 + 16 * i + 8;
            set32(face, at, u32(face, at) + start);
        }
        result.insert(result.end(), face.begin(), face.end());
    }
    return result;
}
