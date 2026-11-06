// =============================================================================
// fileSave — the save helpers report write errors (#274)
//
// Headless, registered in allCoreTests; exit code = pass/fail.
//
// - saveJson serializes before it opens the file: after {"a":1} is saved, a
//   saveJson with a string that is not valid UTF-8 returns false, logs an
//   error, and loadJson still returns a == 1.
// - saveJson writes the bytes of dump() as they are (binary mode: LF only).
// - saveTextFile, appendToFile, saveJson and Pixels::save check the write and
//   the close: on Linux, writing to /dev/full returns false and logs an error.
// - The successful paths still return true and write the expected bytes
//   (Pixels::save writes PNG / BMP signatures; overwriting a file works).
// - PNG output is straight (non-premultiplied) RGBA on every OS (#854): pixels
//   with alpha 0 / 128 / 255 keep their exact RGBA through Pixels::save and the
//   screenshot writer, and a screenshot JPEG is not darkened by alpha.
// - macOS TIFF output also preserves exact straight RGBA (#854).
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include <ImageIO/ImageIO.h>
#endif

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
    check("saveJson {\"a\":1} returns true", saveJson(Json{{"a", 1}}, file).ok());

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
    check("saveJson overwrite returns true", saveJson(j2, file).ok());
    string onDisk = loadTextFile(file);
    check("saveJson writes dump(2) byte for byte", onDisk == j2.dump(2));
    check("saveJson writes no CR", onDisk.find('\r') == string::npos);

    // Compact (indent < 0)
    check("saveJson compact returns true", saveJson(j2, file, -1).ok());
    check("saveJson compact writes dump()", loadTextFile(file) == j2.dump());
}

static void checkSuccessPaths(const fs::path& dir) {
    fs::path txt = dir / "a.txt";
    check("saveTextFile returns true", saveTextFile(txt, "one\n").ok());
    check("saveTextFile overwrite returns true", saveTextFile(txt, "two\n").ok());
    check("appendToFile returns true", appendToFile(txt, "three\n").ok());
    string text = loadTextFile(txt);
    check("text file holds the overwrite plus the append", text == "two\nthree\n", text);

    Pixels px;
    px.allocate(16, 8, 4);
    for (const char* name : {"p.png", "p.jpg", "p.bmp"}) {
        fs::path f = dir / name;
        check(string("Pixels::save ") + name + " returns true", px.save(f).ok());
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

#if defined(__APPLE__) && !TARGET_OS_IPHONE
static void checkStraightAlphaTiff(const fs::path& file, const unsigned char* expected) {
    // Read ImageIO's provider bytes directly. Pixels::load draws through a
    // context that discards alpha; drawing or unpremultiplying here could hide
    // the encoder bug. Requiring alphaLast rejects associated-alpha TIFFs.
    const string bytes = loadTextFile(file);
    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(bytes.data()), bytes.size());
    CGImageSourceRef source = data ? CGImageSourceCreateWithData(data, nullptr) : nullptr;
    if (data) CFRelease(data);
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    if (source) CFRelease(source);
    const string label = "screenshot " + file.extension().string();
    const bool layout = image && CGImageGetWidth(image) == 3 && CGImageGetHeight(image) == 1 &&
        CGImageGetBitsPerComponent(image) == 8 && CGImageGetBitsPerPixel(image) == 32 &&
        CGImageGetAlphaInfo(image) == kCGImageAlphaLast;
    check(label + " decodes as 3x1 straight RGBA", layout);
    CFDataRef decoded = layout ? CGDataProviderCopyData(CGImageGetDataProvider(image)) : nullptr;
    if (image) CGImageRelease(image);
    check(label + " keeps exact RGB at alpha 0 / 128 / 255",
          decoded && CFDataGetLength(decoded) >= 12 &&
          memcmp(CFDataGetBytePtr(decoded), expected, 12) == 0);
    if (decoded) CFRelease(decoded);
}
#endif

// RGBA must survive a PNG write and read unchanged, including the RGB of a
// fully transparent pixel (#854: macOS used to premultiply it away).
static void checkStraightAlphaPng(const fs::path& dir) {
    const unsigned char expected[3][4] = {
        {200, 100, 50, 0},     // alpha 0 keeps its color
        {200, 100, 50, 128},   // low alpha keeps full precision
        {200, 100, 50, 255},
    };
    Pixels px;
    px.allocate(3, 1, 4);
    memcpy(px.getData(), expected, sizeof(expected));

    auto matches = [&](const fs::path& file, const string& label) {
        Pixels back;
        bool loaded = back.load(file).ok();
        check(label + " loads back as 3x1 RGBA",
              loaded && back.getWidth() == 3 && back.getHeight() == 1 && back.getChannels() == 4);
        if (!loaded || back.getWidth() != 3 || back.getHeight() != 1 || back.getChannels() != 4) return;
        for (int i = 0; i < 3; i++) {
            const unsigned char* got = back.getData() + i * 4;
            string detail = "got " + to_string(got[0]) + "," + to_string(got[1]) + "," +
                            to_string(got[2]) + "," + to_string(got[3]);
            check(label + " keeps pixel " + to_string(i) + " exactly (alpha " +
                  to_string(expected[i][3]) + ")", memcmp(got, expected[i], 4) == 0, detail);
        }
    };

    fs::path viaSave = dir / "straight_save.png";
    check("Pixels::save straight-alpha PNG returns true", px.save(viaSave).ok());
    matches(viaSave, "Pixels::save PNG");

    fs::path viaShot = dir / "straight_shot.png";
    check("screenshot writer straight-alpha PNG returns true",
          internal::saveScreenshotPixels(px, viaShot));
    matches(viaShot, "screenshot PNG");

#if defined(__APPLE__) && !TARGET_OS_IPHONE
    for (const char* name : {"straight_shot.tiff", "straight_shot.tif"}) {
        fs::path tiff = dir / name;
        check(string("screenshot writer ") + name + " returns true",
              internal::saveScreenshotPixels(px, tiff));
        checkStraightAlphaTiff(tiff, &expected[0][0]);
    }
#endif

    // JPEG has no alpha: the RGB must not be scaled by the alpha value.
    Pixels flat;
    flat.allocate(16, 16, 4);
    for (int i = 0; i < 16 * 16; i++) {
        unsigned char* d = flat.getData() + i * 4;
        d[0] = 200; d[1] = 100; d[2] = 50; d[3] = 128;
    }
    fs::path jpg = dir / "straight_shot.jpg";
    check("screenshot writer JPEG returns true", internal::saveScreenshotPixels(flat, jpg));
    Pixels jback;
    bool jloaded = jback.load(jpg).ok();
    check("screenshot JPEG loads back", jloaded);
    if (jloaded) {
        const unsigned char* d = jback.getData();
        auto within8 = [](int a, int b) { return abs(a - b) <= 8; };
        check("screenshot JPEG RGB is not darkened by alpha",
              within8(d[0], 200) && within8(d[1], 100) && within8(d[2], 50),
              "got " + to_string(d[0]) + "," + to_string(d[1]) + "," + to_string(d[2]));
    }
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
    checkStraightAlphaPng(dir);
    checkDevFull();

    std::error_code ec;
    fs::remove_all(dir, ec);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
