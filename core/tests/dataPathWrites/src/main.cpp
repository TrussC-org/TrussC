// =============================================================================
// core/tests/dataPathWrites — behavioral regression test for the core file
// writers' path rule (#356).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI on
// macOS / Linux / Windows).
//
// Guards the invariants, for each of setLogFile, FileWriter::open,
// saveTextFile, appendToFile, saveJson, Xml::save and Pixels::save:
//   - a relative path resolves against getDataPath(), not the working
//     directory (the CWD is moved elsewhere for the whole run);
//   - a missing parent folder is created;
//   - an absolute path is used as given;
//   - a parent folder that cannot be created (a regular file in the way; on
//     POSIX also a read-only folder) logs an Error and returns false;
//   - UTF-8 folder and file names land on disk by their real names;
//   - a path with no file name ("" or "sub/") logs an Error and returns
//     false without creating any folder.
// And for setLogFile only:
//   - getLogFilePath() is the resolved absolute path;
//   - a failed call (folder or open failure) keeps the current log file:
//     still open, same path, and the error line and later lines land in it
//     (also for a path with no file name).
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

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

static string readText(const fs::path& p) {
    ifstream in(p, ios::binary);
    return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

// Error lines logged while it is alive
struct ErrorCapture {
    vector<string> errors;
    EventListener sub = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level >= LogLevel::Error) errors.push_back(e.message);
    });
};

// One writer under test: write(path) returns what the writer returned.
struct Writer {
    string name;
    string ext;
    function<bool(const fs::path&)> write;
};

} // namespace

TC_CORE_TEST_MAIN() {
    const fs::path sandbox = fs::temp_directory_path() / "tc_dataPathWrites_test";
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    const fs::path data = sandbox / "data";
    const fs::path cwd = sandbox / "cwd";
    fs::create_directories(data);
    fs::create_directories(cwd);
    setDataPathRoot(data);
    // A relative path that went through the CWD would land here instead
    fs::current_path(cwd);

    Pixels px;
    px.allocate(2, 2, 4);
    px.setColor(0, 0, Color(1, 0, 0, 1));

    const vector<Writer> writers = {
        {"saveTextFile", ".txt", [](const fs::path& p) { return saveTextFile(p, "hello"); }},
        {"appendToFile", ".txt", [](const fs::path& p) { return appendToFile(p, "hello"); }},
        {"FileWriter::open", ".txt", [](const fs::path& p) {
            FileWriter w;
            if (!w.open(p)) return false;
            w.write("hello");
            return true;
        }},
        {"FileWriter::open(append)", ".txt", [](const fs::path& p) {
            FileWriter w;
            if (!w.open(p, true)) return false;
            w.write("hello");
            return true;
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
        {"setLogFile", ".log", [](const fs::path& p) {
            const bool ok = setLogFile(p);
            closeLogFile();
            return ok;
        }},
    };

#ifndef _WIN32
    // A folder this process cannot create entries in (root ignores the mode)
    const fs::path readOnly = data / "readonly";
    fs::create_directories(readOnly);
    ::chmod(readOnly.c_str(), 0555);
    const bool canTestReadOnly = ::geteuid() != 0;
#endif

    int n = 0;
    for (const auto& w : writers) {
        const string tag = "w" + to_string(n++);

        // 1. Relative, parent exists: lands under the data folder, not the CWD
        {
            const fs::path rel = fs::path(tag + "_flat" + w.ext);
            const bool ok = w.write(rel);
            check(w.name + ": relative path returns true", ok);
            check(w.name + ": relative path lands under getDataPath()",
                  fs::is_regular_file(data / rel) && !fs::exists(cwd / rel));
        }

        // 2. Relative, parents missing: created
        {
            const fs::path rel = fs::path(tag) / "a" / "b" / ("file" + w.ext);
            check(w.name + ": missing parent folders -> returns true", w.write(rel));
            check(w.name + ": missing parent folders created, file written",
                  fs::is_regular_file(data / rel) && !fs::exists(cwd / tag));
        }

        // 3. Absolute (outside the data folder, parent missing): used as given
        {
            const fs::path abs = sandbox / "abs" / tag / "sub" / ("file" + w.ext);
            check(w.name + ": absolute path returns true", w.write(abs));
            check(w.name + ": absolute path used as given",
                  fs::is_regular_file(abs) && !fs::exists(data / "abs"));
        }

        // 4. A regular file where the parent folder should be: logs, false
        {
            const fs::path blocker = data / (tag + "_blocker");
            { ofstream out(blocker); out << "x"; }
            ErrorCapture cap;
            const bool ok = w.write(fs::path(tag + "_blocker") / "sub" / ("file" + w.ext));
            check(w.name + ": parent is a file -> returns false", !ok);
            check(w.name + ": parent is a file -> logs an Error", !cap.errors.empty());
        }

#ifndef _WIN32
        // 5. Parent folder inside a read-only folder: logs, false
        if (canTestReadOnly) {
            ErrorCapture cap;
            const bool ok = w.write(fs::path("readonly") / tag / ("file" + w.ext));
            check(w.name + ": read-only folder -> returns false", !ok);
            check(w.name + ": read-only folder -> logs an Error", !cap.errors.empty());
            check(w.name + ": read-only folder -> nothing created",
                  !fs::exists(readOnly / tag));
        }
#endif

        // 6. UTF-8 folder and file names (made from u8 literals, so they are
        //    exact whatever the Windows code page)
        {
            const fs::path rel = utf8ToPath(utf8(u8"日本語フォルダ/") + tag +
                                            utf8(u8"/波〜ファイル") + w.ext);
            check(w.name + ": UTF-8 names -> returns true", w.write(rel));
            const fs::path onDisk = data / fs::path(u8"日本語フォルダ") / fs::path(tag) /
                                    fs::path(u8"波〜ファイル" + std::u8string(w.ext.begin(), w.ext.end()));
            check(w.name + ": UTF-8 names -> real names on disk", fs::is_regular_file(onDisk));
        }

        // 7. No file name: "" resolves to the data folder itself, "sub/" to a
        //    folder. Both log and return false before creating anything.
        {
            // "" against a data root that does not exist yet: it stays missing
            const fs::path freshRoot = sandbox / "fresh" / tag;
            setDataPathRoot(freshRoot / "data");
            ErrorCapture cap;
            const bool ok = w.write(fs::path());
            setDataPathRoot(data);
            check(w.name + ": \"\" -> returns false", !ok);
            check(w.name + ": \"\" -> logs an Error",
                  !cap.errors.empty() && cap.errors.back().find("file name in") != string::npos);
            check(w.name + ": \"\" -> nothing created", !fs::exists(freshRoot));
        }
        {
            ErrorCapture cap;
            const bool ok = w.write(fs::path(tag + "_sub/"));
            check(w.name + ": \"sub/\" -> returns false", !ok);
            check(w.name + ": \"sub/\" -> logs an Error",
                  !cap.errors.empty() && cap.errors.back().find("file name in") != string::npos);
            check(w.name + ": \"sub/\" -> nothing created", !fs::exists(data / (tag + "_sub")));
        }
    }

#ifndef _WIN32
    if (!canTestReadOnly) {
        check("read-only folder cases (skipped: running as root)", true);
    }
#endif

    // --- setLogFile specifics ---
    {
        // getLogFilePath() is the resolved absolute path, and lines land in it
        check("setLogFile(relative): returns true", setLogFile("logs/first.log"));
        const fs::path first = data / "logs" / "first.log";
        const string firstPath = getLogger().getLogFilePath();
        check("setLogFile(relative): getLogFilePath() is absolute, under the data folder",
              utf8ToPath(firstPath).is_absolute() && utf8ToPath(firstPath) == first);
        logNotice() << "line one";

        // Folder failure: keeps the current log
        {
            { ofstream out(data / "logfile_blocker"); out << "x"; }
            ErrorCapture cap;
            const bool ok = setLogFile("logfile_blocker/second.log");
            check("setLogFile(folder failure): returns false", !ok);
            check("setLogFile(folder failure): logs an Error",
                  !cap.errors.empty() &&
                  cap.errors.back().find("Failed to open log file") != string::npos);
            check("setLogFile(folder failure): current log still open, same path",
                  getLogger().isFileOpen() && getLogger().getLogFilePath() == firstPath);
        }
        // Open failure (the path is a folder): keeps the current log
        {
            fs::create_directories(data / "a_folder.log");
            ErrorCapture cap;
            const bool ok = setLogFile("a_folder.log");
            check("setLogFile(open failure): returns false", !ok);
            check("setLogFile(open failure): logs an Error",
                  !cap.errors.empty() &&
                  cap.errors.back().find("Failed to open log file") != string::npos);
            check("setLogFile(open failure): current log still open, same path",
                  getLogger().isFileOpen() && getLogger().getLogFilePath() == firstPath);
        }
        // No file name: keeps the current log
        {
            ErrorCapture cap;
            const bool ok = setLogFile("logs_nofile/");
            check("setLogFile(no file name): returns false", !ok);
            check("setLogFile(no file name): logs an Error",
                  !cap.errors.empty() &&
                  cap.errors.back().find("Failed to open log file") != string::npos);
            check("setLogFile(no file name): current log still open, same path, no folder",
                  getLogger().isFileOpen() && getLogger().getLogFilePath() == firstPath &&
                  !fs::exists(data / "logs_nofile"));
        }
        logNotice() << "line two";
        closeLogFile();

        const string text = readText(first);
        check("setLogFile failures: the error lines land in the current log",
              text.find("[ERROR] Failed to open log file") != string::npos);
        check("setLogFile failures: later lines still land in the current log",
              text.find("line one") != string::npos && text.find("line two") != string::npos);

        // A successful switch moves to the new file and stops the old one
        check("setLogFile(switch): returns true", setLogFile("logs/new/third.log"));
        logNotice() << "line three";
        closeLogFile();
        check("setLogFile(switch): new lines go to the new file only",
              readText(data / "logs" / "new" / "third.log").find("line three") != string::npos &&
              readText(first).find("line three") == string::npos);
    }

    fs::current_path(fs::temp_directory_path(), ec);
#ifndef _WIN32
    ::chmod(readOnly.c_str(), 0755);
#endif
    fs::remove_all(sandbox, ec);

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
