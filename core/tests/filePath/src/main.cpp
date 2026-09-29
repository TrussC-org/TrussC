// =============================================================================
// core/tests/filePath — behavioral regression test for fs::path unification.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI on
// macOS / Linux / Windows).
//
// Guards the invariant: file paths survive end-to-end as fs::path — through
// getDataPath composition, the tc file utilities, and the C-library
// boundaries (stb, miniaudio, nlohmann, pugixml) — including non-ASCII
// (Japanese) names, spaces/parentheses, and absolute-path passthrough.
//
// Sections 1-7 save and load through the same narrow UTF-8 strings, so a
// Windows code page that mangles both directions the same way still round-
// trips there. Section 8 (#259) creates its names from u8 literals, which
// never go through the code page, and checks the UTF-8 strings going into and
// coming out of fs::path against them: the UTF-8 process code page from the
// Windows app manifest, and the helpers / logging that convert with
// pathToUtf8() instead of path::string().
// =============================================================================

#include <TrussC.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// check() for a body that may throw: an exception counts as FAIL instead of
// ending the run.
template <class F>
static void checkNoThrow(const char* name, F&& body) {
    bool ok = false;
    try {
        ok = body();
    } catch (const std::exception& e) {
        std::printf("  exception: %s\n", e.what());
    } catch (...) {
        std::printf("  exception (unknown type)\n");
    }
    check(name, ok);
}

// The bytes of a u8 literal as std::string: the UTF-8 the tc helpers must return
static string utf8(const char8_t* s) {
    return string(reinterpret_cast<const char*>(s));
}

// Minimal 16-bit mono PCM WAV (440 Hz-ish square, ~0.1 s @ 44100)
static vector<uint8_t> makeWavBytes() {
    const uint32_t sampleRate = 44100;
    const uint32_t numSamples = 4410;
    const uint32_t dataSize = numSamples * 2;
    vector<uint8_t> b;
    auto push32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); };
    auto push16 = [&](uint16_t v) { for (int i = 0; i < 2; i++) b.push_back(uint8_t(v >> (8 * i))); };
    auto pushStr = [&](const char* s) { while (*s) b.push_back(uint8_t(*s++)); };
    pushStr("RIFF"); push32(36 + dataSize); pushStr("WAVE");
    pushStr("fmt "); push32(16); push16(1); push16(1);
    push32(sampleRate); push32(sampleRate * 2); push16(2); push16(16);
    pushStr("data"); push32(dataSize);
    for (uint32_t i = 0; i < numSamples; i++) {
        push16(uint16_t(((i / 50) % 2) ? 12000 : -12000));
    }
    return b;
}

int main() {
    // Sandbox under the OS temp dir. On Windows this is an absolute path like
    // C:\Users\...\Temp — using it as the data-path root exercises the
    // fs::is_absolute() root detection (the old path[0]=='/' check failed it).
    const fs::path sandbox = fs::temp_directory_path() / "tc_filePath_test";
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    fs::create_directories(sandbox);

    // --- 1. data-path root + getDataPath composition ---
    {
        setDataPathRoot(sandbox);
        check("setDataPathRoot(absolute): root round-trips",
              fs::path(getDataPathRoot()) == sandbox ||
              fs::path(getDataPathRoot()) == sandbox / "");

        fs::path p = getDataPath("sub/file.txt");
        check("getDataPath(relative): resolves under the absolute root",
              p.is_absolute() && p == sandbox / "sub" / "file.txt");

        const fs::path abs = sandbox / "already_absolute.bin";
        check("getDataPath(absolute): passthrough unchanged",
              fs::path(getDataPath(abs)) == abs);
    }

    // --- 2. text round-trip: ASCII / Japanese name / Japanese content ---
    {
        check("saveTextFile: ASCII name", saveTextFile("ascii.txt", "hello"));
        check("loadTextFile: ASCII round-trip", loadTextFile("ascii.txt") == "hello");

        const string jpName = "日本語ファイル名.txt";
        const string jpBody = "こんにちはTrussC。改行\nもある。";
        check("saveTextFile: Japanese name", saveTextFile(jpName, jpBody));
        check("loadTextFile: Japanese name + content round-trip",
              loadTextFile(jpName) == jpBody);
        check("fileExists: Japanese name", fileExists(jpName));
        check("getFileSize: Japanese name", getFileSize(jpName) == (int64_t)jpBody.size());
    }

    // --- 3. spaces / parentheses / Japanese directory (exhibition-PC case) ---
    {
        const string dir = "新しいフォルダー (2)";
        check("createDirectory: Japanese + space + parens", createDirectory(dir));
        const string f = dir + "/テスト ファイル.txt";
        check("saveTextFile: file inside that directory", saveTextFile(f, "data"));
        check("loadTextFile: reads it back", loadTextFile(f) == "data");

        auto listing = listDirectory(dir);
        bool found = false;
        for (auto& e : listing) {
            if (fs::path(e).filename() == fs::path("テスト ファイル.txt")) found = true;
        }
        check("listDirectory: finds the Japanese-named file", found);

        check("removeFile: Japanese path", removeFile(f) && !fileExists(f));
    }

    // --- 4. binary round-trip through the stb boundary (PNG) ---
    {
        Pixels px;
        px.allocate(4, 4, 4);
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                px.setColor(x, y, Color(x / 3.0f, y / 3.0f, 0.5f, 1.0f));

        const string imgPath = "画像/テスト画像.png";
        createDirectory("画像");
        check("Pixels::save: PNG with Japanese path", px.save(imgPath));

        Pixels loaded;
        bool ok = loaded.load(getDataPath(imgPath)).ok();
        check("Pixels::load: PNG back from Japanese path", ok);
        bool same = ok && loaded.getWidth() == 4 && loaded.getHeight() == 4;
        if (same) {
            Color a = loaded.getColor(3, 0);
            same = a.r > 0.9f && a.g < 0.1f;   // (1, 0, 0.5) corner survived
        }
        check("Pixels round-trip: pixel data intact", same);
    }

    // --- 5. sound decode through the miniaudio boundary (WAV) ---
    {
        const auto wav = makeWavBytes();
        const fs::path wavPath = sandbox / "音" / "テスト音声.wav";
        fs::create_directories(wavPath.parent_path());
        {
            std::ofstream out(wavPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(wav.data()), wav.size());
        }
        Sound snd;
        check("Sound::load: WAV with Japanese absolute path", snd.load(wavPath).ok());
        check("Sound::load: decoded duration > 0", snd.getDuration() > 0.05f);
    }

    // --- 6. JSON / XML round-trip with Japanese paths and values ---
    {
        Json j;
        j["名前"] = "トラス";
        j["value"] = 42;
        createDirectory("設定");   // save helpers don't create parent dirs
        check("saveJson: Japanese path", saveJson(j, "設定/データ.json"));
        Json k = loadJson("設定/データ.json");
        check("loadJson: values round-trip",
              k.value("value", 0) == 42 && k.value("名前", string()) == "トラス");

        Xml xml;
        XmlNode root = xml.addRoot("root");
        root.append_attribute("名") = "値";
        check("Xml::save: Japanese path", xml.save("設定/データ.xml"));
        Xml xml2;
        check("Xml::load: back from Japanese path", xml2.load("設定/データ.xml"));
        check("Xml round-trip: root + Japanese attribute survive",
              string(xml2.root().name()) == "root" &&
              string(xml2.root().attribute("名").value()) == "値");
    }

    // --- 7. FileWriter / FileReader line round-trip ---
    {
        const string txtPath = "書き込み/ログ.txt";
        createDirectory("書き込み");
        {
            FileWriter w;
            check("FileWriter::open: Japanese path", w.open(txtPath));
            w.writeLine("一行目");
            w.writeLine("second line");
        }
        {
            FileReader r;
            bool ok = r.open(txtPath);   // relative: resolved via getDataPath
            check("FileReader::open: Japanese path", ok);
            check("FileReader: Japanese content line survives",
                  ok && r.readLine() == "一行目" && r.readLine() == "second line");
        }
    }

    // --- 8. UTF-8 strings into and out of fs::path (#259) ---
    // On Windows, before #259, fs::path(std::string) decoded UTF-8 in the
    // process ANSI code page, and path::string() / `log << path` threw for
    // characters outside it, so the non-ASCII checks here failed on a CP1252
    // or CP932 machine. The names are made from u8 literals, which fs::path
    // always decodes as UTF-8: they are exact on disk whatever the code page.
    {
#ifdef _WIN32
        const unsigned acp = ::GetACP();
        std::printf("GetACP() = %u\n", acp);
        check("GetACP() == 65001 (UTF-8 activeCodePage manifest embedded)", acp == 65001);
#endif
        const fs::path root = sandbox / "utf8";
        fs::create_directories(root);
        setDataPathRoot(root);

        struct Name { const char* label; const char8_t* name; };
        const vector<Name> names = {
            {"a.txt",                   u8"a.txt"},
            {"z.txt",                   u8"z.txt"},
            {"Japanese",                u8"日本語ファイル名.txt"},
            {"WAVE DASH U+301C",        u8"波\u301Cダッシュ.txt"},
            {"NFD (U+3099, from macOS)", u8"か\u3099.json"},
        };
        const char8_t* nfdJson = names[4].name;
        for (const auto& n : names) {
            // fs::path opens wide on Windows: the exact name lands on disk
            std::ofstream out(root / fs::path(n.name), std::ios::binary);
            out << (n.name == nfdJson ? "{\"v\":1}" : "x");
        }

        // Exit: directory entries come back as UTF-8, and the listing does
        // not stop at the first name the code page lacks
        checkNoThrow("listDirectory: all five names, as UTF-8", [&] {
            vector<string> got = listDirectory("");
            vector<string> want;
            for (const auto& n : names) want.push_back(utf8(n.name));
            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
            return got == want;
        });
        // ...and those strings work as paths again (entry direction)
        checkNoThrow("listDirectory names reopen via fileExists(std::string)", [&] {
            vector<string> got = listDirectory("");
            bool ok = got.size() == names.size();
            for (const auto& s : got) ok = ok && fileExists(s);
            return ok;
        });

        for (const auto& n : names) {
            const string label = string("getFileName: ") + n.label;
            checkNoThrow(label.c_str(), [&] {
                return getFileName(getDataPath("") / fs::path(n.name)) == utf8(n.name);
            });
        }
        checkNoThrow("getBaseName / getFileExtension: NFD name", [&] {
            const fs::path p = getDataPath("") / fs::path(nfdJson);
            return getBaseName(p) == utf8(u8"か\u3099") && getFileExtension(p) == "json";
        });
        checkNoThrow("joinPath / getParentDirectory / getAbsolutePath: UTF-8", [&] {
            const fs::path p = root / fs::path(nfdJson);
            return utf8ToPath(joinPath(root, fs::path(nfdJson))) == p &&
                   utf8ToPath(getParentDirectory(p)) == root &&
                   utf8ToPath(getAbsolutePath(p)) == fs::absolute(p);
        });

        // loadJson logs the path after a successful parse (logVerbose, which
        // formats whatever the log level)
        checkNoThrow("loadJson: NFD name parses and returns v == 1", [&] {
            Json j = loadJson(getDataPath("") / fs::path(nfdJson));
            return j.is_object() && j.value("v", 0) == 1;
        });
        // Xml::save / Xml::load log the path on success too
        checkNoThrow("Xml::save / Xml::load: WAVE DASH name", [&] {
            const fs::path p = getDataPath("") / fs::path(u8"波\u301C.xml");
            Xml out;
            out.addRoot("root");
            Xml in;
            return out.save(p) && in.load(p) && string(in.root().name()) == "root";
        });

        // Entry: a narrow UTF-8 literal names the file by its real name
        checkNoThrow("saveTextFile(narrow UTF-8 literal): real name on disk", [&] {
            if (!saveTextFile("写真.txt", "x")) return false;
            for (const auto& e : fs::directory_iterator(root)) {
#ifdef _WIN32
                if (e.path().filename().wstring() == L"写真.txt") return true;
#else
                if (e.path().filename() == fs::path(u8"写真.txt")) return true;
#endif
            }
            return false;
        });

        // `log << path` writes pathToUtf8(path): UTF-8, no quotes, no throw
        {
            vector<string> seen;
            EventListener sub = getLogger().onLog.listen([&](LogEventArgs& e) {
                seen.push_back(e.message);
            });
            for (const auto& n : names) {
                const string label = string("logNotice() << path: ") + n.label;
                checkNoThrow(label.c_str(), [&] {
                    const fs::path p = getDataPath("") / fs::path(n.name);
                    seen.clear();
                    logNotice() << "path: " << p;
                    return seen.size() == 1 && seen[0] == "path: " + pathToUtf8(p);
                });
            }
        }

        // VideoPlayer::load checks the path for a URL scheme before touching
        // the file: a missing file is FileNotFound, not an exception
        checkNoThrow("VideoPlayer::load(missing emoji name): FileNotFound", [&] {
            VideoPlayer video;
            LoadResult r = video.load(fs::path(u8"Café🎬.mp4"));
            return !r.ok() && r.error == LoadError::FileNotFound;
        });
    }

    fs::remove_all(sandbox, ec);

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
