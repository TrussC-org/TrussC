// #1334: exact RGBA8 readback for blend passes that test without writing depth.
// Integer colors keep Add/Subtract exact; Screen/Multiply round only once.
struct BlendDepthFixture {
    const char* name;
    BlendMode mode;
    Color background, colorA, colorB;
    std::array<unsigned char, 4> expected;
};

const BlendDepthFixture blendDepthFixtures[] = {
    {"add", BlendMode::Add, Color::fromBytes(16, 16, 16),
     Color::fromBytes(32, 64, 16), Color::fromBytes(64, 16, 32), {112, 96, 64, 255}},
    {"screen", BlendMode::Screen, Color(0, 0, 0),
     Color(0.2f, 0.4f, 0.6f), Color(0.4f, 0.6f, 0.2f), {133, 194, 173, 255}},
    {"multiply", BlendMode::Multiply, Color(1, 1, 1),
     Color(0.4f, 0.6f, 0.8f), Color(0.8f, 0.4f, 0.6f), {82, 61, 122, 255}},
    {"subtract", BlendMode::Subtract, Color::fromBytes(224, 208, 192),
     Color::fromBytes(32, 64, 16), Color::fromBytes(64, 16, 32), {128, 128, 144, 255}},
};

void drawBlendDepthQuad(float z, Color color) {
    setColor(color);
    pushMatrix();
    translate(0, 0, z);
    drawRect(-20, -20, 40, 40);
    popMatrix();
}

Pixels renderBlendDepthPair(const BlendDepthFixture& fixture, bool reverse, bool easycam,
                           bool opaqueFirst = false) {
    Fbo fbo;
    fbo.allocate(64, 64, 1);
    fbo.begin(fixture.background.r, fixture.background.g, fixture.background.b, fixture.background.a);
    setupScreenOrtho();
    resetMatrix();
    EasyCam camera;
    camera.setDistance(100);
    camera.enableOrtho();
    if (easycam) camera.begin();
    else translate(32, 32);
    enableDepthTest();
    const auto drawA = [&] {
        setBlendMode(opaqueFirst ? BlendMode::Alpha : fixture.mode);
        drawBlendDepthQuad(10, fixture.colorA);
    };
    const auto drawB = [&] {
        setBlendMode(fixture.mode);
        drawBlendDepthQuad(-10, fixture.colorB);
    };
    if (reverse) { drawB(); drawA(); }
    else { drawA(); drawB(); }
    if (easycam) camera.end();
    fbo.end();
    return readTarget(fbo);
}

void recordBlendDepthPixel(const Pixels& pixels, const std::string& name,
                          const std::array<unsigned char, 4>& expected) {
    const auto* rgba = pixels.getData() + (32 * 64 + 32) * 4;
    std::printf("BLEND DEPTH %s center=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n", name.c_str(),
                rgba[0], rgba[1], rgba[2], rgba[3], expected[0], expected[1], expected[2], expected[3]);
    check((name + " exact center RGBA").c_str(), std::memcmp(rgba, expected.data(), 4) == 0);
    // Optional evidence output; ordinary --gpu-check runs only the assertions.
    if (const char* directory = std::getenv("TC_BLEND_DEPTH_CAPTURE_DIR")) {
        check("blend depth PNG saved", pixels.save(fs::path(directory) / (name + ".png")).ok());
    }
}

void runBlendDepthChecks() {
    pushStyle();
    for (bool easycam : {false, true}) {
        const std::string scope = easycam ? "easycam-" : "ortho-";
        for (const auto& fixture : blendDepthFixtures) {
            const auto forward = renderBlendDepthPair(fixture, false, easycam);
            const auto reverse = renderBlendDepthPair(fixture, true, easycam);
            recordBlendDepthPixel(forward, scope + fixture.name + "-near-first", fixture.expected);
            recordBlendDepthPixel(reverse, scope + fixture.name + "-far-first", fixture.expected);
            check((scope + fixture.name + " both orders have identical RGBA pixels").c_str(),
                  std::memcmp(forward.getData(), reverse.getData(), forward.getTotalBytes()) == 0);
        }
        const BlendDepthFixture opaque = {"opaque-hides-add", BlendMode::Add, Color(0, 0, 0),
                                          Color(0.2f, 0.4f, 0.6f), Color(0.4f, 0.6f, 0.2f), {51, 102, 153, 255}};
        recordBlendDepthPixel(renderBlendDepthPair(opaque, false, easycam, true),
                              scope + opaque.name, opaque.expected);
        for (BlendMode mode : {BlendMode::Alpha, BlendMode::Disabled}) {
            auto control = opaque;
            control.mode = mode;
            control.name = mode == BlendMode::Alpha ? "alpha" : "disabled";
            recordBlendDepthPixel(renderBlendDepthPair(control, false, easycam),
                                  scope + control.name + "-near-first", control.expected);
            recordBlendDepthPixel(renderBlendDepthPair(control, true, easycam),
                                  scope + control.name + "-far-first", control.expected);
        }
    }
    popStyle();
}
