// Headless file-loading regression tests, discovered by --addon-tests-only.
#include <tcxQuadWarp.h>

#include <chrono>
#include <cstdio>
#include <fstream>

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
} // namespace

int main() {
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
