// Headless file-loading and coordinate regression tests, discovered by --addon-tests-only.
#include <tcxQuadWarp.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>

namespace {
using namespace tc;
using tcx::quadwarp::QuadWarp;

int passed = 0;
int failed = 0;

void check(const std::string& name, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name.c_str());
    ok ? ++passed : ++failed;
}

bool sameCorners(const QuadWarp& a, const QuadWarp& b) {
    for (int i = 0; i < 4; ++i) {
        if (a.srcPoints[i] != b.srcPoints[i] || a.dstPoints[i] != b.dstPoints[i]) return false;
    }
    return true;
}

struct TestFiles {
    fs::path dir = fs::current_path() / ("quadwarp-load-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    TestFiles() { fs::create_directory(dir); }
    ~TestFiles() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

// Independent perspective transform with translation, scale and shear.
Vec2 perspective(const Vec2& point) {
    const float w = 1 + point.x / 1024 + point.y / 512;
    return {(100 + 2 * point.x + 0.25f * point.y) / w,
            (80 + 0.125f * point.x + 2 * point.y) / w};
}

void coordinateTests() {
    QuadWarp warp;
    warp.setSourceRect(Rect(-20, 30.5f, 640, 480));
    for (int i = 0; i < 4; ++i) warp.dstPoints[i] = perspective(warp.srcPoints[i]);
    const QuadWarp& mapping = warp; // Both helpers must work on a const warp.
    constexpr float epsilon = 0.002f;
    for (int i = 0; i < 4; ++i) {
        Vec2 screen, source;
        check("source corner maps to corresponding destination corner",
              mapping.sourceToScreen(warp.srcPoints[i], screen) && screen.distance(warp.dstPoints[i]) < epsilon);
        check("destination corner maps back to source rect corner",
              mapping.screenToSource(warp.dstPoints[i], source) && source.distance(warp.srcPoints[i]) < epsilon);
    }
    for (Vec2 point : {Vec2(0, 50), Vec2(320, 240), Vec2(600, 500), Vec2(48, 67),
                       Vec2(-40, 100), Vec2(700, 550)}) {
        Vec2 screen, source;
        check("perspective forward warp matches independent transform, including outside points",
              mapping.sourceToScreen(point, screen) && screen.distance(perspective(point)) < epsilon);
        check("perspective source-screen-source round trip without clamping",
              mapping.screenToSource(screen, source) && source.distance(point) < epsilon);
        Vec2 aliased = point;
        check("input and output may alias",
              mapping.sourceToScreen(aliased, aliased) && mapping.screenToSource(aliased, aliased) &&
              aliased.distance(point) < epsilon);
    }

    // The reviewer's 4K/one-pixel trapezoid, plus source/destination scale and
    // offset variants. Expected points come from its analytic unit-frame map.
    for (float sourceSize : {1.0f, 100.0f, 320.0f, 4096.0f}) {
        for (float targetSize : {1.0f, 100.0f, 4096.0f}) {
            for (Vec2 offset : {Vec2(0, 0), Vec2(3000, 2000)}) {
                QuadWarp small;
                small.setSourceRect(Rect(0, 0, sourceSize, sourceSize));
                small.setTargetRect(Rect(offset.x, offset.y, targetSize, targetSize));
                small.dstPoints[2].x = offset.x + 0.75f * targetSize;
                const std::string name = "scaled trapezoid src=" + std::to_string(sourceSize) +
                    " dst=" + std::to_string(targetSize) + " offset=" + std::to_string(offset.x);
                // At x=3000, float screen coordinates have a 1/4096-pixel
                // ULP. Allow that quantization after magnifying back into
                // source space (up to 4096 source units per screen pixel).
                const float sourceTolerance = epsilon + 0.0005f * sourceSize / targetSize;
                const float screenTolerance = epsilon + 2e-6f * targetSize;
                for (Vec2 unit : {Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), Vec2(0, 1),
                                  Vec2(0.5f, 0.5f), Vec2(0.25f, 0.75f)}) {
                    const Vec2 point = unit * sourceSize;
                    const double w = 1 + double(unit.y) / 3;
                    const Vec2 expected(offset.x + targetSize * double(unit.x) / w,
                                        offset.y + targetSize * (4 * double(unit.y) / 3) / w);
                    Vec2 screen, source;
                    check(name + ": forward matches analytic transform",
                          small.sourceToScreen(point, screen) && screen.distance(expected) < screenTolerance);
                    check(name + ": independent screen point maps to source",
                          small.screenToSource(expected, source) && source.distance(point) < sourceTolerance);
                    check(name + ": source-screen-source round trip",
                          small.screenToSource(screen, source) && source.distance(point) < sourceTolerance);
                    Vec2 roundTrip;
                    check(name + ": screen-source-screen round trip",
                          small.sourceToScreen(source, roundTrip) && roundTrip.distance(screen) < screenTolerance);
                }
            }
        }
    }

    auto rejectBoth = [&](const std::string& name, const QuadWarp& candidate, const Vec2& point) {
        const Vec2 sentinel(123.25f, -456.5f);
        Vec2 output = sentinel;
        check(name + ": forward returns false", !candidate.sourceToScreen(point, output));
        check(name + ": forward leaves output unchanged", output == sentinel);
        output = sentinel;
        check(name + ": inverse returns false", !candidate.screenToSource(point, output));
        check(name + ": inverse leaves output unchanged", output == sentinel);
    };
    const Vec2 invalidQuads[][4] = {
        {{0, 0}, {10, 0}, {20, 0}, {30, 0}}, // Collinear.
        {{10, 10}, {10, 10}, {10, 10}, {10, 10}}, // Collapsed.
        {{0, 0}, {100, 100}, {100, 0}, {0, 100}}, // Self-intersecting.
        {{0, 0}, {100, 0}, {25, 25}, {0, 100}}, // Concave.
        {{0, 0}, {100, 0}, {100, 0}, {0, 100}}, // One collapsed edge.
    };
    int fixture = 0;
    for (const auto& points : invalidQuads) {
        for (bool sourceSide : {false, true}) {
            QuadWarp invalid;
            for (int i = 0; i < 4; ++i) {
                (sourceSide ? invalid.srcPoints[i] : invalid.dstPoints[i]) = points[i];
            }
            rejectBoth("invalid quad " + std::to_string(fixture) + (sourceSide ? " source" : " destination"),
                       invalid, {10, 20});
        }
        // Matching invalid quads can produce an identity homography. These
        // cases must fail specifically because the geometry is not convex.
        QuadWarp matchingInvalid;
        for (int i = 0; i < 4; ++i)
            matchingInvalid.srcPoints[i] = matchingInvalid.dstPoints[i] = points[i];
        rejectBoth("matching invalid quads " + std::to_string(fixture), matchingInvalid, {10, 20});
        ++fixture;
    }
    QuadWarp fallback;
    fallback.setSourceRect(Rect(0, 0, 1e-12f, 1e-12f));
    rejectBoth("homography solver identity fallback", fallback, {0, 0});
    fallback.setTargetRect(Rect(0, 0, 2e-12f, 2e-12f));
    rejectBoth("identity fallback with sub-epsilon corner mismatch", fallback, {0, 0});
    fallback.setTargetRect(Rect(0, 0, 1e-12f, 1e-12f));
    Vec2 identityPoint;
    check("identity remains valid when source and destination corners coincide",
          fallback.sourceToScreen({0, 0}, identityPoint) && identityPoint == Vec2(0, 0) &&
          fallback.screenToSource({0, 0}, identityPoint) && identityPoint == Vec2(0, 0));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (Vec2 point : {Vec2(nan, 0), Vec2(0, nan), Vec2(inf, 0), Vec2(0, -inf)}) {
        rejectBoth("non-finite input", mapping, point);
        for (bool sourceSide : {false, true}) {
            QuadWarp invalid;
            (sourceSide ? invalid.srcPoints[3] : invalid.dstPoints[3]) = point;
            rejectBoth("non-finite quad corner", invalid, {10, 20});
        }
    }

    // Exact binary coefficients: forward w = 1 + x/128, inverse w = 1 - x/128.
    QuadWarp horizon;
    horizon.setSourceRect(Rect(0, 0, 128, 128));
    horizon.dstPoints[0] = {0, 0};
    horizon.dstPoints[1] = {64, 0};
    horizon.dstPoints[2] = {64, 64};
    horizon.dstPoints[3] = {0, 128};
    Vec2 screen, source;
    check("horizon fixture maps normally on the positive-w side",
          horizon.sourceToScreen({128, 64}, screen) && screen.distance({64, 32}) < epsilon &&
          horizon.screenToSource(screen, source) && source.distance({128, 64}) < epsilon);
    for (float x : {128.0f, 256.0f}) {
        const Vec2 sentinel(12, 34);
        Vec2 output = sentinel;
        check("forward rejects w <= 0 and preserves output",
              !horizon.sourceToScreen({-x, 20}, output) && output == sentinel);
        check("inverse rejects w <= 0 and preserves output",
              !horizon.screenToSource({x, 20}, output) && output == sentinel);
    }

    // A finite homogeneous result can still overflow during the divide.
    QuadWarp overflow;
    overflow.setSourceRect(Rect(0, 0, 128, 128));
    overflow.dstPoints[0] = {0, 0};
    overflow.dstPoints[1] = {128, 0};
    overflow.dstPoints[2] = {64, 64};
    overflow.dstPoints[3] = {0, 64};
    const float max = std::numeric_limits<float>::max();
    Vec2 unchanged(12, 34);
    check("forward rejects non-finite projective divide and preserves output",
          !overflow.sourceToScreen({max, -128 + 1.0f / 65536}, unchanged) && unchanged == Vec2(12, 34));
    check("inverse rejects non-finite projective divide and preserves output",
          !overflow.screenToSource({max, 128 - 1.0f / 65536}, unchanged) && unchanged == Vec2(12, 34));

    QuadWarp mirrored;
    mirrored.setTargetRect(Rect(100, 0, -100, 100));
    Vec2 output;
    check("convex mirrored quad remains valid",
          mirrored.sourceToScreen({25, 50}, output) && output.distance({75, 50}) < epsilon);
}
} // namespace

int main() {
    coordinateTests();
    TestFiles files;
    QuadWarp saved;
    saved.setSourceRect(Rect(-20, 30.5f, 640, 480));
    saved.setTargetRect(Rect(120, -100.25f, 800, 600));
    saved.dstPoints[2].set(850.5f, 550.25f);
    const std::string savedPath = (files.dir / "saved.json").string();
    saved.save(savedPath);
    const Json valid = loadJson(savedPath);
    check("save writes both arrays", valid.is_object() && valid.contains("quadwarp"));
    if (failed) return 1;

    QuadWarp loaded;
    check("saved file loads successfully", loaded.load(savedPath));
    check("save/load preserves every corner", sameCorners(saved, loaded));
    loaded.load(savedPath); // Existing callers can continue ignoring the result.

    QuadWarp before;
    before.setSourceRect(Rect(1, 2, 3, 4));
    before.setTargetRect(Rect(5, 6, 7, 8));
    QuadWarp warp;
    warp.setSourceRect(Rect(1, 2, 3, 4));
    warp.setTargetRect(Rect(5, 6, 7, 8));

    auto rejectFile = [&](const std::string& name, const fs::path& file) {
        const std::string path = file.string();
        bool warning = false;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning &&
                e.message.find("QuadWarp") != std::string::npos &&
                e.message.find("Cannot load " + path + ": ") != std::string::npos) {
                warning = true;
            }
        });
        bool result = true;
        bool threw = false;
        try {
            result = warp.load(path);
        } catch (...) {
            threw = true;
        }
        check(name + " returns false without throwing", !threw && !result);
        check(name + " preserves both arrays", sameCorners(before, warp));
        check(name + " warns with path and reason", warning);
    };
    auto rejectText = [&](const std::string& name, const std::string& text) {
        const fs::path file = files.dir / "invalid.json";
        {
            std::ofstream output(file, std::ios::binary);
            output << text;
        }
        rejectFile(name, file);
    };
    auto reject = [&](const std::string& name, const Json& json) {
        rejectText(name, json.dump());
    };
    auto acceptExtras = [&](const std::string& name, const Json& json) {
        const fs::path file = files.dir / "extra-points.json";
        {
            std::ofstream output(file, std::ios::binary);
            output << json.dump();
        }
        QuadWarp candidate;
        check(name + " returns true", candidate.load(file.string()));
        check(name + " applies exactly the first four points", sameCorners(saved, candidate));
    };

    // Exercise both sides, including a late failure after valid source points.
    for (const char* side : {"src", "dst"}) {
        const std::string prefix = std::string(side) + ": ";
        Json bad = valid;
        bad["quadwarp"][side][3].erase("y");
        reject(prefix + "missing y", bad);
        bad = valid;
        bad["quadwarp"][side][3].erase("x");
        reject(prefix + "missing x", bad);
        for (const char* coordinate : {"x", "y"}) {
            for (const Json& value : {Json("600"), Json(nullptr), Json(true),
                                     Json::array(), Json::object(), Json(1e300), Json(-1e300)}) {
                bad = valid;
                bad["quadwarp"][side][3][coordinate] = value;
                reject(prefix + coordinate + " = " + value.dump(), bad);
            }
        }
        bad = valid;
        bad["quadwarp"][side] = Json::array({0, 0, 600, 0});
        reject(prefix + "flat number array", bad);
        bad = valid;
        bad["quadwarp"][side][3] = Json::array({1, 2});
        reject(prefix + "array instead of point object", bad);
        bad = valid;
        bad["quadwarp"][side].erase(3);
        reject(prefix + "fewer than four points", bad);
        Json extra = valid;
        extra["quadwarp"][side].push_back({{"x", 1}, {"y", 2}});
        acceptExtras(prefix + "more than four points", extra);
        extra = valid;
        extra["quadwarp"][side].push_back(nullptr);
        acceptExtras(prefix + "malformed fifth point", extra);
        bad = valid;
        bad["quadwarp"].erase(side);
        reject(prefix + "missing (only one side present)", bad);
        bad = valid;
        bad["quadwarp"][side] = Json::object();
        reject(prefix + "object instead of array", bad);
    }
    reject("missing quadwarp", Json::object());
    reject("quadwarp is not an object", {{"quadwarp", 42}});
    reject("both arrays missing", {{"quadwarp", Json::object()}});
    reject("root is not an object", Json::array());
    reject("null root", nullptr);
    rejectText("empty file", "");
    rejectText("malformed JSON", "{\"quadwarp\":");
    std::string overflow = valid.dump();
    const auto coordinate = overflow.find("\"x\":");
    const auto end = overflow.find(',', coordinate);
    overflow.replace(coordinate + 4, end - coordinate - 4, "1e400");
    rejectText("JSON number overflow", overflow);
    rejectFile("missing file", files.dir / "missing.json");

    check("valid load still succeeds after failures", warp.load(savedPath));
    check("valid load replaces both arrays", sameCorners(saved, warp));
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
