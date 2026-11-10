#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <array>
#include <cstdio>
#include <cstring>

using namespace tc;
namespace {
int failures = 0;
bool completed = false;
bool recordOnly = false;
fs::path output;
const std::array<Color, 3> colors = {Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)};
const Color uniform(0.25f, 0.75f, 0.5f);
void check(const char* label, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
bool sameColor(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
void cpuTests();
void stroke(bool closed, bool multicolor) {
    beginStroke();
    setColor(multicolor ? colors[0] : uniform); vertex(40, 120);
    setColor(multicolor ? colors[1] : uniform); vertex(150, 40);
    setColor(multicolor ? colors[2] : uniform); vertex(260, 120);
    endStroke(closed);
}
void expect(const Pixels& pixels, int x, int y, Color expected, const char* label, int tolerance = 3) {
    const Color actual = pixels.getColor(x, y);
    std::printf("PIXEL %s (%d,%d) = (%.0f,%.0f,%.0f,%.0f)\n", label, x, y,
        actual.r * 255, actual.g * 255, actual.b * 255, actual.a * 255);
    if (recordOnly) return;
    auto matches = [tolerance](float a, float b) {
        return std::abs(std::lround(a * 255) - std::lround(b * 255)) <= tolerance;
    };
    check(label, matches(actual.r, expected.r) && matches(actual.g, expected.g) &&
                 matches(actual.b, expected.b) && matches(actual.a, expected.a));
}
// Miter cross-sections can skew the gradient at a spine midpoint. Verify a
// mixture of the correct endpoint colors rather than assuming exactly 50%.
void expectBlend(const Pixels& pixels, int x, int y, bool closing) {
    const Color c = pixels.getColor(x, y);
    std::printf("PIXEL blend (%d,%d) = (%.0f,%.0f,%.0f,%.0f)\n", x, y,
        c.r * 255, c.g * 255, c.b * 255, c.a * 255);
    if (recordOnly) return;
    const float second = closing ? c.b : c.g;
    const float unused = closing ? c.g : c.b;
    check(closing ? "closing segment blends last to first" : "segment blends endpoint colors",
        c.r > 0.25f && c.r < 0.75f && second > 0.25f && second < 0.75f &&
        unused < 0.01f && std::abs(c.r + second - 1) < 0.01f && c.a > 0.99f);
}
class StrokeApp : public App {
    Fbo target_;
    void capture(const char* name, bool closed, bool multicolor) {
        target_.begin(0, 0, 0);
        setupScreenOrtho();
        setStrokeWeight(20);
        for (int cap = 0; cap < 3; ++cap) {
            for (int join = 0; join < 3; ++join) {
                auto matrix = scopedMatrix();
                translate(join * 320, cap * 180);
                setStrokeCap(static_cast<StrokeCap>(cap));
                setStrokeJoin(static_cast<StrokeJoin>(join));
                stroke(closed, multicolor);
            }
        }
        target_.end();
        Pixels pixels; pixels.allocate(960, 540, 4);
        check("stroke FBO readback", target_.readPixels(pixels.getData()));
        if (!output.empty()) check("stroke PNG saved", bool(pixels.save(output / name)));
        for (int cap = 0; cap < 3; ++cap) {
            for (int join = 0; join < 3; ++join) {
                const int x = join * 320, y = cap * 180;
                const Color first = multicolor ? colors[0] : uniform;
                const Color middle = multicolor ? colors[1] : uniform;
                const Color last = multicolor ? colors[2] : uniform;
                expect(pixels, x + 40, y + 120, first, "first vertex", 4);
                expect(pixels, x + 150, y + 34, middle, "joint", 4);
                expect(pixels, x + 259, y + 119, last, "last vertex", 4);
                if (multicolor) expectBlend(pixels, x + 95, y + 80, false);
                else expect(pixels, x + 95, y + 80, uniform, "uniform segment");
                if (closed) {
                    if (multicolor) expectBlend(pixels, x + 150, y + 120, true);
                    else expect(pixels, x + 150, y + 120, uniform, "uniform closing segment");
                } else if (cap != 0) {
                    expect(pixels, x + 34, y + 124, first, "start cap");
                    expect(pixels, x + 266, y + 124, last, "end cap");
                }
            }
        }
    }
public:
    void setup() override { target_.allocate(960, 540); }
    void draw() override {
        capture("multicolor.png", false, true);
        capture("closed.png", true, true);
        capture("single-color.png", false, false);
        // APIs sharing the accumulator keep using the current uniform color.
        target_.begin(0, 0, 0); setupScreenOrtho();
        setColor(uniform); setStrokeWeight(20); setStrokeCap(StrokeCap::Round);
        drawStroke(40, 40, 260, 40);
        Path path; path.addVertex(40, 100); path.addVertex(150, 160); path.addVertex(260, 100);
        path.drawStroke();
        target_.end();
        Pixels pixels; pixels.allocate(960, 540, 4);
        check("shared APIs readback", target_.readPixels(pixels.getData()));
        expect(pixels, 150, 40, uniform, "drawStroke uniform color");
        expect(pixels, 150, 160, uniform, "Path::drawStroke uniform color");
        if (!output.empty()) check("shared APIs PNG saved", bool(pixels.save(output / "single-color-apis.png")));
        completed = true;
        exitApp();
    }
};
void cpuTests() {
    beginStroke();
    for (int i = 0; i < 3; ++i) {
        setColor(colors[i]); setStrokeWeight(2 + i);
        vertex(40 + 110 * i, i == 1 ? 40 : 120);
    }
    const auto& vertices = internal::currentWindowContext().strokeVertices;
    bool captured = vertices.size() == 3;
    for (int i = 0; captured && i < 3; ++i)
        captured &= sameColor(vertices[i].color, colors[i]) && vertices[i].width == 2 + i;
    check("accumulator captures vertex colors and widths", captured);
    beginStroke();
    check("new stroke clears captured colors", internal::currentWindowContext().strokeVertices.empty());
    internal::currentWindowContext().strokeStarted = false;
    beginStroke();
    setStrokeCap(StrokeCap::Round); setStrokeJoin(StrokeJoin::Miter); vertex(0, 0);
    const internal::CurveAppendCheckpoint checkpoint;
    setStrokeCap(StrokeCap::Butt); setStrokeJoin(StrokeJoin::Bevel); vertex(40, 0);
    checkpoint.restore();
    check("checkpoint restores vertex styles together", vertices.size() == 1 &&
        vertices.front().cap == StrokeCap::Round && vertices.front().join == StrokeJoin::Miter);
    setStrokeJoin(StrokeJoin::Round); vertex(100, 0);
    setStrokeCap(StrokeCap::Square); setStrokeJoin(StrokeJoin::Bevel); vertex(100, 100);
    setStrokeCap(StrokeCap::Butt); setStrokeJoin(StrokeJoin::Miter);
    check("start and end caps captured before later changes", vertices.front().cap == StrokeCap::Round &&
        vertices.back().cap == StrokeCap::Square);
    check("each corner captures its own join", vertices[1].join == StrokeJoin::Round &&
        vertices[2].join == StrokeJoin::Bevel);
    beginStroke();
    const internal::CurveAppendCheckpoint emptyCheckpoint;
    vertex(1, 1); emptyCheckpoint.restore();
    setStrokeCap(StrokeCap::Square); setStrokeJoin(StrokeJoin::Round); vertex(2, 2);
    check("empty checkpoint leaves no stale first style", vertices.size() == 1 &&
        vertices.front().cap == StrokeCap::Square && vertices.front().join == StrokeJoin::Round);
    beginStroke(); internal::currentWindowContext().strokeStarted = false;
    Path path; path.addVertex(0, 0); path.addVertex(100, 0); path.addVertex(100, 100);
    for (auto join : {StrokeMesh::JOIN_MITER, StrokeMesh::JOIN_ROUND, StrokeMesh::JOIN_BEVEL}) {
        for (auto cap : {StrokeMesh::CAP_BUTT, StrokeMesh::CAP_ROUND, StrokeMesh::CAP_SQUARE}) {
            StrokeMesh stroke(path);
            stroke.setColor(uniform).setWidth(4).setJoinType(join).setCapType(cap); stroke.update();
            const Mesh& mesh = stroke.getMesh();
            const auto& actual = mesh.getColors();
            check("direct StrokeMesh stays uniform for every join/cap", !actual.empty() &&
                actual.size() == mesh.getVertices().size() &&
                std::all_of(actual.begin(), actual.end(), [](const Color& c) { return sameColor(c, uniform); }));
        }
    }
    path.addVertex(200, 100);
    StrokeMesh reset(path), expected(path);
    reset.setWidth(12).setCapType(StrokeMesh::CAP_ROUND).setJoinType(StrokeMesh::JOIN_ROUND);
    reset.update();
    reset.setCapType(StrokeMesh::CAP_SQUARE).setJoinType(StrokeMesh::JOIN_BEVEL); reset.update();
    expected.setWidth(12).setCapType(StrokeMesh::CAP_SQUARE).setJoinType(StrokeMesh::JOIN_BEVEL);
    expected.update();
    check("direct setters replace the whole stroke after update",
        reset.getMesh().getVertices() == expected.getMesh().getVertices());
    const auto& points = reset.getMesh().getVertices();
    check("whole-stroke join setter changes both corners", points.size() == 42);
    check("whole-stroke cap setter extends both ends", std::any_of(points.begin(), points.end(),
        [](const Vec3& p) { return p.x == -6; }) && std::any_of(points.begin(), points.end(),
        [](const Vec3& p) { return p.x == 206; }));
}

}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 1) { cpuTests(); return failures ? 1 : 0; }
    if (std::strcmp(argv[1], "--gpu") != 0) return 2;
    if (argc >= 3) { output = argv[2]; fs::create_directories(output); }
    if (argc >= 4) recordOnly = std::strcmp(argv[3], "--record-only") == 0;
    WindowSettings settings; settings.width = 960; settings.height = 540;
    return tcCoreTest::runDisplayTest<StrokeApp>(settings, failures, [] {
        check("stroke display checks completed", completed);
    });
}
