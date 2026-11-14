static void checkCompactionCpu() {
    using Access = internal::FontAtlasCompactionTestAccess;
    internal::FontAtlasManager three;
    Access::seed(three, 3, 256, 4096);
    three.processCompaction();
    check("compact: three frozen pages do not auto-merge", three.getAtlasCount() == 3);
    three.scheduleCompaction();
    check("compact: manual request only schedules", three.getAtlasCount() == 3 && three.getAtlasGeneration() == 0);
    auto recording = Access::retain(three, 0);
    weak_ptr<internal::AtlasState> old = recording;
    const auto original = recording->getPixels();
    three.processCompaction();
    check("compact: manual merge bumps generation once", three.getAtlasCount() == 1 && three.getAtlasGeneration() == 1);
    check("compact: old recording retains immutable bytes", !old.expired() && recording->getPixels() == original);
    recording.reset();
    check("compact: old page releases with its last recording", old.expired());
    internal::FontAtlasManager four;
    Access::seed(four, 4, 256, 4096);
    four.processCompaction();
    check("compact: four frozen pages auto-merge to smallest power of two", four.getAtlasCount() == 1 && four.getAtlas(0).getWidth() == 512);
    bool bytes = true;
    for (uint32_t i = 0; i < 4; ++i) {
        const auto* glyph = four.getOrLoadGlyph(i);
        const auto& page = four.getAtlas(glyph->getAtlasIndex());
        const int x = int(lround(glyph->getU0() * page.getWidth()));
        const int y = int(lround(glyph->getV0() * page.getHeight()));
        bytes &= x % 16 == 0 && y % 16 == 0 && page.getPixels()[size_t(y) * page.getWidth() + x] == i + 1;
    }
    check("compact: direct glyph lookup preserves bytes and alignment", bytes);
    four.processCompaction();
    check("compact: immutable merged page is not rebuilt", four.getAtlasGeneration() == 1);
    Access::seed(four, 3, 256, 4096);
    four.processCompaction();
    Access::seed(four, 3, 256, 4096);
    four.processCompaction();
    check("compact: repeated merges reclaim slack instead of doubling pages", four.getAtlasCount() == 1 && four.getAtlas(0).getWidth() == 1024 && four.getAtlasGeneration() == 3);
    internal::FontAtlasManager spaces;
    Access::seed(spaces, 4, 256, 4096);
    Access::addEmptyGlyphs(spaces, 30000);
    spaces.processCompaction();
    const auto* space = spaces.getOrLoadGlyph(0x10000);
    check("compact: empty glyphs consume no packing cells and remain cached",
        spaces.getAtlasCount() == 1 && spaces.getAtlas(0).getWidth() == 512 &&
        spaces.getLoadedGlyphCount() == 30004 && space->isValid() && space->getAtlasIndex() == 0 &&
        space->getU0() == space->getU1() && space->getV0() == space->getV1());
    internal::FontAtlasManager overflow;
    Access::seed(overflow, 9, 256, 512);
    overflow.processCompaction();
    check("compact: overflow splits into capped pages and a remainder", overflow.getAtlasCount() == 3);
    internal::FontAtlasManager maxPages;
    Access::seed(maxPages, 4, 512, 512);
    maxPages.scheduleCompaction();
    maxPages.processCompaction();
    check("compact: GPU-max pages are excluded", maxPages.getAtlasCount() == 4 && maxPages.getAtlasGeneration() == 0);
    // The actual rasterizer must continue to accept new glyphs after a merge.
    const auto font = makeSquareFont();
    internal::FontAtlasManager live;
    check("compact: live font loads", live.setupFromMemory(font.data(), font.size(), 24));
    const int inputSide = live.getAtlas(0).getWidth();
    for (uint32_t i = 0; i < 500; ++i) {
        live.getOrLoadGlyph(kFirstCodepoint + i);
        // Simulate the fixed-size part of recording, without GPU resources.
        Access::sealLast(live);
    }
    auto glyphPixels = [](internal::FontAtlasManager& manager, uint32_t codepoint) {
        const auto* glyph = manager.getOrLoadGlyph(codepoint);
        const auto& page = manager.getAtlas(glyph->getAtlasIndex());
        const int x = int(lround(glyph->getU0() * page.getWidth()));
        const int y = int(lround(glyph->getV0() * page.getHeight()));
        const int width = int(lround(glyph->getU1() * page.getWidth())) - x;
        const int height = int(lround(glyph->getV1() * page.getHeight())) - y;
        vector<uint8_t> pixels;
        for (int row = 0; row < height; ++row) {
            const auto begin = page.getPixels().begin() + size_t(y + row) * page.getWidth() + x;
            pixels.insert(pixels.end(), begin, begin + width);
        }
        return pixels;
    };
    vector<vector<uint8_t>> glyphsBefore;
    for (uint32_t i = 0; i < 500; ++i) glyphsBefore.push_back(glyphPixels(live, kFirstCodepoint + i));
    const auto activePage = Access::retain(live, live.getAtlasCount() - 1);
    const auto beforeCount = live.getAtlasCount();
    live.processCompaction();
    check("compact: active input page stays separate and small", live.getAtlasCount() < beforeCount && Access::retain(live, live.getAtlasCount() - 1) == activePage && activePage->getWidth() == inputSide);
    bool identical = true;
    for (uint32_t i = 0; i < 500; ++i) identical &= glyphsBefore[i] == glyphPixels(live, kFirstCodepoint + i);
    check("compact: all 500 rasterized glyphs retain identical bytes", identical);
    check("compact: new glyph remains valid after merging", live.getOrLoadGlyph(kFirstCodepoint + 500)->isValid());

    const auto path = filesystem::temp_directory_path() / "fontAtlasR8_spaces.ttf";
    {
        ofstream output(path, ios::binary);
        output.write(reinterpret_cast<const char*>(font.data()), font.size());
    }
    Font spaceFont;
    check("batch spaces: font loads", bool(spaceFont.load(path, 24)));
    GlyphBatch batch;
    batch.setFont(spaceFont);
    for (int i = 0; i < 60000; ++i) batch.add(uint32_t(' '), Mat4::identity(), Color(1));
    check("batch spaces: zero-area glyphs are not retained",
        Access::placementCount(batch) == 0 && Access::placementCapacity(batch) == 0);
    batch.clear();
    check("batch spaces: clear keeps no placement capacity", Access::placementCapacity(batch) == 0);
    filesystem::remove(path);
}

#include "compactGapThresholds.h"

bool compactCompleted = false;
bool compactOrientationOnly = false;
class CompactCheckApp : public App {
    Font font_;
    GlyphBatch batch_;
    Fbo target_, fill_;
    shared_ptr<internal::FontAtlasManager> manager_;
    static constexpr int width = 1280, rowHeight = 112, rows = 9, height = 8 + rows * rowHeight;
    vector<uint8_t> before_, main_;
    size_t case_ = 0;
    const CompactGapCase& gapCase() const { return compactGapCases[case_]; }
    float comparisonScale(int row) const {
        if (row == 0) return 1;
        if (row < 7) return std::ldexp(1.0f, -row - 1);
        return float(gapCase().threshold / gapCase().side * (row == 7 ? 1.0001 : .99));
    }
    int frame_ = 0;
    uint64_t generation_ = 0;
    size_t pages_ = 0;
    void render() {
        target_.begin(0, 0, 0, 0);
        setColor(1);
        // L0–L6, just above the continuous threshold, and just below it.
        for (int i = 0; i < rows; ++i) {
            // Multiple pixel phases sample edges as well as glyph centers.
            const float phases[] = {0, .125f, .375f, .625f};
            for (int phase = 0; phase < (i == 0 ? 1 : 4); ++phase) {
                const float x = 8 + phase * 144 + phases[phase];
                pushMatrix();
                translate(x, 8 + i * rowHeight + phases[phase]);
                scale(comparisonScale(i));
                font_.drawString(text(), 0, 24, Direction::Left, Direction::Baseline);
                popMatrix();
                batch_.draw(Mat4::translate(width / 2 + x, float(8 + i * rowHeight) + phases[phase], 0) * Mat4::scale(comparisonScale(i), comparisonScale(i), 1));
            }
        }
        target_.end();
    }
    static string text() {
        string result;
        for (uint32_t i = 0; i < 8; ++i) result += utf8(kFirstCodepoint + i);
        return result;
    }
    void checkPublishedRows(const vector<uint8_t>& pixels) {
        int different = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            const auto* glyph = manager_->getOrLoadGlyph(kFirstCodepoint + i);
            const auto& page = manager_->getAtlas(glyph->getAtlasIndex());
            const int x = int(lround(glyph->getU0() * page.getWidth()));
            const int y = int(lround(glyph->getV0() * page.getHeight()));
            for (int row = 0; row < gapCase().side; ++row) for (int col = 0; col < gapCase().side; ++col) {
                const int expected = page.getPixels()[size_t(y + row) * page.getWidth() + x + col];
                const int actual = pixels[(size_t(32 + row) * width + 8 + i * (gapCase().side + 2) + col) * 4 + 3];
                different += abs(actual - expected) > 1;
            }
        }
        check("compact orientation: published UVs sample the expected asymmetric rows",
            different == 0, "different texels=" + to_string(different));
    }
public:
    void setup() override {
        font_.setOversampling(1);
        check("compact display: font loads", bool(font_.load(g_fontPath, 24)));
        GlyphBatch spaces;
        spaces.setFont(font_);
        spaces.add(string(30000, ' '));
        check("batch spaces: string layout retains no zero-area glyphs",
            internal::FontAtlasCompactionTestAccess::placementCount(spaces) == 0 &&
            internal::FontAtlasCompactionTestAccess::placementCapacity(spaces) == 0);
        manager_ = internal::SharedFontCache::getInstance().get(internal::FontCacheKey{g_fontPath, 24, 0, true, 1});
        target_.allocate(width, height);
        fill_.allocate(64, 64);
    }
    void draw() override {
        if (!manager_) { exitApp(); return; }
        if (frame_++ % 2 == 0) {
            if (compactOrientationOnly) case_ = 5; // Original 30-texel asymmetric fixture.
            // Main uses the same CPU R8 mip builder with a fixed two-texel gap.
            internal::FontAtlasCompactionTestAccess::seedMixed(*manager_, gapCase().side, true);
            batch_.setFont(font_);
            batch_.clear();
            batch_.add(text());
            render();
            main_.resize(width * height * 4);
            check("compact display: main-layout readback", target_.readPixels(main_.data()));
            if (const char* directory = getenv("TC_MAIN_REFERENCE_DIR")) {
                ifstream file(filesystem::path(directory) / ("main-" + to_string(gapCase().side) + ".rgba"), ios::binary);
                vector<uint8_t> reference(main_.size());
                file.read(reinterpret_cast<char*>(reference.data()), reference.size());
                check("compact display: origin/main reference readback is complete", size_t(file.gcount()) == reference.size());
                check("compact display: fixed-gap fixture reproduces origin/main", reference == main_);
                main_ = std::move(reference);
            }
            internal::FontAtlasCompactionTestAccess::seedMixed(*manager_, gapCase().side);
            fill_.begin(0, 0, 0, 0);
            font_.drawString(text(), 0, 32);
            const auto beforeRequest = manager_->getAtlasGeneration();
            const bool inPass = internal::currentTarget().inPass;
            font_.compactAtlas();
            check("compact display: request preserves the active pass and generation",
                internal::currentTarget().inPass == inPass && manager_->getAtlasGeneration() == beforeRequest);
            fill_.end();
            batch_.setFont(font_);
            batch_.clear();
            batch_.add(text());
            render();
            before_.resize(width * height * 4);
            check("compact display: baseline readback", target_.readPixels(before_.data()));
            for (int row = 0; row < rows; ++row) for (int engine = 0; engine < 2; ++engine) {
                bool ink = false;
                for (int y = 8 + row * rowHeight; y < 8 + row * rowHeight + rowHeight; ++y)
                    for (int x = engine * (width / 2); x < engine * (width / 2) + width / 2; ++x)
                        ink |= before_[(size_t(y) * width + x) * 4 + 3] != 0;
                const bool asserted = gapCase().side * comparisonScale(row) >= gapCase().threshold;
                printf("COMPACT_INK side=%d row=%d engine=%d ink=%d %s\n", gapCase().side, row, engine, ink, asserted ? "asserted" : "recorded");
                if (asserted) check("compact display: asserted comparison contains ink", ink);
            }
            checkPublishedRows(before_);
            generation_ = manager_->getAtlasGeneration();
            pages_ = font_.getAtlasCount();
            font_.compactAtlas();
            check("compact display: public API schedules without mid-pass work", manager_->getAtlasGeneration() == generation_);
            return;
        }
        check("compact display: frame boundary merges frozen pages", manager_->getAtlasGeneration() == generation_ + 1 && font_.getAtlasCount() < pages_);
        render();
        vector<uint8_t> after(before_.size());
        check("compact display: merged readback", target_.readPixels(after.data()));
        checkPublishedRows(after);
        for (int row = 0; !compactOrientationOnly && row < rows; ++row) {
            const double pixels = gapCase().side * comparisonScale(row);
            for (int engine = 0; engine < 2; ++engine) for (int reference = 0; reference < 2; ++reference) {
                const auto& baseline = reference ? main_ : before_;
                const double threshold = reference ? std::max(gapCase().threshold, gapCase().mainThreshold) : gapCase().threshold;
                const bool asserted = pixels >= threshold;
                int maxDifference = 0, different = 0, aboveTolerance = 0;
                for (int y = 8 + row * rowHeight; y < 8 + (row + 1) * rowHeight; ++y)
                    for (int x = engine * (width / 2); x < (engine + 1) * (width / 2); ++x) {
                        int delta = 0;
                        for (int c = 0; c < 4; ++c) {
                            const size_t offset = (size_t(y) * width + x) * 4 + c;
                            delta = std::max(delta, abs(int(after[offset]) - baseline[offset]));
                        }
                        maxDifference = std::max(maxDifference, delta);
                        different += delta != 0;
                        aboveTolerance += delta > 1;
                    }
                printf("COMPACT_PIXELS side=%d row=%d scale=%.9g pixels=%.9g threshold=%.9g engine=%s reference=%s %s different_pixels=%d above_tolerance=%d max=%d\n",
                    gapCase().side, row, comparisonScale(row), pixels, threshold, engine ? "GlyphBatch" : "drawString",
                    reference ? (getenv("TC_MAIN_REFERENCE_DIR") ? "origin/main" : "main-layout") : "before", asserted ? "asserted" : "recorded", different, aboveTolerance, maxDifference);
                // Below the model's two-sided threshold, record differences without failing.
                if (asserted) check("compact display: side " + to_string(gapCase().side) + " row " + to_string(row) +
                    " engine " + to_string(engine) + " reference " + to_string(reference), aboveTolerance == 0);
            }
        }
        if (!compactOrientationOnly && ++case_ < std::size(compactGapCases)) return;
        compactCompleted = true;
        exitApp();
    }
};

static void benchmarkCompaction(bool gpu) {
    const int cap = gpu ? internal::FontAtlasManager::atlasLimit() : 8192;
    if (cap < 2048) { check("compact benchmark: GPU supports the legacy page size", false); return; }
    internal::FontAtlasManager manager;
    internal::FontAtlasCompactionTestAccess::seedBenchmark(manager, cap);
    vector<shared_ptr<internal::AtlasState>> recordings;
    for (size_t i = 0; i < manager.getAtlasCount(); ++i) {
        auto page = internal::FontAtlasCompactionTestAccess::retain(manager, i);
        if (gpu) internal::FontAtlasManager::publishAtlas(*page);
        recordings.push_back(std::move(page));
    }
#if defined(__linux__) && !defined(__ANDROID__)
    if (gpu && (sg_query_backend() == SG_BACKEND_GLCORE || sg_query_backend() == SG_BACKEND_GLES3)) glFinish();
#endif
    const size_t beforeBytes = manager.getMemoryUsage();
    const auto start = chrono::steady_clock::now();
    manager.scheduleCompaction();
    manager.processCompaction();
#if defined(__linux__) && !defined(__ANDROID__)
    if (gpu && (sg_query_backend() == SG_BACKEND_GLCORE || sg_query_backend() == SG_BACKEND_GLES3)) glFinish();
#endif
    const double milliseconds = chrono::duration<double, milli>(chrono::steady_clock::now() - start).count();
    long peakKiB = 0;
#if defined(__linux__) && !defined(__ANDROID__)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    peakKiB = usage.ru_maxrss;
#endif
    printf("compact benchmark: gpu=%d source_pages=90 source_side=2048 glyphs=500 cap=%d before_bytes=%zu after_bytes=%zu result_pages=%zu milliseconds=%.3f peak_rss_kib=%ld\n",
        gpu, cap, beforeBytes, manager.getMemoryUsage(), manager.getAtlasCount(), milliseconds, peakKiB);
    int side = 2048;
    while (side <= cap / 2) side *= 2;
    const int gap = std::max(2, (704 + 7) / 8);
    const int strideX = (608 + gap + 15) & ~15;
    const int strideY = (704 + gap + 15) & ~15;
    const size_t capacity = size_t(1 + (side - 16 - 608 - gap) / strideX) *
        (1 + (side - 16 - 704 - gap) / strideY);
    const size_t expected = side == 2048 ? 90 : (500 + capacity - 1) / capacity;
    check("compact benchmark: 500 legacy glyphs respect the GPU cap",
        manager.getAtlasCount() == expected && manager.getAtlasGeneration() == (side > 2048 ? 1u : 0u));
}
class CompactBenchmarkApp : public App {
    EventListener after_;
public:
    void setup() override {
        // afterFrame follows endGpuFrame, with no target pass open.
        after_ = events().afterFrame.listen([] {
            benchmarkCompaction(true);
            exitApp();
        });
    }
};
