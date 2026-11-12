#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#ifdef TC_GLYPH_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#endif

using namespace tc;
namespace {
int failures = 0;
bool completed = false;
std::string testFontName = TC_FONT_SANS;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
bool sameMatrix(const Mat4& a, const Mat4& b) {
    return std::equal(a.m, a.m + 16, b.m);
}
constexpr int extent = 192;
const Color tint(0.8f, 0.4f, 0.2f, 0.7f);
using Bytes = std::vector<unsigned char>;
Bytes read(Fbo& target) {
    Bytes pixels(extent * extent * 4);
    check("pixel readback", target.readPixels(pixels.data()));
    return pixels;
}
void compare(const char* label, const Bytes& reference, const Bytes& actual, int tolerance = 2) {
    int worst = 0, different = 0, lit = 0;
    for (size_t i = 0; i < reference.size(); ++i) {
        worst = std::max(worst, std::abs(int(reference[i]) - int(actual[i])));
        if (std::abs(int(reference[i]) - int(actual[i])) > tolerance) ++different;
        if (reference[i]) ++lit;
    }
    std::printf("PIXELS %s max=%d different=%d lit=%d\n", label, worst, different, lit);
    check(label, different == 0 && lit > 0);
}

class GlyphApp : public App {
    Font font_;
    Fbo reference_, actual_;
    GlyphBatch retained_;
    sg_buffer retainedVertices_{}, retainedIndices_{};
    int frame_ = 0;

    template<class Fn> Bytes render(Fbo& fbo, Fn fn) {
        fbo.begin(0, 0, 0, 0);
        setupScreenOrtho();
        setColor(tint);
        setBlendMode(BlendMode::Alpha);
        setTextAlign(Direction::Left, Direction::Top);
        fn();
        fbo.end();
        return read(fbo);
    }
    void scissorChecks() {
        GlyphBatch batch; batch.setFont(font_);
        batch.add(U'A', Mat4::translate(40, 40, 0), tint);
        const auto expected = render(reference_, [&] {
            pushScissor(0, 0, 1, 1); font_.drawString("A", 40, 40); popScissor();
        });
        const auto got = render(actual_, [&] {
            pushScissor(0, 0, 1, 1); batch.draw(); popScissor();
        });
        const auto nonzero = [](const Bytes& pixels) {
            return std::count_if(pixels.begin(), pixels.end(), [](unsigned char value) { return value != 0; });
        };
        std::printf("SCISSOR reference_nonzero=%zu batch_nonzero=%zu\n",
                    size_t(nonzero(expected)), size_t(nonzero(got)));
        check("batch-only pass respects captured scissor", nonzero(expected) == 0 && expected == got);

        const auto scene = [&](bool batched, bool mixed) {
            if (mixed) drawRect(10, 10, 8, 8);
            pushScissor(45, 45, 8, 8);
            if (batched) batch.draw(); else font_.drawString("A", 40, 40);
            if (mixed) drawRect(48, 48, 12, 12);
            popScissor();
            if (batched) batch.draw(Mat4::translate(60, 0, 0));
            else font_.drawString("A", 100, 40);
            if (mixed) drawRect(140, 40, 8, 8);
        };
        for (bool mixed : {false, true}) {
            const auto reference = render(reference_, [&] { scene(false, mixed); });
            const auto actual = render(actual_, [&] { scene(true, mixed); });
            compare(mixed ? "mixed sgl/batch scissor and restoration" : "batch-only changing scissor", reference, actual);
        }
    }
    void pageOrderChecks() {
        auto file = systemFontPath(testFontName);
        const bool fallback = file.empty();
        if (fallback) file = getDataPath(utf8ToPath(testFontName));
        std::error_code error;
        if (!fs::is_regular_file(file, error)) {
            std::printf("SKIP pageOrderChecks: no local font; pass --font <path>\n");
            return;
        }
        const auto path = pathToUtf8(file);
        std::printf("PAGE_ORDER font=%s source=%s\n", path.c_str(), fallback ? "--font fallback" : "system");
        // Publish through the normal cache path. Fill until a glyph actually
        // lands on another page instead of assuming a font's glyph dimensions.
        internal::FontCacheKey key{path, 27};
        key.oversample = Font::getDefaultOversampling();
        auto manager = std::make_shared<internal::FontAtlasManager>();
        manager->setOversample(key.oversample); manager->setMipmaps(true);
        const bool prepared = manager->prepareFile(path, 27, 0, 256);
        check("small atlas prepared", prepared);
        if (!prepared) return;
        internal::SharedFontCache::getInstance().publishPrepared(key, manager);
        Font multi;
        const bool loaded = bool(multi.load(path, 27));
        check("small atlas font loads", loaded);
        if (!loaded) return;
        manager->getOrLoadGlyph(U'A');
        uint32_t second = 0;
        int filled = 0;
        // Scan below the BMP surrogate range, stopping at 4096 nonempty glyphs.
        for (uint32_t cp = 33; cp < 0xD800 && filled < 4096; ++cp) {
            if (!manager->fontHasGlyph(cp)) continue;
            const auto* glyph = manager->getOrLoadGlyph(cp);
            if (!glyph->isValid() || glyph->getWidth() <= 0 || glyph->getHeight() <= 0) continue;
            ++filled;
            if (glyph->getAtlasIndex() > 0) { second = cp; break; }
        }
        const auto* a = manager->getOrLoadGlyph(U'A');
        check("fill found a glyph on a second atlas page (limit: 4096 glyphs below U+D800)", second != 0);
        if (!second) return;
        check("A and B occupy pages zero and one", a->isValid() && a->getWidth() > 0 &&
            a->getAtlasIndex() == 0 && manager->getOrLoadGlyph(second)->getAtlasIndex() == 1);
        // The bounded scan selects a BMP codepoint; encode it for drawString.
        std::string secondText;
        if (second < 0x80) secondText += char(second);
        else {
            if (second >= 0x800) secondText += char(0xE0 | (second >> 12));
            secondText += char((second < 0x800 ? 0xC0 : 0x80) | ((second >> 6) & 0x3F));
            secondText += char(0x80 | (second & 0x3F));
        }
        const Color red(1, 0, 0, 0.8f), blue(0, 0, 1, 0.8f), green(0, 1, 0, 0.8f);
        const auto expected = render(reference_, [&] {
            setColor(red); multi.drawString("A", 40, 40);
            setColor(blue); multi.drawString(secondText, 40, 40);
            setColor(green); multi.drawString("A", 40, 40);
        });
        const auto before = sg_query_stats().cur_frame.num_draw;
        const auto got = render(actual_, [&] {
            GlyphBatch batch; batch.setFont(multi);
            const auto matrix = Mat4::translate(40, 40, 0);
            batch.add(U'A', matrix, red); batch.add(second, matrix, blue); batch.add(U'A', matrix, green);
            batch.draw();
        });
        const auto draws = sg_query_stats().cur_frame.num_draw - before;
        std::printf("PAGE_ORDER pages=%zu draws=%u B=U+%04X filled=%d A_size=%.1fx%.1f\n",
                    multi.getAtlasCount(), draws, second, filled, a->getWidth(), a->getHeight());
        compare("interleaved page painter order", expected, got);
        check("A/B/A issues three consecutive run draws", draws == 3);
        const auto singleBefore = sg_query_stats().cur_frame.num_draw;
        render(actual_, [&] {
            GlyphBatch batch; batch.setFont(multi);
            batch.add(std::string("AAA"), tint); batch.draw();
        });
        check("consecutive glyphs on one page issue one draw", sg_query_stats().cur_frame.num_draw - singleBefore == 1);
    }
    void layoutChecks() {
        const std::string text = "Ag 12\nxy";
        for (auto mode : {WritingMode::Horizontal, WritingMode::VerticalRL}) {
            font_.setWritingMode(mode);
            font_.setTcyDigits(2, TcyMode::Combine, TcyMode::Rotate);
            for (auto h : {Direction::Left, Direction::Center, Direction::Right})
            for (auto v : {Direction::Top, Direction::Center, Direction::Bottom, Direction::Baseline}) {
                auto expected = render(reference_, [&] { font_.drawString(text, 96, 96, h, v); });
                auto got = render(actual_, [&] {
                    GlyphBatch batch;
                    batch.setFont(font_); batch.setAlign(h, v); batch.setWritingMode(mode);
                    pushMatrix(); translate(96, 96); batch.add(text, tint); popMatrix();
                    batch.draw();
                });
                compare(mode == WritingMode::Horizontal ? "horizontal alignment" : "vertical/TCY alignment", expected, got);
            }
        }
        font_.setWritingMode(WritingMode::Horizontal);
        font_.enableWrap(true); font_.setMaxLineLength(65);
        auto expected = render(reference_, [&] { font_.drawString("wrap words here", 20, 20); });
        auto got = render(actual_, [&] {
            GlyphBatch batch; batch.setFont(font_);
            pushMatrix(); translate(20, 20); batch.add(std::string("wrap words here"), tint); popMatrix(); batch.draw();
        });
        compare("shared wrapping layout", expected, got);
        font_.enableWrap(false);
        // Single glyph overload ignores vertical writing mode.
        for (auto h : {Direction::Left, Direction::Center, Direction::Right})
        for (auto v : {Direction::Top, Direction::Center, Direction::Bottom, Direction::Baseline}) {
            expected = render(reference_, [&] { font_.drawString("g", 96, 96, h, v); });
            got = render(actual_, [&] {
                GlyphBatch batch; batch.setFont(font_); batch.setWritingMode(WritingMode::VerticalRL); batch.setAlign(h, v);
                batch.add(U'g', Mat4::translate(96, 96, 0), tint); batch.draw();
            });
            compare("independent glyph alignment", expected, got);
        }
        const Rect ink = font_.getGlyphInkBox(U'g');
        check("ink includes descender and half-size", ink.y < 0 && ink.getBottom() > 0 && ink.width / 2 > 0);
        check("space ink is empty", font_.getGlyphInkBox(U' ').width == 0);
        expected = render(reference_, [&] {
            font_.drawString("g", 96 - ink.getCenterX(), 96 - ink.getCenterY(), Direction::Left, Direction::Baseline);
        });
        got = render(actual_, [&] {
            GlyphBatch batch; batch.setFont(font_); batch.setAlignToInk(true);
            batch.add(U'g', Mat4::translate(96, 96, 0), tint); batch.draw();
        });
        compare("ink center pivot", expected, got);
        for (float factor : {0.25f, 0.125f}) {
            expected = render(reference_, [&] {
                pushMatrix(); translate(60, 60); scale(factor); font_.drawString("Minified text", 0, 0); popMatrix();
            });
            got = render(actual_, [&] {
                GlyphBatch batch; batch.setFont(font_);
                pushMatrix(); translate(60, 60); scale(factor); batch.add(std::string("Minified text"), tint); popMatrix(); batch.draw();
            });
            compare("4x/8x minification with mips", expected, got);
        }
    }
    void batchSettingsChecks() {
        const Mat4 current = Mat4::translate(65, 60, 0) * Mat4::rotateZ(0.17f);
        const Color overrideColor(0.2f, 0.7f, 0.9f, 0.8f);
        // Both directions across the grid-fit boundary, including a final 1:1.
        for (float factor : {0.5f, 2.0f}) {
            const Mat4 transform = Mat4::translate(3, 5, 0) * Mat4::scale(factor);
            const Mat4 explicitModel = Mat4::translate(105, 95, 0) * Mat4::scale(1 / factor);
            for (int form = 0; form < 4; ++form) {
                const bool explicitMatrix = form == 1 || form == 3;
                const bool explicitColor = form >= 2;
                const auto expected = render(reference_, [&] {
                    GlyphBatch batch; batch.setFont(font_);
                    batch.add(U'g', explicitMatrix ? explicitModel : current * transform,
                              explicitColor ? overrideColor : tint);
                    batch.draw();
                });
                const auto actual = render(actual_, [&] {
                    GlyphBatch batch; batch.setFont(font_);
                    batch.setTransform(transform); batch.setColor(tint);
                    pushMatrix(); setMatrix(current);
                    switch (form) {
                        case 0: batch.add(U'g'); break;
                        case 1: batch.add(uint32_t(U'g'), explicitModel); break;
                        case 2: batch.add(uint32_t(U'g'), overrideColor); break;
                        case 3: batch.add(U'g', explicitModel, overrideColor); break;
                    }
                    popMatrix(); batch.draw();
                });
                compare("codepoint overload uses final matrix and selected color", expected, actual);
            }
            for (auto mode : {WritingMode::Horizontal, WritingMode::VerticalRL}) {
                font_.setWritingMode(mode);
                for (bool explicitColor : {false, true}) {
                    const auto expected = render(reference_, [&] {
                        GlyphBatch explicitBatch; explicitBatch.setFont(font_);
                        pushMatrix(); setMatrix(current * transform);
                        explicitBatch.add(std::string("Ag\n12"), explicitColor ? overrideColor : tint);
                        popMatrix(); explicitBatch.draw();
                    });
                    const auto actual = render(actual_, [&] {
                        GlyphBatch batch; batch.setFont(font_);
                        batch.setTransform(transform); batch.setColor(tint);
                        pushMatrix(); setMatrix(current);
                        if (explicitColor) batch.add(std::string("Ag\n12"), overrideColor);
                        else batch.add("Ag\n12");
                        check("string add preserves global matrix", sameMatrix(getMatrix(), current));
                        popMatrix(); batch.draw();
                    });
                    compare("string overload uses final matrix for layout", expected, actual);
                }
            }
            font_.setWritingMode(WritingMode::Horizontal);
        }
        // A local inverse scale returns the final model to the grid-fit range.
        const auto fittedReference = render(reference_, [&] {
            font_.drawString("Ag\nxy", 70, 60);
        });
        const auto fittedActual = render(actual_, [&] {
            GlyphBatch batch; batch.setFont(font_); batch.setTransform(Mat4::scale(2)); batch.setColor(tint);
            pushMatrix(); translate(70, 60); scale(0.5f); batch.add("Ag\nxy"); popMatrix(); batch.draw();
        });
        compare("string grid fit follows final 1:1 product", fittedReference, fittedActual);

        for (float factor : {0.5f, 1.0f, 2.0f}) {
            const Mat4 finalModel = Mat4::translate(65, 60, 0) * Mat4::scale(factor);
            const auto expected = render(reference_, [&] {
                pushMatrix(); setMatrix(finalModel); font_.drawString("g", 0, 0); popMatrix();
            });
            const auto actual = render(actual_, [&] {
                GlyphBatch glyph; glyph.setFont(font_); glyph.setTransform(Mat4::scale(3));
                // Ambient and batch scales must not affect an explicit matrix.
                pushMatrix(); scale(4); glyph.add(U'g', finalModel, tint); popMatrix(); glyph.draw();
            });
            compare("explicit glyph grid fit follows supplied matrix", expected, actual);
        }
        GlyphBatch batch; batch.setFont(font_);
        check("default transform and color", sameMatrix(batch.getTransform(), Mat4::identity()) && batch.getColor() == Color(1, 1, 1, 1));
        const Mat4 transform = Mat4::scale(1.5f);
        batch.setTransform(transform); batch.setColor(tint);
        const auto expected = render(reference_, [&] {
            GlyphBatch explicitBatch; explicitBatch.setFont(font_);
            explicitBatch.add(U'A', Mat4::translate(25, 30, 0) * transform, tint);
            explicitBatch.add(U'B', Mat4::translate(75, 30, 0), overrideColor);
            explicitBatch.add(U'C', Mat4::translate(115, 30, 0) * transform, tint);
            explicitBatch.draw();
        });
        const auto actual = render(actual_, [&] {
            pushMatrix(); translate(25, 30); batch.add(U'A'); popMatrix();
            batch.add(U'B', Mat4::translate(75, 30, 0), overrideColor);
            pushMatrix(); translate(115, 30); batch.add(U'C'); popMatrix(); batch.draw();
        });
        compare("settings persist across adds and explicit replacements", expected, actual);
        batch.clear();
        check("clear keeps transform and color", sameMatrix(batch.getTransform(), transform) && batch.getColor() == tint);
        auto cleared = render(actual_, [&] {
            pushMatrix(); translate(65, 60); batch.add(U'g'); popMatrix(); batch.draw();
        });
        auto explicitResult = render(reference_, [&] {
            GlyphBatch explicitBatch; explicitBatch.setFont(font_);
            explicitBatch.add(U'g', Mat4::translate(65, 60, 0) * transform, tint); explicitBatch.draw();
        });
        compare("settings affect adds after clear", explicitResult, cleared);
        batch.resetTransform();
        check("resetTransform keeps color", sameMatrix(batch.getTransform(), Mat4::identity()) && batch.getColor() == tint);
        batch.setTransform(transform); batch.resetColor();
        check("resetColor keeps transform", sameMatrix(batch.getTransform(), transform) && batch.getColor() == Color(1, 1, 1, 1));
        batch.resetTransform(); batch.clear();
        cleared = render(actual_, [&] {
            pushMatrix(); translate(65, 60); batch.add(U'g'); popMatrix(); batch.draw();
        });
        explicitResult = render(reference_, [&] {
            GlyphBatch explicitBatch; explicitBatch.setFont(font_);
            explicitBatch.add(U'g', Mat4::translate(65, 60, 0), Color(1, 1, 1, 1)); explicitBatch.draw();
        });
        compare("resets restore identity and white for later adds", explicitResult, cleared);

        const auto inkCenter = [](const Bytes& pixels) {
            int x0 = extent, y0 = extent, x1 = -1, y1 = -1;
            for (int y = 0; y < extent; ++y) for (int x = 0; x < extent; ++x) {
                if (pixels[(y * extent + x) * 4 + 3] < 128) continue;
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
            check("scaled glyph has visible ink", x1 >= x0 && y1 >= y0);
            return Vec2((x0 + x1 + 1) * 0.5f, (y0 + y1 + 1) * 0.5f);
        };
        Vec2 centers[2];
        for (int i = 0; i < 2; ++i) {
            const auto pixels = render(actual_, [&] {
                GlyphBatch centered; centered.setFont(font_); centered.setAlignToInk(true);
                centered.setTransform(Mat4::scale(i == 0 ? 1.0f : 2.0f));
                pushMatrix(); translate(96, 96); centered.add(U'g'); popMatrix(); centered.draw();
            });
            centers[i] = inkCenter(pixels);
        }
        std::printf("INK_CENTERS identity=(%.1f,%.1f) scale2=(%.1f,%.1f)\n",
                    centers[0].x, centers[0].y, centers[1].x, centers[1].y);
        check("batch scaling keeps ink center fixed in pixels",
              std::fabs(centers[0].x - centers[1].x) <= 1 && std::fabs(centers[0].y - centers[1].y) <= 1 &&
              std::fabs(centers[1].x - 96) <= 1 && std::fabs(centers[1].y - 96) <= 1);
    }
    void cameraCheck() {
        EasyCam camera; camera.setDistance(200);
        GlyphBatch batch; batch.setFont(font_); batch.setAlign(Direction::Center, Direction::Baseline);
        auto drawScene = [&](bool batched, bool record) {
            camera.begin(); enableDepthTest();
            for (int i = 0; i < 8; ++i) {
                pushMatrix(); translate(float(i * 3) - 10.5f, float(i % 2 * 2), float(i % 3 * 12));
                rotateY(0.25f * i); rotateZ(0.1f * i); scale(0.25f);
                const Color glyphColor(0.2f + 0.08f * i, 0.4f, 0.8f - 0.05f * i, 0.7f);
                if (batched) { if (record) batch.add(U'R', glyphColor); }
                else { setColor(glyphColor); font_.drawString("R", 0, 0, Direction::Center, Direction::Baseline); }
                popMatrix();
            }
            if (batched) {
                const auto before = sg_query_stats();
                batch.draw();
                if (!record) check("camera changes need zero geometry uploads",
                    sg_query_stats().total.buffers.inited == before.total.buffers.inited &&
                    sg_query_stats().cur_frame.num_write_buffer_transient == before.cur_frame.num_write_buffer_transient &&
                    sg_query_stats().cur_frame.num_write_buffer_unsealed == before.cur_frame.num_write_buffer_unsealed);
            }
            disableDepthTest(); camera.end();
        };
        const auto expected = render(reference_, [&] { drawScene(false, false); });
        const auto got = render(actual_, [&] { drawScene(true, true); });
        compare("EasyCam 3D rotated glyphs/depth", expected, got);
        camera.setDistance(260);
        const auto movedReference = render(reference_, [&] { drawScene(false, false); });
        const auto movedBatch = render(actual_, [&] { drawScene(true, false); });
        compare("retained glyphs follow the current EasyCam view", movedReference, movedBatch);
    }
    void growthAndTransform() {
        // A new cache key keeps this growth test cold after the layout tests.
        Font growing; check("growth font", static_cast<bool>(growing.load(testFontName, 31)));
        GlyphBatch batch; batch.setFont(growing);
        sg_view recorded{};
        auto got = render(actual_, [&] {
            pushMatrix(); translate(20, 20); batch.add(U'A', tint); popMatrix();
            batch.draw(); // record page 0 before adding more coverage
            const int before = growing.getAtlas(0)->getWidth();
            for (uint32_t cp = 33; cp < 600; ++cp) batch.add(cp, Mat4::translate(-5000, -5000, 0), tint);
            check("recorded atlas page keeps its size", growing.getAtlas(0)->getWidth() == before);
            batch.draw(Mat4::translate(65, 45, 0));
            recorded = internal::currentTarget().deferredShaderDraws.back().bindings.views[0];
        });
        check("mip chain built at the flush", sg_query_image_desc(sg_query_view_desc(recorded).texture.image).num_mipmaps > 1);
        const auto expected = render(reference_, [&] {
            growing.drawString("A", 20, 20);
            growing.drawString("A", 85, 65);
        });
        compare("growth, queued old view, draw(transform)", expected, got);
        got = render(actual_, [&] { batch.clear(); batch.draw(); });
        check("clear removes geometry", std::all_of(got.begin(), got.end(), [](unsigned char value) { return value == 0; }));
    }
    void targetAndBlendChecks() {
        for (auto format : {TextureFormat::RGBA8, TextureFormat::RGBA16F}) for (int samples : {1, 4}) {
            reference_.allocate(extent, extent, samples, format); actual_.allocate(extent, extent, samples, format);
            auto expected = render(reference_, [&] { font_.drawString("Fbo", 40, 60); });
            auto got = render(actual_, [&] {
                GlyphBatch batch; batch.setFont(font_);
                batch.add(std::string("Fbo"), tint);
                batch.draw(Mat4::translate(40, 60, 0));
                const auto desc = sg_query_pipeline_desc(internal::currentTarget().deferredShaderDraws.back().pipeline);
                check("target format/MSAA pipeline", desc.colors[0].pixel_format == (format == TextureFormat::RGBA8 ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_RGBA16F) && desc.sample_count == samples);
            });
            compare("target format/MSAA pixels", expected, got);
        }
        actual_.allocate(extent, extent, 1, TextureFormat::RGBA16F);
        render(actual_, [&] {
            GlyphBatch mask; mask.setFont(font_); mask.add(U'M', Mat4::translate(40, 40, 0), Color(1)); mask.draw();
        });
        std::vector<float> maskPixels(extent * extent * 4);
        check("coverage mask readback", actual_.readPixelsFloat(maskPixels.data()));
        size_t probe = 0;
        for (size_t i = 0; i < maskPixels.size(); i += 4)
            if (maskPixels[i + 3] > maskPixels[probe + 3]) probe = i;
        check("blend probe is inside glyph ink", maskPixels[probe + 3] > 0.99f);
        const Color source(3, 1.5f, 0.5f, 0.5f), background(0.2f, 0.3f, 0.4f, 0.6f);
        for (auto mode : {BlendMode::Alpha, BlendMode::Add, BlendMode::Screen, BlendMode::Multiply, BlendMode::Subtract, BlendMode::Disabled}) {
            actual_.begin(background.r, background.g, background.b, background.a); setupScreenOrtho();
            GlyphBatch batch; batch.setFont(font_); batch.add(U'M', Mat4::translate(40, 40, 0), source);
            setBlendMode(mode); batch.draw();
            const auto desc = sg_query_pipeline_desc(internal::currentTarget().deferredShaderDraws.back().pipeline);
            const auto recipe = internal::pipeDesc2D(mode);
            check("blend selected at draw", desc.colors[0].blend.src_factor_rgb == recipe.colors[0].blend.src_factor_rgb || mode == BlendMode::Disabled);
            actual_.end();
            std::vector<float> pixels(extent * extent * 4);
            check("float readback", actual_.readPixelsFloat(pixels.data()));
            const float alpha = source.a * maskPixels[probe + 3];
            const float src[] = {source.r, source.g, source.b}, dst[] = {background.r, background.g, background.b};
            bool matches = true;
            for (size_t channel = 0; channel < 3; ++channel) {
                float expected = 0;
                switch (mode) {
                    case BlendMode::Alpha: expected = src[channel]*alpha + dst[channel]*(1-alpha); break;
                    case BlendMode::Add: expected = src[channel]*alpha + dst[channel]; break;
                    case BlendMode::Screen: expected = src[channel]*alpha + dst[channel]*(1-src[channel]*alpha); break;
                    case BlendMode::Multiply: expected = src[channel]*alpha*dst[channel] + dst[channel]*(1-alpha); break;
                    case BlendMode::Subtract: expected = dst[channel]-src[channel]*alpha; break;
                    case BlendMode::Disabled: expected = src[channel]; break;
                    // Custom equations have their own oracle in customBlend.
                    case BlendMode::Custom: matches = false; break;
                }
                matches &= std::fabs(pixels[probe+channel]-expected) < 0.004f;
            }
            const float expectedAlpha = mode == BlendMode::Disabled ? alpha :
                ((mode == BlendMode::Add || mode == BlendMode::Subtract) ? alpha + background.a : alpha + background.a*(1-alpha));
            matches &= std::fabs(pixels[probe+3]-expectedAlpha) < 0.004f;
            check("blend pixels, unclamped RGB and Color alpha", matches);
        }
    }
    void retain(bool first) {
        actual_.allocate(extent, extent);
        actual_.begin(0,0,0,0); setupScreenOrtho(); setBlendMode(BlendMode::Alpha);
        if (first) { retained_.setFont(font_); retained_.add(U'R', Mat4::translate(40, 40, 0), tint); }
        const auto before = sg_query_stats();
        retained_.draw();
        const auto& command = internal::currentTarget().deferredShaderDraws.back();
        if (first) { retainedVertices_ = command.bindings.vertex_buffers[0]; retainedIndices_ = command.bindings.index_buffer; }
        const auto after = sg_query_stats();
        check(first ? "dirty upload creates reusable vertex and upload buffers" : "retained frame has zero buffer uploads",
            after.total.buffers.inited - before.total.buffers.inited == (first ? 2u : 0u) &&
            after.cur_frame.num_write_buffer_transient - before.cur_frame.num_write_buffer_transient == (first ? 1u : 0u) &&
            after.cur_frame.num_write_buffer_unsealed == before.cur_frame.num_write_buffer_unsealed &&
            command.bindings.vertex_buffers[0].id == retainedVertices_.id && command.bindings.index_buffer.id == retainedIndices_.id);
        actual_.end();
        auto expected = render(reference_, [&] { font_.drawString("R", 40, 40); });
        compare("retained redraw pixels", expected, read(actual_));
    }
    void streamChecks() {
        GlyphBatch batch, other; batch.setFont(font_); other.setFont(font_);
        const Mat4 left = Mat4::translate(20, 40, 0), right = Mat4::translate(80, 40, 0);
        const auto expected = render(reference_, [&] {
            font_.drawString("A", 20, 40); font_.drawString("A", 20, 40);
            font_.drawString("B", 80, 40);
        });
        unsigned steadyCreates = 0, steadyUploads = 0;
        for (int frame = 0; frame < 8; ++frame) {
            sg_commit(); internal::drainPendingGpuDestroys();
            const auto before = sg_query_stats();
            auto pixels = render(actual_, [&] {
                batch.clear(); batch.add(U'A', left, tint); batch.draw();
                const auto once = sg_query_stats().cur_frame.num_write_buffer_transient;
                batch.draw();
                check("second draw uploads nothing", sg_query_stats().cur_frame.num_write_buffer_transient == once);
                batch.add(U'B', right, tint); batch.draw();
            });
            if (frame) {
                steadyCreates += sg_query_stats().total.buffers.inited - before.total.buffers.inited;
                steadyUploads += sg_query_stats().cur_frame.num_write_buffer_transient - before.cur_frame.num_write_buffer_transient;
            }
            // First snapshot draws twice, so the reference has three As total.
            const auto reference = render(reference_, [&] {
                font_.drawString("A", 20, 40); font_.drawString("A", 20, 40);
                font_.drawString("A", 20, 40); font_.drawString("B", 80, 40);
            });
            compare("same-frame append preserves earlier draws", reference, pixels);
        }
        std::printf("STREAM frames=8 measured_frames=7 creates=%u uploads=%u expected_uploads=14\n", steadyCreates, steadyUploads);
        check("changed same-frame snapshots reuse buffers after warmup", steadyCreates == 0 && steadyUploads == 14);
        const auto got = render(actual_, [&] {
            batch.clear(); batch.add(U'A', left, tint); batch.draw();
            const auto first = internal::currentTarget().deferredShaderDraws.back().bindings;
            batch.clear(); batch.add(U'B', right, tint); batch.draw();
            const auto second = internal::currentTarget().deferredShaderDraws.back().bindings;
            check("clear plus draw preserves first snapshot", first.vertex_buffers[0].id != second.vertex_buffers[0].id);
            other.add(U'A', left, tint); other.draw();
            check("batches share one quad index buffer", first.index_buffer.id == second.index_buffer.id &&
                second.index_buffer.id == internal::currentTarget().deferredShaderDraws.back().bindings.index_buffer.id);
        });
        compare("same-frame clear preserves earlier draws", expected, got);
        // Force both shared-index and vertex growth after a queued draw.
        const auto growthReference = render(reference_, [&] {
            for (int i = 0; i < 130; ++i) font_.drawString("A", 20, 40);
        });
        const auto growth = render(actual_, [&] {
            GlyphBatch growing; growing.setFont(font_); growing.add(U'A', left, tint); growing.draw();
            const auto oldIndex = internal::currentTarget().deferredShaderDraws.back().bindings.index_buffer;
            for (int i = 0; i < 128; ++i) growing.add(U'A', left, tint);
            growing.draw();
            check("shared index grows for larger runs", oldIndex.id != internal::currentTarget().deferredShaderDraws.back().bindings.index_buffer.id);
        });
        compare("buffer growth preserves queued draws", growthReference, growth);
    }
    void benchmark() {
        font_.setGridFit(false);
        reference_.allocate(extent, extent);
        std::vector<double> times[3];
        unsigned draws[3]{};
        for (int count : {3000, 6000}) {
            unsigned creations[3]{}, warmCreations[3]{};
            GlyphBatch bands[3];
            reference_.begin(0,0,0,0); setupScreenOrtho();
            for (auto& band : bands) band.setFont(font_);
            for (int i = 0; i < count; ++i) bands[i % 3].add(U'A', Mat4::translate(float(i%180), float(i/180%180), 0) * Mat4::scale(0.2f), tint);
            reference_.end();
            for (auto& values : times) values.clear();
            for (int iteration = 0; iteration < 13; ++iteration) for (int method = 0; method < 3; ++method) {
                const auto start = std::chrono::steady_clock::now();
                reference_.begin(0,0,0,0); setupScreenOrtho(); setColor(tint); setTextAlign(Direction::Left, Direction::Top);
                const auto before = sg_query_stats().cur_frame.num_draw;
                const auto buffersBefore = sg_query_stats().total.buffers.inited;
                if (method == 0) for (int band = 0; band < 3; ++band) for (int i = band; i < count; i += 3) {
                    pushMatrix(); translate(float(i%180), float(i/180%180)); scale(0.2f);
                    font_.drawString("A", 0, 0); popMatrix();
                } else {
                    if (method == 2) {
                        for (auto& band : bands) band.clear();
                        for (int i = 0; i < count; ++i)
                            bands[i % 3].add(U'A', Mat4::translate(float(i%180), float(i/180%180), 0) * Mat4::scale(0.2f), tint);
                    }
                    for (auto& band : bands) band.draw();
                }
                reference_.end();
                draws[method] = sg_query_stats().cur_frame.num_draw - before;
                auto pixels = read(reference_); // synchronizes the same GPU work for both methods
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
                if (iteration >= 3) {
                    times[method].push_back(ms);
                    creations[method] += sg_query_stats().total.buffers.inited - buffersBefore;
                } else warmCreations[method] += sg_query_stats().total.buffers.inited - buffersBefore;
                sg_commit(); internal::drainPendingGpuDestroys();
            }
            for (int method = 0; method < 3; ++method) {
                auto& values = times[method]; std::sort(values.begin(), values.end());
                std::printf("BENCH glyphs=%d bands=3 method=%s median_ms=%.3f p95_ms=%.3f draws=%u measured_creates=%u warmup_creates=%u\n", count, method == 0 ? "drawString" : (method == 1 ? "GlyphBatch_retained" : "GlyphBatch_rebuild"), (values[values.size()/2] + values[(values.size()-1)/2]) * 0.5, values.back(), draws[method], creations[method], warmCreations[method]);
            }
            check("three batches issue three page draws", draws[1] == 3 && draws[2] == 3);
            check("rebuilt frames create zero buffers after warmup", creations[2] == 0);
        }
    }
public:
    void runOffscreenChecks() {
        setup();
        scissorChecks(); pageOrderChecks(); layoutChecks(); batchSettingsChecks(); cameraCheck();
        streamChecks(); growthAndTransform(); targetAndBlendChecks();
        reference_.allocate(extent, extent); retain(true);
        for (int frame = 0; frame < 2; ++frame) {
            sg_commit(); internal::drainPendingGpuDestroys(); retain(false);
        }
        benchmark(); completed = true;
    }
    void setup() override {
        sg_enable_stats();
        check("test font loads", static_cast<bool>(font_.load(testFontName, 24)));
        reference_.allocate(extent, extent); actual_.allocate(extent, extent);
    }
    void draw() override {
        if (frame_ == 0) {
            scissorChecks(); pageOrderChecks(); layoutChecks(); batchSettingsChecks(); cameraCheck();
        streamChecks(); growthAndTransform(); targetAndBlendChecks();
            reference_.allocate(extent, extent);
            retain(true);
        } else {
            retain(false);
            if (frame_ == 2) { benchmark(); completed = true; exitApp(); }
        }
        ++frame_;
    }
};
#ifdef TC_GLYPH_EGL
int runEglChecks() {
    // Keep EGL private to this mode; no linked symbols can interpose other tests.
    const auto closeLibrary = [](void* handle) { dlclose(handle); };
    std::unique_ptr<void, decltype(closeLibrary)> library(
        dlopen(TC_GLYPH_EGL_LIBRARY_PATH, RTLD_NOW | RTLD_LOCAL), closeLibrary);
    if (!library) return 1;
    const auto getProcAddress = reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(dlsym(library.get(), "eglGetProcAddress"));
    const auto initialize = reinterpret_cast<PFNEGLINITIALIZEPROC>(dlsym(library.get(), "eglInitialize"));
    const auto bindApi = reinterpret_cast<PFNEGLBINDAPIPROC>(dlsym(library.get(), "eglBindAPI"));
    const auto chooseConfig = reinterpret_cast<PFNEGLCHOOSECONFIGPROC>(dlsym(library.get(), "eglChooseConfig"));
    const auto createContext = reinterpret_cast<PFNEGLCREATECONTEXTPROC>(dlsym(library.get(), "eglCreateContext"));
    const auto destroyContext = reinterpret_cast<PFNEGLDESTROYCONTEXTPROC>(dlsym(library.get(), "eglDestroyContext"));
    const auto terminate = reinterpret_cast<PFNEGLTERMINATEPROC>(dlsym(library.get(), "eglTerminate"));
    if (!getProcAddress || !initialize || !bindApi || !chooseConfig || !createContext || !destroyContext || !terminate) return 1;
    const auto getPlatform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(getProcAddress("eglGetPlatformDisplayEXT"));
    if (!getPlatform) return 1;
    const EGLDisplay display = getPlatform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint major = 0, minor = 0;
    if (!initialize(display, &major, &minor) || !bindApi(EGL_OPENGL_API)) return 1;
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig config; EGLint count = 0;
    if (!chooseConfig(display, attributes, &config, 1, &count) || count != 1) return 1;
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    const EGLContext context = createContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    // Other combined fixtures wrap the linked EGL symbol for GLES probes.
    // Resolve this fixture's context entry directly from its EGL implementation.
    const auto makeCurrent = reinterpret_cast<PFNEGLMAKECURRENTPROC>(getProcAddress("eglMakeCurrent"));
    if (!makeCurrent) return 1;
    if (context == EGL_NO_CONTEXT || !makeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return 1;
    sg_desc gpu{};
    gpu.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    gpu.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    gpu.environment.defaults.sample_count = 1;
    gpu.buffer_pool_size = 2048; gpu.image_pool_size = 2048; gpu.view_pool_size = 4096;
    gpu.pipeline_pool_size = 4096; gpu.shader_pool_size = 1024;
    gpu.logger.func = internal::sokolLog;
    sg_setup(&gpu);
    sgl_desc_t immediate{}; immediate.context_pool_size = 64;
    immediate.pipeline_pool_size = 4096; immediate.max_vertices = 65536; immediate.max_commands = 16384;
    immediate.logger.func = internal::sokolLog; sgl_setup(&immediate);
    {
        Window owner; internal::WindowRequestAccess::bindForTests(owner, 1435);
        auto& window = internal::WindowRequestAccess::context(owner);
        internal::WindowRequestAccess::binding(owner).metadata.dpi_scale = 1;
        auto* previous = internal::currentWindowCtx(); internal::currentWindowCtx() = &window;
        window.fbWidth = extent; window.fbHeight = extent; window.dpiScale = 1;
        window.swapchainTarget.colorFormat = SG_PIXELFORMAT_RGBA8;
        window.swapchainTarget.depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;
        window.swapchainTarget.sampleCount = 1;
        internal::prepareWindowTarget();
        {
            auto app = internal::makeConstructedAppAt<GlyphApp>(&window);
            internal::AppOwnerScope scope{internal::appOwner(*app)};
            app->runOffscreenChecks();
        }
        window.teardownTargets();
        sg_commit(); internal::drainPendingGpuDestroys();
        internal::currentWindowCtx() = previous;
    }
    sgl_shutdown(); sg_shutdown();
    makeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    destroyContext(display, context); terminate(display);
    check("surfaceless GPU checks complete", completed);
    return failures ? 1 : 0;
}
#endif

} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--font") == 0) testFontName = argv[i + 1];
    Font font;
    check("unloaded font has empty ink", font.getGlyphInkBox(U'A').width == 0);
    GlyphBatch empty;
    for (auto cp : {uint32_t(U'A')}) {
        empty.add(cp); empty.add(cp, Mat4::identity());
        empty.add(cp, Color(1)); empty.add(cp, Mat4::identity(), Color(1));
    }
    empty.add(U'A'); empty.add(U'A', Mat4::identity());
    empty.add(U'A', Color(1)); empty.add(U'A', Mat4::identity(), Color(1));
    empty.add("A"); empty.add("A", Color(1));
    empty.add(std::string("A")); empty.add(std::string("A"), Color(1));
    empty.clear(); empty.draw();
#ifdef TC_GLYPH_EGL
    if (argc > 1 && std::strcmp(argv[1], "--egl-check") == 0) return runEglChecks();
#endif
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings; settings.setSize(extent, extent); settings.setHighDpi(false); settings.setPipelinePoolSize(4096);
        return tcCoreTest::runDisplayTest<GlyphApp>(settings, failures, [] { check("GPU checks complete", completed); });
    }
    return failures ? 1 : 0;
}
