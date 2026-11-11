// Included inside the fixture's anonymous namespace. The synthetic glyph is
// a square with a counter: strokes, the transparent center and the empty
// surroundings are checked independently of the rendered quad's bounds.
bool midpassCompleted = false;

class MidpassCheckApp : public App {
    Font font_;
    Font burstFont_;
    Fbo once_;
    Fbo burst_;
    int frame_ = 0;
    bool painted_ = false;
    sg_image retiredImage_ = {};
    sg_view retiredView_ = {};
    EventListener overlay_;
    EventListener capture_;

    void checkGlyphs() {
        vector<unsigned char> px(256 * 192 * 4);
        check("one-shot Fbo readback", once_.readPixels(px.data()));
        // 128px font: square at (100..900, 0..800)/1000 of an em.
        // Center/Center at (128,96) puts its transparent counter around (128,83).
        auto matches = [&](int x, int y, int r, int g, int b, int a) {
            const auto* p = &px[(y * 256 + x) * 4];
            return abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 &&
                   abs(p[2] - b) <= 2 && abs(p[3] - a) <= 2;
        };
        bool strokes = true, counter = true, outside = true;
        for (int y = 40; y < 126; ++y) for (int x = 84; x < 173; ++x)
            if (x < 96 || x >= 160 || y < 49 || y >= 116)
                strokes &= matches(x, y, 255, 128, 64, 255);
        for (int y = 68; y < 98; ++y) for (int x = 112; x < 144; ++x)
            counter &= matches(x, y, 0, 0, 0, 0);
        for (int y = 0; y < 192; ++y) for (int x = 0; x < 256; ++x)
            if (x < 60 || x > 190 || y < 15 || y > 150)
                outside &= matches(x, y, 0, 0, 0, 0);
        check("glyph strokes have the expected coverage and color in this frame", strokes);
        check("glyph counter inside the quad remains transparent", counter);
        check("pixels outside the glyph remain transparent", outside);
    }

public:
    void setup() override {
        font_.setOversampling(3);
        check("midpass font loads at size 128", bool(font_.load(g_fontPath, 128)));
        burstFont_.setOversampling(3);
        check("independent burst font loads", bool(burstFont_.load(g_fontPath, 32)));
        once_.allocate(256, 192, 1, TextureFormat::RGBA8, true);
        burst_.allocate(256, 192);
        // Like an ImGui render callback: an independent textured draw after
        // application rendering, in the swapchain pass, every frame.
        overlay_ = events().onRender.listen([this] {
            setColor(1);
            once_.draw(0, 0);
            setColor(0, 1, 0);
            drawRect(8, 8, 16, 16);
        });
        capture_ = events().afterFrame.listen([this] {
            Pixels screen;
#if defined(__EMSCRIPTEN__)
            // grabScreen is unavailable on Web. Read the canvas before the
            // browser presents this tick; raw GL rows start at the bottom.
            screen.allocate(256, 192, 4);
            glReadPixels(0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, screen.getData());
            check("swapchain capture after overlay", glGetError() == GL_NO_ERROR);
            const int glyphY = 192 - 1 - 80, overlayY = 192 - 1 - 16;
#else
            check("swapchain capture after overlay", grabScreen(screen));
            const int glyphY = 80, overlayY = 16;
#endif
            if (screen.isAllocated()) {
                const auto glyph = screen.getColor(88, glyphY);
                const auto green = screen.getColor(16, overlayY);
                check("swapchain and overlay still render", glyph.r > .98f &&
                      abs(glyph.g - .5f) < .02f && glyph.b > .23f && glyph.b < .27f &&
                      green.r < .02f && green.g > .98f && green.b < .02f);
            }
            if (++frame_ == 4) { midpassCompleted = true; exitApp(); }
        });
    }

    void draw() override {
        clear(0);
        internal::ensureCurrentPass();
        if (!painted_) {
            check("swapchain pass is open before Fbo begin", isInSwapchainPass());
            once_.begin(0, 0, 0, 0);
            setColor(1, .5f, .25f);
            pushMatrix();
            scale(2);
            font_.drawString(utf8(kFirstCodepoint), -200, -200);
            popMatrix();
            const auto oldImage = font_.getAtlas(0)->getTexture();
            const auto oldView = font_.getAtlas(0)->getView();
            retiredImage_ = oldImage;
            retiredView_ = oldView;
            // First draw must have mips immediately; no second draw is needed
            // to repair this one-shot Fbo.
            font_.drawString(utf8(kFirstCodepoint), 128, 96, Direction::Center, Direction::Center);
            check("first minified draw already has a mip chain",
                  sg_query_image_desc(font_.getAtlas(0)->getTexture()).num_mipmaps > 1);
            check("queued old atlas remains valid during replacement",
                  sg_query_image_state(oldImage) == SG_RESOURCESTATE_VALID &&
                  sg_query_view_state(oldView) == SG_RESOURCESTATE_VALID);
            // A further offscreen draw exercises retirement independently of
            // the first-draw mip assertion, without repainting the glyph.
            font_.drawString(utf8(kFirstCodepoint), -200, -200);
            once_.end();
            painted_ = true;
        }
        if (frame_ == 1) {
            check("old atlas retires without another draw of the one-shot font",
                  sg_query_image_state(retiredImage_) == SG_RESOURCESTATE_INVALID &&
                  sg_query_view_state(retiredView_) == SG_RESOURCESTATE_INVALID);
        }
        checkGlyphs();
        // New glyphs and page growth after the one-shot draw must preserve it.
        burst_.begin(0, 0, 0, 0);
        setColor(1, .5f, .25f);
        if (frame_ == 2) {
            burstFont_.clearAtlas();
            vector<sg_image> held;
            for (;;) {
                const sg_image image = sg_alloc_image();
                if (!image.id) break;
                held.push_back(image);
            }
            burstFont_.drawString(utf8(kFirstCodepoint), 128, 96, Direction::Center, Direction::Center);
            check("failed atlas allocation is not published as valid", !burstFont_.getAtlas(0)->isTextureValid());
            for (auto image : held) sg_dealloc_image(image);
            burstFont_.drawString(utf8(kFirstCodepoint), 128, 96, Direction::Center, Direction::Center);
            check("atlas retries successfully in the same pass", burstFont_.getAtlas(0)->isTextureValid() &&
                  sg_query_image_desc(burstFont_.getAtlas(0)->getTexture()).num_mipmaps > 1);
        }
        for (int i = 0; i < (frame_ == 0 ? 500 : 1); ++i)
            burstFont_.drawString(utf8(kFirstCodepoint + 1 + (i % 16)), 128, 96,
                             Direction::Center, Direction::Center);
        burst_.end();
    }
};
