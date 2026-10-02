// =============================================================================
// core/tests/userDataPath — behavioral regression test for where the app
// writes (#433).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI on
// macOS / Linux / Windows).
//
// Guards the invariants:
//   - getUserDataPath() is the OS per-user app folder and getTempPath() the
//     OS temp folder + the app's name, created on first use:
//       Linux:   $XDG_DATA_HOME/<exe>/ (an absolute value only), else
//                $HOME/.local/share/<exe>/; $TMPDIR/<exe>/, else /tmp/<exe>/
//       Windows: %LOCALAPPDATA%\<exe>\ ; the temp folder (%TMP%) \<exe>\
//       macOS:   ~/Library/Application Support/<id>/ ; $TMPDIR/<id>/
//     (environment overrides on Linux and Windows; on macOS the parent
//     folders are checked);
//   - an absolute argument is returned as is, a relative one is joined to the
//     folder;
//   - setUserDataPathRoot() fixes the folder (absolute as given, relative
//     against the executable directory), and a file saved there through
//     getUserDataPath() is read back from the same place;
//   - with a (simulated) app bundle, every core writer (saveTextFile,
//     appendToFile, FileWriter::open, saveJson, Xml::save, Pixels::save,
//     Image::save, setLogFile, VideoWriter::open, saveScreenshot (queued),
//     AudioRecorder::start)
//     refuses a path that resolves inside it: returns false, logs exactly one
//     Error that names the file and getUserDataPath(), and creates nothing;
//     relative (data folder inside the bundle), absolute, and through a
//     setUserDataPathRoot() inside the bundle alike. A sibling folder whose
//     name starts with the bundle's is not inside it, and writes through
//     getUserDataPath() still work.
//   - On macOS / iOS only: the first relative write into the data folder
//     outside a bundle logs one Notice naming getUserDataPath(), once per
//     process.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok) {
    std::printf("%-76s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// Same folder, whatever the trailing separator or "..": compares the
// normalized absolute forms.
static bool samePath(const fs::path& a, const fs::path& b) {
    auto norm = [](fs::path p) {
        p = fs::absolute(p).lexically_normal();
        if (p.filename().empty()) p = p.parent_path();
        return p;
    };
    return norm(a) == norm(b);
}

// Log lines (Notice and above) logged while it is alive
struct LogCapture {
    vector<string> errors;
    vector<string> notices;
    EventListener sub = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level >= LogLevel::Error) errors.push_back(e.message);
        else if (e.level == LogLevel::Notice) notices.push_back(e.message);
    });
};

static void setEnv(const char* name, const fs::path& value) {
#ifdef _WIN32
    _wputenv_s(fs::path(name).wstring().c_str(), value.wstring().c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

static void unsetEnv(const char* name) {
#ifdef _WIN32
    _wputenv_s(fs::path(name).wstring().c_str(), L"");
#else
    unsetenv(name);
#endif
}

// Saves an environment variable and puts it back on scope exit
struct EnvRestore {
    string name;
    bool had = false;
    string value;
    explicit EnvRestore(const char* n) : name(n) {
        if (const char* v = std::getenv(n)) { had = true; value = v; }
    }
    ~EnvRestore() {
        if (had) setEnv(name.c_str(), fs::path(value));
        else unsetEnv(name.c_str());
    }
};

// One writer under test: write(path) returns what the writer returned.
struct Writer {
    string name;
    string ext;
    function<bool(const fs::path&)> write;
};

// A write outside the bundle went through. VideoWriter depends on the
// platform's encoder being present, and saveScreenshot only queues the
// capture, so for those two only the refusal is ruled out.
static bool notRefused(const Writer& w, bool ok, const LogCapture& cap, const fs::path& target) {
    if (w.name == "VideoWriter::open" || w.name == "saveScreenshot") {
        for (const auto& e : cap.errors) {
            if (e.find("inside the app bundle") != string::npos) return false;
        }
        return true;
    }
    return ok && cap.errors.empty() && fs::is_regular_file(target);
}

static void checkPlatformFolders(const fs::path& sandbox) {
    std::error_code ec;
#if defined(__linux__)
    const fs::path app = getExecutablePath().filename();
    EnvRestore xdg("XDG_DATA_HOME"), home("HOME"), tmp("TMPDIR");

    setEnv("XDG_DATA_HOME", sandbox / "xdg");
    internal::resetUserDataPathForTests();
    const fs::path u = getUserDataPath();
    check("Linux: getUserDataPath() is $XDG_DATA_HOME/<exe>/",
          samePath(u, sandbox / "xdg" / app));
    check("Linux: ... and the folder is created on first use",
          fs::is_directory(sandbox / "xdg" / app));
    check("Linux: getUserDataPath(\"a/b.json\") joins the folder",
          samePath(getUserDataPath("a/b.json"), sandbox / "xdg" / app / "a" / "b.json"));

    unsetEnv("XDG_DATA_HOME");
    setEnv("HOME", sandbox / "home");
    internal::resetUserDataPathForTests();
    check("Linux: no XDG_DATA_HOME -> $HOME/.local/share/<exe>/",
          samePath(getUserDataPath(), sandbox / "home" / ".local" / "share" / app));

    setEnv("XDG_DATA_HOME", fs::path("relative/xdg"));
    internal::resetUserDataPathForTests();
    check("Linux: a relative XDG_DATA_HOME is ignored (XDG spec)",
          samePath(getUserDataPath(), sandbox / "home" / ".local" / "share" / app));

    setEnv("TMPDIR", sandbox / "tmp");
    internal::resetUserDataPathForTests();
    check("Linux: getTempPath() is $TMPDIR/<exe>/",
          samePath(getTempPath(), sandbox / "tmp" / app) &&
          fs::is_directory(sandbox / "tmp" / app));
    check("Linux: getTempPath(\"c.bin\") joins the folder",
          samePath(getTempPath("c.bin"), sandbox / "tmp" / app / "c.bin"));

    unsetEnv("TMPDIR");
    internal::resetUserDataPathForTests();
    check("Linux: no TMPDIR -> /tmp/<exe>/", samePath(getTempPath(), fs::path("/tmp") / app));
    fs::remove(fs::path("/tmp") / app, ec);   // only if this test made it empty
#elif defined(_WIN32)
    const fs::path app = getExecutablePath().stem();
    EnvRestore local("LOCALAPPDATA"), tmp("TMP"), temp("TEMP");

    setEnv("LOCALAPPDATA", sandbox / "local");
    internal::resetUserDataPathForTests();
    check("Windows: getUserDataPath() is %LOCALAPPDATA%\\<exe>\\",
          samePath(getUserDataPath(), sandbox / "local" / app) &&
          fs::is_directory(sandbox / "local" / app));

    // Reading the OS root must not silently select a different folder for
    // a long environment value. No filesystem write is needed for this check.
    fs::path longLocal = sandbox;
    for (int i = 0; i < 100; ++i) longLocal /= "segment";
    setEnv("LOCALAPPDATA", longLocal);
    check("Windows: a long LOCALAPPDATA is preserved",
          samePath(internal::platformUserDataRoot(), longLocal / app));

    setEnv("TMP", sandbox / "tmp");
    setEnv("TEMP", sandbox / "tmp");
    internal::resetUserDataPathForTests();
    check("Windows: getTempPath() is %TMP%\\<exe>\\",
          samePath(getTempPath(), sandbox / "tmp" / app) &&
          fs::is_directory(sandbox / "tmp" / app));
#elif defined(__APPLE__)
    internal::resetUserDataPathForTests();
    const char* homeEnv = std::getenv("HOME");
    const fs::path u = getUserDataPath();
    check("macOS: getUserDataPath() is ~/Library/Application Support/<id>/",
          homeEnv && samePath(u.parent_path().parent_path(),
                              fs::path(homeEnv) / "Library" / "Application Support") &&
          fs::is_directory(u));
    const char* tmpEnv = std::getenv("TMPDIR");
    const fs::path t = getTempPath();
    check("macOS: getTempPath() is $TMPDIR/<id>/",
          (!tmpEnv || samePath(t.parent_path().parent_path(), fs::path(tmpEnv))) &&
          fs::is_directory(t));
    check("macOS: both use the same app folder name",
          u.parent_path().filename() == t.parent_path().filename());
#endif
    internal::resetUserDataPathForTests();

    // Absolute arguments pass through
    const fs::path abs = sandbox / "elsewhere" / "x.json";
    check("getUserDataPath(absolute) returns it as is", getUserDataPath(abs) == abs);
    check("getTempPath(absolute) returns it as is", getTempPath(abs) == abs);
}

} // namespace

TC_CORE_TEST_MAIN() {
    const fs::path sandbox = fs::absolute(fs::temp_directory_path() / "tc_userDataPath_test");
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    fs::create_directories(sandbox);

    // Device-less audio engine for AudioRecorder::start
    internal::setNullAudioBackendForTests(true);
    getMainThreadId();
    auto& engine = AudioEngine::getInstance();
    const bool audioUp = engine.init(AudioSettings{});
    check("audio engine starts on the null backend", audioUp);

    // --- per-platform folders ---
    checkPlatformFolders(sandbox);

    // --- setUserDataPathRoot ---
    {
        const fs::path root = sandbox / "exhibit";
        setUserDataPathRoot(root);
        check("setUserDataPathRoot(absolute): getUserDataPath() is that folder",
              samePath(getUserDataPath(), root));
        check("setUserDataPathRoot(absolute): created on first use", fs::is_directory(root));
        Json j;
        j["v"] = 7;
        check("saveJson(getUserDataPath(...)) returns true",
              saveJson(j, getUserDataPath("settings.json")));
        check("... the file lands in the set root", fs::is_regular_file(root / "settings.json"));
        check("... and loadJson(getUserDataPath(...)) reads it back",
              loadJson(getUserDataPath("settings.json")).value("v", 0) == 7);

        setUserDataPathRoot("userDataRel");
        check("setUserDataPathRoot(relative): resolved against the executable directory",
              samePath(getUserDataPath(), getExecutableDir() / "userDataRel"));
        fs::remove_all(getExecutableDir() / "userDataRel", ec);
    }

    // --- writes into a simulated app bundle are refused ---
    const fs::path bundle = sandbox / "Fake.app";
    const fs::path bundleData = bundle / "Contents" / "Resources" / "data";
    fs::create_directories(bundleData);
    { ofstream(bundleData / "bundled.txt") << "read me"; }
    internal::setAppBundlePathForTests(bundle);
    setDataPathRoot(bundleData);
    const fs::path userRoot = sandbox / "user";
    setUserDataPathRoot(userRoot);

    check("reads from the bundle still work",
          loadTextFile("bundled.txt") == "read me");

    Pixels px;
    px.allocate(2, 2, 4);
    Image img;
    img.allocate(2, 2, 4);

    const vector<Writer> writers = {
        {"saveTextFile", ".txt", [](const fs::path& p) { return saveTextFile(p, "hello"); }},
        {"appendToFile", ".txt", [](const fs::path& p) { return appendToFile(p, "hello"); }},
        {"FileWriter::open", ".txt", [](const fs::path& p) {
            FileWriter w;
            return w.open(p);
        }},
        {"FileWriter::open(append)", ".txt", [](const fs::path& p) {
            FileWriter w;
            return w.open(p, true);
        }},
        {"saveJson", ".json", [](const fs::path& p) {
            Json j;
            j["v"] = 1;
            return saveJson(j, p);
        }},
        {"Xml::save", ".xml", [](const fs::path& p) {
            Xml xml;
            xml.addRoot("root");
            return xml.save(p);
        }},
        {"Pixels::save", ".png", [&px](const fs::path& p) { return px.save(p); }},
        {"Image::save", ".png", [&img](const fs::path& p) { return img.save(p); }},
        {"setLogFile", ".log", [](const fs::path& p) {
            const bool ok = setLogFile(p);
            closeLogFile();
            return ok;
        }},
        {"VideoWriter::open", ".mp4", [](const fs::path& p) {
            VideoWriter w;
            return w.open(p, 16, 16);
        }},
#ifndef __EMSCRIPTEN__
        // Queues the capture (a console test has no frame to drain it)
        {"saveScreenshot", ".png", [](const fs::path& p) { return saveScreenshot(p); }},
#endif
        {"AudioRecorder::start", ".wav", [](const fs::path& p) {
            AudioRecorder rec;
            const bool ok = rec.start(p);
            rec.stop();
            return ok;
        }},
    };

    int n = 0;
    for (const auto& w : writers) {
        const string tag = "w" + to_string(n++);
        const bool isRealWriter = !(w.name == "AudioRecorder::start" && !audioUp);

        // Relative: resolves into the bundle's data folder
        {
            const string file = tag + "_rel" + w.ext;
            const fs::path rel = fs::path(tag) / file;
            LogCapture cap;
            const bool ok = w.write(rel);
            check(w.name + ": relative into the bundle -> returns false", !ok);
            check(w.name + ": ... logs exactly one Error naming the file and getUserDataPath()",
                  cap.errors.size() == 1 &&
                  cap.errors[0].find(file) != string::npos &&
                  cap.errors[0].find("getUserDataPath(") != string::npos);
            check(w.name + ": ... creates nothing in the bundle", !fs::exists(bundleData / tag));
        }

        // Absolute path inside the bundle
        {
            const fs::path abs = bundle / "Contents" / (tag + "_abs") / ("f" + w.ext);
            LogCapture cap;
            const bool ok = w.write(abs);
            check(w.name + ": absolute into the bundle -> returns false, one Error",
                  !ok && cap.errors.size() == 1);
            check(w.name + ": ... creates nothing", !fs::exists(abs.parent_path()));
        }

        // Through getUserDataPath(): written
        if (isRealWriter) {
            const fs::path target = getUserDataPath(fs::path(tag) / ("ok" + w.ext));
            LogCapture cap;
            const bool ok = w.write(target);
            check(w.name + ": through getUserDataPath() -> written, no Error",
                  notRefused(w, ok, cap, target));
        }

        // A sibling folder named like the bundle is not inside it
        if (isRealWriter) {
            const fs::path sibling = sandbox / "Fake.app2" / tag / ("s" + w.ext);
            LogCapture cap;
            const bool ok = w.write(sibling);
            check(w.name + ": a sibling \"Fake.app2\" is not the bundle -> written",
                  notRefused(w, ok, cap, sibling));
        }
    }

    // A user data root inside the bundle: its writes are refused too
    {
        setUserDataPathRoot(bundle / "Contents" / "UserData");
        LogCapture cap;
        const fs::path target = getUserDataPath("inside.txt");
        check("getUserDataPath(inside the bundle): does not create its root",
              !fs::exists(target.parent_path()) && cap.errors.empty());
        const bool ok = saveTextFile(target, "x");
        check("setUserDataPathRoot(inside the bundle): writes still refused",
              !ok && cap.errors.size() == 1 &&
              !fs::exists(target.parent_path()));
    }

    // A refused log destination must keep the existing log open, with the
    // refusal and later messages recorded there.
    {
        const fs::path logPath = userRoot / "active.log";
        check("setLogFile: opens a user data log", setLogFile(logPath));
        LogCapture cap;
        check("setLogFile: bundle refusal keeps the current log open",
              !setLogFile(bundleData / "refused.log") && cap.errors.size() == 1 &&
              getLogger().isFileOpen() && samePath(getLogger().getLogFilePath(), logPath));
        logError("userDataPath") << "after bundle refusal";
        closeLogFile();
        const string text = loadTextFile(logPath);
        check("setLogFile: current log contains the refusal and later messages",
              text.find("getUserDataPath(") != string::npos &&
              text.find("after bundle refusal") != string::npos);
    }

    // Outside a bundle (no bundle set): relative writes into the data folder work
    internal::setAppBundlePathForTests(fs::path());
    setUserDataPathRoot(userRoot);
    {
        const fs::path devData = sandbox / "bin" / "data";
        fs::create_directories(devData);
        setDataPathRoot(devData);
        LogCapture cap;
        check("no bundle: a relative write into the data folder works",
              saveTextFile("dev.txt", "x") && fs::is_regular_file(devData / "dev.txt") &&
              cap.errors.empty());
#ifdef __APPLE__
        auto noticeCount = [&cap] {
            int c = 0;
            for (const auto& m : cap.notices) {
                if (m.find("getUserDataPath()") != string::npos) ++c;
            }
            return c;
        };
        check("macOS: the first relative write into the data folder logs one Notice",
              noticeCount() == 1);
        saveTextFile("dev2.txt", "x");
        check("macOS: ... once per process", noticeCount() == 1);
#else
        check("not macOS / iOS: no Notice for a relative write into the data folder",
              cap.notices.empty());
#endif
    }

    if (audioUp) engine.shutdown();
    fs::remove_all(sandbox, ec);

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
