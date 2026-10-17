// #455: screenshot fallback paths, warnings, MCP replies and native encoders.
// Default: no GPU. --screen: also exercise deferred and synchronous captures.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"

#include <cstdio>
#include <cstring>
#include <fstream>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
fs::path sandbox;
vector<string> warnings;

void check(const string& name, bool ok) {
    printf("%-72s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

string supportedFormats() {
    if (Platform::isMacOS()) return "png, jpg/jpeg, bmp, tiff/tif, gif";
    if (Platform::isWindows()) return "png, jpg/jpeg, bmp, tga";
    return "png, jpg/jpeg, bmp";
}

void checkQueued(const fs::path& requested, const fs::path& expected, bool warn) {
    auto& queue = internal::currentWindowContext().pendingScreenshotPaths;
    queue.clear();
    const auto count = warnings.size();
    check("queue " + internal::pathToUtf8(requested.filename()), saveScreenshot(requested));
    check("queued actual destination", queue.size() == 1 && queue.front() == expected);
    check("warning count", warnings.size() == count + (warn ? 1 : 0));
    if (warn && warnings.size() > count) {
        check("warning names actual destination",
              warnings.back().find(internal::pathToUtf8(expected)) != string::npos);
        check("warning names supported formats",
              warnings.back().find(supportedFormats()) != string::npos);
    }
    queue.clear();
}

void checkMcp(const fs::path& requested, const fs::path& expected) {
    Json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                {"params", {{"name", "tc_save_screenshot"},
                            {"arguments", {{"path", internal::pathToUtf8(requested)}}}}}};
    const auto count = warnings.size();
    const string response = mcp::internal::Server::instance().processMessage(req.dump());
    auto& deferred = mcp::internal::deferralState();
    check("MCP screenshot defers its capture", response.empty() && deferred.hasEnvelope && bool(deferred.envelope));
    check("MCP fallback warns once", warnings.size() == count + 1);
    check("MCP warning names actual destination", warnings.size() > count &&
          warnings.back().find(internal::pathToUtf8(expected)) != string::npos);
    check("MCP capture retains admission credits", mcp::internal::workingMemory() > 0);
    check("MCP capture leaves application queue independent", internal::currentWindowContext().pendingScreenshotPaths.empty());
    // No GPU in this tier: cancel the queued producer after checking its
    // admitted lifetime. Native file encoders are exercised by --screen.
    deferred.envelope = {};
    deferred.hasEnvelope = false;
    check("cancelled MCP capture releases credits", mcp::internal::workingMemory() == 0);
}

void checkFile(const fs::path& path, const string& magic) {
    ifstream file(path, ios::binary);
    string header(magic.size(), '\0');
    file.read(header.data(), (streamsize)header.size());
    check("correct encoding: " + internal::pathToUtf8(path.filename()), header == magic);
    Pixels px;
    check("decode screenshot: " + internal::pathToUtf8(path.filename()), static_cast<bool>(px.load(path)));
    check("screenshot dimensions", px.getWidth() == getFramebufferWidth() && px.getHeight() == getFramebufferHeight());
}

class ScreenshotApp : public App {
    bool queued = false;
    size_t warningsBeforeCapture = 0;
public:
    void draw() override {
        clear(0.25f, 0.5f, 0.75f);
        if (queued) return;
        queued = true;
        warningsBeforeCapture = warnings.size();
        check("deferred unknown extension", saveScreenshot(sandbox / "deferred.xyz"));
        check("deferred no extension", saveScreenshot(sandbox / "deferred"));
        check("deferred mixed-case PNG", saveScreenshot(sandbox / "deferred.PnG"));
        check("deferred mixed-case JPEG", saveScreenshot(sandbox / "deferred.JpEg"));
        check("deferred mixed-case BMP", saveScreenshot(sandbox / "deferred.BmP"));
    }
    void update() override {
        if (!queued) return; // Previous frame's afterFrame drain has completed.
        check("deferred fallbacks warn once per path",
              warnings.size() == warningsBeforeCapture + 2);
        const string png("\x89PNG\r\n\x1a\n", 8);
        checkFile(sandbox / "deferred.xyz.png", png);
        checkFile(sandbox / "deferred.png", png);
        checkFile(sandbox / "deferred.PnG", png);
        checkFile(sandbox / "deferred.JpEg", string("\xff\xd8\xff", 3));
        checkFile(sandbox / "deferred.BmP", "BM");
        check("no file at unknown requested name", !fs::exists(sandbox / "deferred.xyz"));
        check("no file at extensionless requested name", !fs::exists(sandbox / "deferred"));
        const auto count = warnings.size();
        check("direct file worker", internal::captureWindowToFile(sandbox / "direct.xyz"));
        checkFile(sandbox / "direct.xyz.png", png);
        check("direct worker warns once", warnings.size() == count + 1);
        check("no duplicate suffix", !fs::exists(sandbox / "deferred.xyz.png.png"));
        exitApp();
    }
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    sandbox = fs::temp_directory_path() / "tc_screenshotExtension";
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    setDataPathRoot(sandbox);
    auto listener = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.rfind("[Screenshot]", 0) == 0)
            warnings.push_back(e.message);
    });
    for (const char* name : {"shot.PnG", "shot.JpG", "shot.JpEg", "shot.BmP"})
        checkQueued(sandbox / name, sandbox / name, false);
    for (const char* name : {"shot.xyz", "shot", "shot.", ".hidden", "画像.xyz"})
        checkQueued(sandbox / internal::utf8ToPath(name),
                    sandbox / internal::utf8ToPath(string(name) + ".png"), true);
    checkQueued("nested/relative.xyz", sandbox / "nested/relative.xyz.png", true);
    check("relative destination folder created", fs::is_directory(sandbox / "nested"));
    for (const char* name : {"shot.TiF", "shot.TiFf", "shot.GiF", "shot.TgA"}) {
        const bool supported = string(name) == "shot.TgA" ? Platform::isWindows() : Platform::isMacOS();
        checkQueued(sandbox / name, sandbox / (string(name) + (supported ? "" : ".png")), !supported);
    }
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    checkMcp("mcp.xyz", sandbox / "mcp.xyz.png");
    checkMcp(sandbox / "mcp", sandbox / "mcp.png");
    if (argc > 1 && strcmp(argv[1], "--screen") == 0) {
        WindowSettings settings;
        settings.setSize(64, 48);
        settings.setHighDpi(false);
        return tcCoreTest::runDisplayTest<ScreenshotApp>(settings, failures, [] {
            fs::remove_all(sandbox);
            printf("screenshotExtension: %s\n", failures ? "FAIL" : "OK");
        });
    }
    fs::remove_all(sandbox);
    printf("screenshotExtension: %s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
