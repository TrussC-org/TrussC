#pragma once
#include "../../../../core/tests/common/tcDisplayTest.h"
#include <array>

namespace {
int curveFailures = 0;
void curveCheck(const char* label, bool ok) {
    std::printf("%-72s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++curveFailures;
}
std::filesystem::path fixturePath(const char* name) {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "bin/data" / name;
}
bool approxEq(const tc::Color& a, const tc::Color& b, float tolerance = 0.000002f) {
    return std::abs(a.r-b.r) <= tolerance && std::abs(a.g-b.g) <= tolerance &&
           std::abs(a.b-b.b) <= tolerance && std::abs(a.a-b.a) <= tolerance;
}
const char* const curveFixtures[] = {"identity-1d.cube", "gamma22-1d.cube",
                                   "shaper-identity.cube", "shaper-brg.cube"};
float gammaReference(float value) {
    // Expected outputs at inputs 0, 1/8, ..., 1: alternating pow(x, 2.2)
    // table points and arithmetic midpoints of adjacent table values.
    const float expected[] = {0, 0.02368307135f, 0.0473661427f, 0.13250189175f,
                             0.2176376408f, 0.37434343295f, 0.5310492251f, 0.76552461255f, 1};
    return expected[int(std::round(std::clamp(value, 0.0f, 1.0f) * 8))];
}
tc::Color referenceColor(int fixture, tc::Color c) {
    c.r = std::clamp(c.r, 0.0f, 1.0f);
    c.g = std::clamp(c.g, 0.0f, 1.0f);
    c.b = std::clamp(c.b, 0.0f, 1.0f);
    if (fixture == 1) return {gammaReference(c.r), gammaReference(c.g), gammaReference(c.b), c.a};
    if (fixture >= 2) {
        const auto squareCurve = [](float v) { return v <= 0.5f ? v * 0.5f : v * 1.5f - 0.5f; };
        c = {squareCurve(c.r), c.g, squareCurve(1-c.b), c.a};
        if (fixture == 3) c = {c.b,c.r,c.g,c.a};
    }
    return c;
}
void parserCurveChecks() {
    using namespace tcx::lut::detail;
    CubeData parsed;
    std::string error;
    for (const char* name : curveFixtures) {
        std::ifstream input(fixturePath(name));
        curveCheck(name, parseCube(input, parsed, error) && parsed.curveSize >= 3 && !parsed.title.empty());
    }
    const auto previousCurve = parsed.curve;
    const auto previousRgb = parsed.rgb;
    for (const char* source : {
        "LUT_1D_SIZE 0\n", "LUT_1D_SIZE 1\n", "LUT_1D_SIZE -2\n", "LUT_1D_SIZE 2.5\n",
        "LUT_1D_SIZE 99999999999999999999999\n", "LUT_1D_SIZE 2 extra\n",
        "LUT_1D_SIZE 2\nLUT_1D_SIZE 2\n", "LUT_1D_SIZE 2\n0 0 0\n",
        "LUT_1D_SIZE 2\n0 0 0\n1 1 1\n2 2 2\n", "LUT_1D_SIZE 2\n0 0 nan\n1 1 1\n",
        "LUT_1D_SIZE 2\n0 0 0\nLUT_3D_SIZE 2\n", "LUT_1D_SIZE 2\nLUT_3D_SIZE 2\n0 0 0\n1 1 1\n",
        "LUT_1D_SIZE 2\nLUT_1D_INPUT_RANGE -1 1\n", "LUT_1D_SIZE 2\nLUT_3D_INPUT_RANGE 0 2\n",
        "LUT_1D_SIZE 2\nDOMAIN_MIN 0 -1 0\n", "LUT_1D_SIZE 2\nDOMAIN_MAX 1 1 2\n"}) {
        std::istringstream input(source);
        curveCheck("malformed curve rejects without replacing parsed tables",
                   !parseCube(input, parsed, error) && !error.empty() &&
                   parsed.curve == previousCurve && parsed.rgb == previousRgb);
    }
    // No allocation based on an untrusted declaration alone.
    std::istringstream huge("LUT_1D_SIZE 2147483647\n");
    curveCheck("large 1D declaration without rows rejects as incomplete",
               !parseCube(huge, parsed, error) && error == "incomplete LUT table");
    std::istringstream reversed("LUT_3D_SIZE 2\nLUT_1D_SIZE 2\n0 0 0\n1 1 1\n"
                               "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    curveCheck("either size declaration order uses shaper rows first",
               parseCube(reversed, parsed, error) && parsed.curve.size() == 6 && parsed.rgb.size() == 24);
    std::istringstream title("TITLE \"curve #1\" # comment\r\nLUT_1D_SIZE 2\r\n0 0 0\r\n1 1 1\r\n");
    curveCheck("quoted TITLE preserves hashes and ignores trailing comments with CRLF",
               parseCube(title, parsed, error) && parsed.title == "curve #1");
    std::istringstream backslashes("TITLE \"C:\\LUTs\\\"\nLUT_1D_SIZE 2\n0 0 0\n1 1 1\n");
    curveCheck("quoted TITLE keeps backslashes literally",
               parseCube(backslashes, parsed, error) && parsed.title == "C:\\LUTs\\");
}
class CurveApp : public tc::App {
public:
    bool completed = false;
    void draw() override {
        using namespace tc;
        if (completed) return;
        tcx::lut::LutShader shader;
        curveCheck("built-in LUT shader loads", shader.load());
        const std::array<Color, 11> samples = {{
            {0,0,0}, {1,1,1}, {0.25f,0.5f,0.75f}, {0.125f,0.375f,0.625f},
            {0.875f,0.125f,0.375f}, {0.5f,0.25f,0}, {0.75f,1,0.5f},
            {-0.25f,1.25f,0.5f}, {0,0.5f,1,0.25f}, {0.375f,0.625f,0.875f}, {1,0,0.25f}
        }};
        for (int fixture = 0; fixture < 4; ++fixture) {
            tcx::lut::Lut3D lut;
            curveCheck(curveFixtures[fixture], lut.load(fixturePath(curveFixtures[fixture])) && lut.isAllocated());
            if (!lut.isAllocated()) continue;
            curveCheck("loaded size reports 3D side, or zero for 1D only", lut.getSize() == (fixture < 2 ? 0 : 2));
            for (const auto& sample : samples)
                curveCheck("CPU exact points, midpoints, per-channel input and clamping",
                           approxEq(lut.apply(sample), referenceColor(fixture, sample)));
            // 1D remains linear even when nearest filtering is selected for 3D.
            lut.setFilter(TextureFilter::Nearest);
            if (fixture < 2)
                curveCheck("1D interpolation is independent of 3D filter", approxEq(lut.apply(samples[3]), referenceColor(fixture,samples[3])));
            lut.setFilter(TextureFilter::Linear);
            tcx::lut::Lut3D moved;
            moved = std::move(lut);
            curveCheck("move transfers both tables", moved.isAllocated() && !lut.isAllocated() &&
                       approxEq(moved.apply(samples[3]), referenceColor(fixture,samples[3])));
            lut = std::move(moved);
            shader.setLut(lut);
            constexpr int width = 9;
            unsigned char bytes[width * 4];
            for (int i = 0; i < width; ++i) {
                bytes[i*4] = static_cast<unsigned char>(i == 8 ? 255 : i*32);
                bytes[i*4+1] = static_cast<unsigned char>(255-bytes[i*4]);
                bytes[i*4+2] = static_cast<unsigned char>((i*53)%256);
                bytes[i*4+3] = 255;
            }
            Texture source;
            source.allocate(width,1,TextureFormat::RGBA8,TextureUsage::Dynamic);
            source.loadData(bytes,width,1,4);
            source.setFilter(TextureFilter::Nearest);
            shader.setTexture(source);
            Fbo fbo; fbo.allocate(width,1);
            for (float blendAmount : {1.0f,0.5f,0.0f}) {
                shader.setBlend(blendAmount);
                fbo.begin(0,0,0,1); setupScreenOrtho(); shader.draw(0,0,width,1); fbo.end();
                unsigned char pixels[width*4] = {};
                curveCheck("LUT Fbo readback succeeds", fbo.readPixels(pixels));
                for (int i = 0; i < width; ++i) {
                    const Color input(bytes[i*4]/255.0f,bytes[i*4+1]/255.0f,bytes[i*4+2]/255.0f);
                    const Color expected = input*(1-blendAmount) + lut.apply(input)*blendAmount;
                    const Color actual(pixels[i*4]/255.0f,pixels[i*4+1]/255.0f,pixels[i*4+2]/255.0f,pixels[i*4+3]/255.0f);
                    curveCheck("GPU curve/composition/blend agrees with CPU", approxEq(actual,expected,2.0f/255.0f));
                }
            }
            const auto oldColor = lut.apply(samples[3]);
            curveCheck("failed reload preserves complete LUT and title",
                       !lut.load(fixturePath("missing.cube")) && lut.isAllocated() &&
                       !lut.getTitle().empty() && approxEq(lut.apply(samples[3]),oldColor));
            lut.clear();
            curveCheck("clear resets tables and CPU application", !lut.isAllocated() && lut.getSize() == 0 &&
                       approxEq(lut.apply(samples[3]),samples[3]));
        }
        auto identity = tcx::lut::createIdentity(2);
        curveCheck("existing generated 3D LUT still applies", identity.isAllocated() && approxEq(identity.apply(samples[3]),samples[3]));
        Texture source;
        const unsigned char bytes[] = {64,128,192,255};
        source.allocate(1,1,TextureFormat::RGBA8,TextureUsage::Dynamic);
        source.loadData(bytes,1,1,4);
        shader.setTexture(source); shader.setLut(identity); shader.setBlend(1);
        Fbo fbo; fbo.allocate(1,1);
        fbo.begin(0,0,0,1); setupScreenOrtho(); shader.draw(0,0,1,1); fbo.end();
        unsigned char pixel[4] = {};
        curveCheck("3D-only GPU path keeps valid identity curve binding", fbo.readPixels(pixel) &&
                   std::abs(int(pixel[0])-64) <= 1 && std::abs(int(pixel[1])-128) <= 1 &&
                   std::abs(int(pixel[2])-192) <= 1 && pixel[3] == 255);
        completed = true;
        exitApp(curveFailures ? 1 : 0);
    }
    void exit() override { curveCheck("GPU checks completed",completed); }
};
int runCurveGpuChecks() {
    tc::WindowSettings settings; settings.setSize(64,64); settings.setSwapInterval(0);
    return tcCoreTest::runDisplayTest<CurveApp>(settings,curveFailures);
}
}
