// Included in the fixture's anonymous namespace. Stats and actual image handles
// count production uploads without adding counters to the Font API.
bool burstCompleted = false;

class BurstCheckNode : public Node {
    Font font_;
    Fbo fbo_;
    Fbo later_;
    bool ran_ = false;
    bool mipmaps_ = true;
public:
    void setup() override {
        mipmaps_ = !getenv("TC_FONT_BURST_NO_MIPS");
        font_.setMipmaps(mipmaps_);
        font_.setOversampling(3);
        check("burst: 128px font loads", bool(font_.load(g_fontPath, 128)));
        fbo_.allocate(3680, 3680, 1, TextureFormat::RGBA8, true);
        later_.allocate(256, 192);
    }
    void draw() override {
        if (ran_) return;
        ran_ = true;
        const int count = getenv("TC_FONT_BURST_COUNT") ? atoi(getenv("TC_FONT_BURST_COUNT")) : 500;
        const size_t limit = getenv("TC_FONT_BURST_BYTE_LIMIT") ? strtoull(getenv("TC_FONT_BURST_BYTE_LIMIT"), nullptr, 10) : 0;
        const auto before = sg_query_stats().total.images;
        map<uint32_t, size_t> images;
        map<size_t, int> creations;
        map<size_t, int> glyphsPerPage;
        const auto manager = internal::SharedFontCache::getInstance().get(
            internal::FontCacheKey{g_fontPath, 128, 0, mipmaps_, 3});
        check("burst: inspect the same cached atlas manager", bool(manager));
        if (!manager) { exitApp(); return; }
        const int stride = g_burstPerGlyph ? 7 : 1;
        size_t liveBytes = 0;
        int mipRebuilds = 0;
        auto observe = [&] {
            for (size_t p = 0; p < font_.getAtlasCount(); ++p) {
                const auto image = font_.getAtlas(p)->getTexture();
                if (!image.id || images.count(image.id)) continue;
                const auto desc = sg_query_image_desc(image);
                size_t bytes = 0;
                for (int level = 0, w = desc.width, h = desc.height;
                     level < desc.num_mipmaps; ++level, w = max(1, w / 2), h = max(1, h / 2))
                    bytes += size_t(w) * h;
                images[image.id] = bytes;
                liveBytes += bytes;
                ++creations[p];
                if (desc.num_mipmaps > 1) ++mipRebuilds;
            }
        };
        const auto start = chrono::steady_clock::now();
        fbo_.begin(0, 0, 0, 0);
        setColor(1, .5f, .25f);
        int completed = 0;
        for (int i = 0; i < count; ++i) {
            // A separated grid leaves every first/last glyph observable.
            font_.drawString(utf8(kFirstCodepoint + stride * i), 96 + (i % 25) * 140,
                             110 + (i / 25) * 172, Direction::Center, Direction::Center);
            ++glyphsPerPage[manager->getOrLoadGlyph(kFirstCodepoint + stride * i)->getAtlasIndex()];
            observe();
            ++completed;
            if (limit && liveBytes >= limit) break;
        }
        fbo_.end();
        observe();
        const double seconds = chrono::duration<double>(chrono::steady_clock::now() - start).count();
        const auto after = sg_query_stats().total.images;
        int maxCreations = 0;
        for (auto entry : creations) maxCreations = max(maxCreations, entry.second);
        printf("FONT_BURST glyphs=%d pages=%zu uploads=%u mip_rebuilds=%d max_per_page=%d peak_live_images=%zu peak_live_bytes=%zu seconds=%.6f\n",
               completed, font_.getAtlasCount(), after.inited - before.inited, mipRebuilds,
               maxCreations, images.size(), liveBytes, seconds);
        fflush(stdout);
        // Shelf strides include the trailing padding and round the next
        // raster origin up to 16. The final row/column need no rounded tail.
        const auto* glyph = manager->getOrLoadGlyph(kFirstCodepoint);
        const auto* page = font_.getAtlas(0);
        const int padding = int(lround(glyph->getV0() * page->getHeight()));
        const int glyphWidth = int(lround((glyph->getU1() - glyph->getU0()) * page->getWidth()));
        const int glyphHeight = int(lround((glyph->getV1() - glyph->getV0()) * page->getHeight()));
        const int cellWidth = (glyphWidth + 2 + 15) / 16 * 16;
        const int cellHeight = (glyphHeight + 4 + 15) / 16 * 16;
        const int capacity = (1 + (page->getWidth() - padding - glyphWidth - 2) / cellWidth) *
                             (1 + (page->getHeight() - padding - glyphHeight - 2) / cellHeight);
        const int unalignedCapacity = ((page->getWidth() - 2) / (glyphWidth + 2)) *
            (1 + (page->getHeight() - 2 - glyphHeight - 2) / (glyphHeight + 4));
        bool aligned = true;
        for (int i = 0; i < completed; ++i) {
            const auto* g = manager->getOrLoadGlyph(kFirstCodepoint + stride * i);
            const auto& a = manager->getAtlas(g->getAtlasIndex());
            aligned &= int(lround(g->getU0() * a.getWidth())) % 16 == 0 &&
                       int(lround(g->getV0() * a.getHeight())) % 16 == 0;
        }
        check("burst: every recorded glyph origin is 16-aligned", aligned);
        printf("FONT_ALIGNMENT capacity=%d unaligned_capacity=%d unused_area=%.4f%% unaligned_unused_area=%.4f%%\n",
               capacity, unalignedCapacity,
               100.0 * (1 - double(capacity) * glyphWidth * glyphHeight / (page->getWidth() * page->getHeight())),
               100.0 * (1 - double(unalignedCapacity) * glyphWidth * glyphHeight / (page->getWidth() * page->getHeight())));
        const size_t sanePages = (completed + capacity - 1) / capacity + 1;
        printf("FONT_PACKING variant=%s cell=%dx%d page=%dx%d capacity=%d max_pages=%zu pages=%zu glyphs_per_page=",
               g_burstPerGlyph ? "per-glyph" : "original", cellWidth, cellHeight,
               page->getWidth(), page->getHeight(), capacity, sanePages, font_.getAtlasCount());
        for (const auto& entry : glyphsPerPage) printf("%zu:%d,", entry.first, entry.second);
        printf("\n");
        check("burst: pages fill before spilling", font_.getAtlasCount() <= sanePages);
        check("burst: all requested distinct glyphs rasterized", completed == count && font_.getLoadedGlyphCount() >= size_t(count));
        check("burst: image creations/uploads per page per flush <= 1", maxCreations <= 1 &&
              after.inited - before.inited == font_.getAtlasCount() && images.size() == font_.getAtlasCount() &&
              after.alive - before.alive == images.size());
        check("burst: one mip rebuild per dirty page", mipRebuilds == (mipmaps_ ? int(font_.getAtlasCount()) : 0));
        check("burst: live images bounded by pages, not drawString calls", images.size() <= font_.getAtlasCount());
        check("burst: atlas image bytes below 128 MiB", liveBytes < 128u * 1024 * 1024);
        vector<unsigned char> px(size_t(3680) * 3680 * 4);
        check("burst: Fbo readback", fbo_.readPixels(px.data()));
        int strokes = 0, counters = 0;
        for (int i = 0; i < completed; ++i) {
            const int x = 96 + (i % 25) * 140, y = 110 + (i / 25) * 172;
            const auto* ink = &px[(size_t(y - 16) * 3680 + x - 40) * 4];
            const auto* hole = &px[(size_t(y - 16) * 3680 + x) * 4];
            if (ink[0] > 250 && abs(ink[1] - 128) <= 2 && ink[3] > 250) ++strokes;
            if (hole[3] == 0) ++counters;
        }
        check("burst: every glyph survives the one-shot pass", strokes == completed, to_string(strokes));
        check("burst: every glyph counter stays transparent", counters == completed, to_string(counters));
        const size_t burstBytes = liveBytes;
        const size_t pageImageBytes = images.empty() ? 0 : images.begin()->second;
        // A second pass adds new coverage to the same fixed-size pages. Each
        // dirty page still publishes once, and the first pass remains intact.
        creations.clear();
        const auto secondBefore = sg_query_stats().total.images;
        fbo_.begin();
        font_.drawString(utf8(kFirstCodepoint + stride * 500), -300, -300);
        font_.drawString(utf8(kFirstCodepoint + stride * 500), -300, -300);
        fbo_.end();
        observe();
        check("burst: next pass coalesces fresh glyphs", sg_query_stats().total.images.inited - secondBefore.inited == 1);
        // The new page is now published. Fresh coverage on that same page
        // must dirty it again, while earlier pass snapshots remain alive.
        const auto thirdBefore = sg_query_stats().total.images;
        later_.begin(0, 0, 0, 0);
        setColor(1, .5f, .25f);
        font_.drawString(utf8(kFirstCodepoint + stride * 501), 128, 96, Direction::Center, Direction::Center);
        font_.drawString(utf8(kFirstCodepoint + stride * 501), -300, -300);
        later_.end();
        observe();
        check("burst: fresh coverage on a published page uploads once",
              sg_query_stats().total.images.inited - thirdBefore.inited == 1);
        check("burst: later Fbo readback", later_.readPixels(px.data()));
        const auto* ink = &px[(80 * 256 + 88) * 4];
        const auto* hole = &px[(80 * 256 + 128) * 4];
        check("burst: later pass contains the newly added glyph",
              ink[0] > 250 && abs(ink[1] - 128) <= 2 && ink[3] > 250 && hole[3] == 0);
        printf("FONT_BURST_MULTIPASS peak_live_images=%zu peak_live_bytes=%zu\n", images.size(), liveBytes);
        check("burst: snapshots stay bounded across three consuming flushes",
              images.size() == sg_query_stats().total.images.alive - before.alive &&
              images.size() <= 2 * font_.getAtlasCount() && liveBytes <= burstBytes + 2 * pageImageBytes);
        burstCompleted = true;
        exitApp();
    }
};

class BurstCheckApp : public App {
public:
    void setup() override { addChild(make_shared<BurstCheckNode>()); }
};
