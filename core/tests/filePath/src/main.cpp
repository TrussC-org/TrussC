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
// On Windows this exercises the UTF-8 → wide conversions; a regression to
// ACP-narrow file IO fails here with Japanese names.
// =============================================================================

#include <TrussC.h>

#include <cstdio>
#include <cstdint>
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
        bool ok = loaded.load(getDataPath(imgPath));
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
        check("Sound::load: WAV with Japanese absolute path", snd.load(wavPath));
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

    fs::remove_all(sandbox, ec);

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
