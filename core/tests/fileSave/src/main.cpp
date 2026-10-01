// =============================================================================
// fileSave — the save helpers report write errors (#274)
//
// Headless, plain main(), exit code = pass/fail.
//
// - saveJson serializes before it opens the file: after {"a":1} is saved, a
//   saveJson with a string that is not valid UTF-8 returns false, logs an
//   error, and loadJson still returns a == 1.
// - saveJson writes the bytes of dump() as they are (binary mode: LF only).
// - saveTextFile, appendToFile, saveJson and Pixels::save check the write and
//   the close: on Linux, writing to /dev/full returns false and logs an error.
// - The successful paths still return true and write the expected bytes
//   (Pixels::save writes PNG / BMP signatures; overwriting a file works).
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

static int g_errors = 0;

static fs::path makeTestDir() {
    auto stamp = chrono::system_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() / ("trussc_fileSave_" + to_string(stamp));
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

static void checkJsonKeepsFileOnSerializeError(const fs::path& dir) {
    fs::path file = dir / "settings.json";
    check("saveJson {\"a\":1} returns true", saveJson(Json{{"a", 1}}, file));

    Json bad;
    bad["name"] = string("\x82\xa0");   // CP932 bytes, not valid UTF-8
    int before = g_errors;
    check("saveJson with invalid UTF-8 returns false", !saveJson(bad, file));
    check("saveJson with invalid UTF-8 logs an error", g_errors > before);

    Json back = loadJson(file);
    check("loadJson after the failed save still returns a == 1",
          back.is_object() && back.contains("a") && back["a"] == 1, back.dump());

    // Overwrite works, and the bytes on disk are dump()'s (LF, no CR)
    Json j2{{"b", Json::array({1, 2})}};
    check("saveJson overwrite returns true", saveJson(j2, file));
    string onDisk = loadTextFile(file);
    check("saveJson writes dump(2) byte for byte", onDisk == j2.dump(2));
    check("saveJson writes no CR", onDisk.find('\r') == string::npos);

    // Compact (indent < 0)
    check("saveJson compact returns true", saveJson(j2, file, -1));
    check("saveJson compact writes dump()", loadTextFile(file) == j2.dump());
}

static void checkSuccessPaths(const fs::path& dir) {
    fs::path txt = dir / "a.txt";
    check("saveTextFile returns true", saveTextFile(txt, "one\n"));
    check("saveTextFile overwrite returns true", saveTextFile(txt, "two\n"));
    check("appendToFile returns true", appendToFile(txt, "three\n"));
    string text = loadTextFile(txt);
    check("text file holds the overwrite plus the append", text == "two\nthree\n", text);

    Pixels px;
    px.allocate(16, 8, 4);
    for (const char* name : {"p.png", "p.jpg", "p.bmp"}) {
        fs::path f = dir / name;
        check(string("Pixels::save ") + name + " returns true", px.save(f));
        std::error_code ec;
        auto size = fs::file_size(f, ec);
        check(string("Pixels::save ") + name + " writes a non-empty file", !ec && size > 0);
    }
    string png = loadTextFile(dir / "p.png");
    check("Pixels::save .png starts with the PNG signature",
          png.size() > 8 && png.compare(0, 8, string("\x89PNG\r\n\x1a\n", 8)) == 0);
    string jpg = loadTextFile(dir / "p.jpg");
    check("Pixels::save .jpg starts with the JPEG SOI marker",
          jpg.size() > 2 && (unsigned char)jpg[0] == 0xFF && (unsigned char)jpg[1] == 0xD8);
    string bmp = loadTextFile(dir / "p.bmp");
    check("Pixels::save .bmp starts with BM", bmp.size() > 2 && bmp.compare(0, 2, "BM") == 0);
}

static void checkDevFull() {
#if defined(__linux__)
    std::error_code ec;
    if (!fs::exists("/dev/full", ec)) {
        printf("/dev/full not present: skipped\n");
        return;
    }
    int before = g_errors;
    check("saveTextFile to /dev/full (1 MiB) returns false",
          !saveTextFile("/dev/full", string(1 << 20, 'x')));
    check("saveTextFile to /dev/full (1 byte) returns false", !saveTextFile("/dev/full", "x"));
    check("appendToFile to /dev/full returns false", !appendToFile("/dev/full", "x"));
    check("saveJson to /dev/full returns false", !saveJson(Json{{"a", 1}}, "/dev/full"));
    Pixels px;
    px.allocate(64, 64, 4);
    check("Pixels::save to /dev/full returns false", !px.save("/dev/full"));
    check("each /dev/full failure logs an error", g_errors - before >= 5,
          to_string(g_errors - before));
#else
    printf("/dev/full checks: Linux only, skipped\n");
#endif
}

} // namespace

TC_CORE_TEST_MAIN() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Error) ++g_errors;
    });

    fs::path dir = makeTestDir();
    checkJsonKeepsFileOnSerializeError(dir);
    checkSuccessPaths(dir);
    checkDevFull();

    std::error_code ec;
    fs::remove_all(dir, ec);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
