// =============================================================================
// core/tests/dataPathLoads — behavioral regression test for the loaders' path
// rule (#273).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI on
// macOS / Linux / Windows).
//
// Guards the invariants:
//   - getDataPath() / getDataPathRoot() called from two threads at once,
//     before anything else has touched the data path, give the same result
//     as a later call on the main thread (run it under TSan where available);
//   - Pixels::load / loadHDR, Sound::load / loadStream and tcxLut's
//     Lut3D::load resolve a relative path against getDataPath(), not the
//     working directory (the CWD is moved elsewhere for the whole run), with
//     no CWD fallback: a file that exists only in the CWD is not found;
//   - Pixels::save("a.png") followed by Pixels::load("a.png") round-trips;
//   - a UTF-8 file name in the data folder loads through Sound::load;
//   - an absolute path is used as given.
// Font::load and a full Lut3D::load create GPU resources, so they are not
// run here; Lut3D is checked up to its .cube parse (the TITLE line is read
// from the data-folder file, then an invalid size stops it before the GPU).
// =============================================================================

#include <TrussC.h>
#include <tcLut.h>
#include "../../common/tcCoreTest.h"

#include <barrier>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok) {
    std::printf("%-72s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// The bytes of a u8 literal as std::string
static string utf8(const char8_t* s) {
    return string(reinterpret_cast<const char*>(s));
}

static bool writeBytes(const fs::path& p, const void* data, size_t size) {
    ofstream out(p, ios::binary);
    out.write(static_cast<const char*>(data), (streamsize)size);
    return (bool)out;
}

static bool writeText(const fs::path& p, const string& text) {
    return writeBytes(p, text.data(), text.size());
}

// A short 16-bit mono PCM WAV (a 440 Hz tone)
static vector<uint8_t> makeWav(int frames = 4410, int rate = 44100) {
    vector<uint8_t> b;
    auto u32 = [&b](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); };
    auto u16 = [&b](uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); };
    auto tag = [&b](const char* t) { b.insert(b.end(), t, t + 4); };
    const uint32_t dataBytes = (uint32_t)frames * 2;
    tag("RIFF"); u32(36 + dataBytes); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    tag("data"); u32(dataBytes);
    for (int i = 0; i < frames; ++i) {
        u16((uint16_t)(int16_t)(8000 * sin(TAU * 440.0 * i / rate)));
    }
    return b;
}

// Error lines logged while it is alive
struct ErrorCapture {
    vector<string> errors;
    EventListener sub = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level >= LogLevel::Error) errors.push_back(e.message);
    });
    bool has(const string& part) const {
        for (auto& m : errors) if (m.find(part) != string::npos) return true;
        return false;
    }
};

} // namespace

TC_CORE_TEST_MAIN() {
    // --- getDataPath from two threads before anything else -----------------
    {
        barrier ready(2);
        fs::path a, b, rootA, rootB;
        auto worker = [&ready](fs::path& out, fs::path& root) {
            ready.arrive_and_wait();   // start both calls together
            out = getDataPath("x.png");
            root = getDataPathRoot();
        };
        thread t1(worker, ref(a), ref(rootA));
        thread t2(worker, ref(b), ref(rootB));
        t1.join();
        t2.join();
        check("getDataPath: two threads agree", a == b && rootA == rootB);
        check("getDataPath: threads agree with the main thread",
              a == getDataPath("x.png") && rootA == getDataPathRoot());
        check("getDataPath: result is absolute", a.is_absolute());
    }

    const fs::path original = fs::current_path();
    const fs::path sandbox = fs::temp_directory_path() / "tc_dataPathLoads_test";
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    const fs::path data = sandbox / "data";
    const fs::path cwd = sandbox / "cwd";
    fs::create_directories(data);
    fs::create_directories(cwd);
    // #365 P2: normalize the data root, preserving the caller's filename.
    const fs::path filename = "symlink/../asset.wav";
    const fs::path relativeRoot = "unused/../data";
    setDataPathRoot(relativeRoot);
    check("getDataPath: relative root is normalized, filename is preserved",
          getDataPath(filename) == getExecutableDir() / "data" / filename);
    setDataPathRoot(sandbox / "unused/../data");
    check("getDataPath: absolute root is normalized, filename is preserved",
          getDataPath(filename) == data / filename);
    const fs::path absoluteInput = data / "symlink/../asset.wav";
    check("getDataPath: absolute input passes through unchanged",
          getDataPath(absoluteInput) == absoluteInput);
    setDataPathRoot(data);
    // A relative path that went through the CWD would land here instead
    fs::current_path(cwd);

    // --- Pixels ---------------------------------------------------------------
    {
        Pixels px;
        px.allocate(4, 4, 4);
        px.setColor(1, 2, Color(1, 0, 0, 1));
        check("Pixels::save(\"a.png\") writes into the data folder",
              px.save("a.png") && fs::exists(data / "a.png"));
        Pixels q;
        LoadResult r = q.load("a.png");
        check("Pixels::load(\"a.png\") reads it back", r.ok() && q.getWidth() == 4 &&
              q.getHeight() == 4 && q.getColor(1, 2).r > 0.99f);

        Pixels abs;
        check("Pixels::load: absolute path used as given", abs.load(data / "a.png").ok());

        Pixels workerPixels;
        LoadResult workerResult;
        thread worker([&] { workerResult = workerPixels.load("a.png"); });
        worker.join();
        check("Pixels::load: a worker thread reads from the data folder",
              workerResult.ok() && workerPixels.getWidth() == 4 &&
              workerPixels.getColor(1, 2).r > 0.99f);

#ifndef _WIN32
        // The filename's .. follows the symlink at the filesystem boundary.
        // Normalizing the full path would incorrectly look in data/ instead.
        fs::create_directories(sandbox / "target/child");
        fs::create_directory_symlink(sandbox / "target/child", data / "symlink");
        fs::copy_file(data / "a.png", sandbox / "target/outside.png");
        Pixels symlinkPixels;
        check("Pixels::load: filename symlink/.. keeps filesystem semantics",
              symlinkPixels.load("symlink/../outside.png").ok() &&
              symlinkPixels.getWidth() == 4);
#endif

        // Only in the CWD: not found (no CWD fallback)
        fs::copy_file(data / "a.png", cwd / "cwdOnly.png", ec);
        Pixels c;
        LoadResult rc = c.load("cwdOnly.png");
        check("Pixels::load: a file only in the CWD is not found",
              !rc.ok() && rc.error == LoadError::FileNotFound);
    }

    // --- Pixels::loadHDR --------------------------------------------------------
    {
        const float rgb[2 * 2 * 3] = {0.5f, 1.0f, 2.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        vector<uint8_t> hdr;
        stbi_write_hdr_to_func([](void* ctx, void* d, int n) {
            auto* v = static_cast<vector<uint8_t>*>(ctx);
            v->insert(v->end(), (uint8_t*)d, (uint8_t*)d + n);
        }, &hdr, 2, 2, 3, rgb);
        check("loadHDR: test file written", writeBytes(data / "env.hdr", hdr.data(), hdr.size()));
        Pixels h;
        LoadResult r = h.loadHDR("env.hdr");
        check("Pixels::loadHDR(\"env.hdr\") reads from the data folder",
              r.ok() && h.isFloat() && h.getWidth() == 2 && h.getColor(0, 0).b > 1.5f);

        writeBytes(cwd / "cwdOnly.hdr", hdr.data(), hdr.size());
        Pixels c;
        LoadResult rc = c.loadHDR("cwdOnly.hdr");
        check("Pixels::loadHDR: a file only in the CWD is not found",
              !rc.ok() && rc.error == LoadError::FileNotFound);
    }

    check("null backend starts", AudioEngine::getInstance().init(
        AudioSettings{.backend = AudioBackend::Null}));

    // --- Sound -----------------------------------------------------------------
    {
        const vector<uint8_t> wav = makeWav();
        const fs::path jpName = utf8ToPath(utf8(u8"テスト音声.wav"));
        check("Sound: test files written", writeBytes(data / jpName, wav.data(), wav.size()) &&
              writeBytes(cwd / "cwdOnly.wav", wav.data(), wav.size()));

        Sound s;
        check("Sound::load(\"テスト音声.wav\") reads from the data folder",
              s.load(jpName).ok() && s.getDuration() > 0.05f);

        Sound st;
        check("Sound::loadStream(\"テスト音声.wav\") reads from the data folder",
              st.loadStream(jpName).ok() && st.isStreaming());

        Sound abs;
        check("Sound::load: absolute path used as given", abs.load(data / jpName).ok());

        Sound c, cs;
        check("Sound::load: a file only in the CWD is not found", !c.load("cwdOnly.wav").ok());
        check("Sound::loadStream: a file only in the CWD is not found",
              !cs.loadStream("cwdOnly.wav").ok());
    }

    // --- Lut3D (tcxLut), up to the .cube parse --------------------------------
    {
        // TITLE is read first; LUT_3D_SIZE 1 is then refused before any GPU work
        check("Lut3D: test files written",
              writeText(data / "x.cube", "TITLE \"fromData\"\nLUT_3D_SIZE 1\n") &&
              writeText(cwd / "cwdOnly.cube", "TITLE \"fromCwd\"\nLUT_3D_SIZE 1\n"));

        tcx::lut::Lut3D lut;
        ErrorCapture cap;
        check("Lut3D::load(\"x.cube\") opens the data-folder file",
              !lut.load("x.cube") && lut.getTitle() == "fromData" &&
              cap.has("invalid LUT size") && !cap.has("failed to open"));

        tcx::lut::Lut3D c;
        ErrorCapture capC;
        check("Lut3D::load: a file only in the CWD is not opened",
              !c.load("cwdOnly.cube") && capC.has("failed to open"));
    }

    fs::current_path(original);
    fs::remove_all(sandbox, ec);

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail,
                g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
