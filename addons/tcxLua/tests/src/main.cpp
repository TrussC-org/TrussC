// =============================================================================
// tcxLua tests - headless behavioral test (no window).
//
// Built and run by CI via examples/build_all.py --addon-tests-only
// (exit 0 = pass, non-zero = fail). Console only.
//
// Error-contained entry points (tcxLua::call / tcxLua::runFile):
//   - a syntax error, a missing file, a runtime error(), a nil function and
//     string.char(340) each return false; nothing throws
//   - file diagnostics name the script; Unicode paths and empty files work
//   - missing/unreadable files and repeated failing calls log errors
//   - a valid file and a valid call return true, and arguments arrive
//
// Lua Node surface (TC_LUA_SKIP):
//   - Node.callAfterAsync / callEveryAsync / cancelAsyncTimer /
//     cancelAllAsyncTimers are nil
//   - Node().callAfter / callEvery / cancelTimer are still there
// =============================================================================

#include <tcxLua.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-56s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    ok ? ++g_pass : ++g_fail;
}

// Runs `body`, which returns the helper's result. Passes when the result is
// `expected` and nothing was thrown.
template<typename F>
static void checkNoThrow(const char* name, bool expected, F&& body) {
    bool threw = false, result = !expected;
    try {
        result = body();
    } catch (const std::exception& e) {
        threw = true;
        std::printf("  (threw: %s)\n", e.what());
    } catch (...) {
        threw = true;
        std::printf("  (threw a non-std exception)\n");
    }
    check(name, !threw && result == expected);
}

static fs::path writeFile(const fs::path& dir, const string& name, const string& text) {
    fs::path p = dir / name;
    ofstream(p, ios::binary) << text;
    return p;
}

int main() {
    fs::path dir = fs::temp_directory_path() /
        ("tcxLua-tests-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);

    tcxLua tcx;
    auto lua = tcx.getLuaState();
    sol::state& L = *lua;
    vector<string> errors;
    auto logListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
        if (e.level == tc::LogLevel::Error) errors.push_back(e.message);
    });

    // ---- runFile -------------------------------------------------------------
    fs::path okFile = writeFile(dir, "ok.lua",
        "loaded = 1\n"
        "function add(a, b) result = a + b end\n"
        "function fails() error('raised by the test') end\n"
        "function nilIndex() local a = nil; a.x = 1 end\n"
        "function keyPressed(key) return string.char(key) end\n");
    fs::path syntaxFile = writeFile(dir, "syntax.lua", "function draw(\n  x = = 1\n");
    fs::path runtimeFile = writeFile(dir, "runtime.lua", "error('top-level error')\n");
    fs::path unicodeDir = dir / fs::path(u8"日本語");
    fs::create_directories(unicodeDir);
    fs::path unicodeFile = unicodeDir / fs::path(u8"スクリプト.lua");
    ofstream(unicodeFile, ios::binary) << "unicodeLoaded = 1\n";
    fs::path emptyFile = writeFile(dir, "empty.lua", "");

    checkNoThrow("runFile: valid file returns true", true, [&] { return tcxLua::runFile(L, okFile); });
    check("runFile: valid file ran", L["loaded"].get_or(0) == 1);
    errors.clear();
    checkNoThrow("runFile: syntax error returns false", false, [&] { return tcxLua::runFile(L, syntaxFile); });
    check("runFile: syntax error names the file", errors.size() == 1 &&
        errors.back().find("syntax.lua:") != string::npos);
    checkNoThrow("runFile: top-level error() returns false", false, [&] { return tcxLua::runFile(L, runtimeFile); });
    errors.clear();
    checkNoThrow("runFile: missing file returns false", false, [&] { return tcxLua::runFile(L, dir / "missing.lua"); });
    check("runFile: missing file logs an error", errors.size() == 1 &&
        errors.back().find("missing.lua") != string::npos);
    checkNoThrow("runFile: Unicode path returns true", true, [&] { return tcxLua::runFile(L, unicodeFile); });
    check("runFile: Unicode path file ran", L["unicodeLoaded"].get_or(0) == 1);
    checkNoThrow("runFile: empty file returns true", true, [&] { return tcxLua::runFile(L, emptyFile); });
    checkNoThrow("runFile: directory returns false", false, [&] { return tcxLua::runFile(L, dir); });
#ifndef _WIN32
    fs::path unreadableFile = writeFile(dir, "unreadable.lua", "loaded = 2\n");
    fs::permissions(unreadableFile, fs::perms::none);
    errors.clear();
    checkNoThrow("runFile: unreadable file returns false", false, [&] { return tcxLua::runFile(L, unreadableFile); });
    check("runFile: unreadable file logs an error", errors.size() == 1 &&
        errors.back().find("unreadable.lua") != string::npos);
    fs::permissions(unreadableFile, fs::perms::owner_all);
#endif

    // ---- call ----------------------------------------------------------------
    checkNoThrow("call: valid function returns true", true, [&] { return tcxLua::call(L, "add", 2, 3); });
    check("call: arguments arrive", L["result"].get_or(0) == 5);
    checkNoThrow("call: runtime error() returns false", false, [&] { return tcxLua::call(L, "fails"); });
    errors.clear();
    tcxLua::call(L, "fails");
    tcxLua::call(L, "fails");
    check("call: repeated failures each log", errors.size() == 2);
    checkNoThrow("call: index of nil returns false", false, [&] { return tcxLua::call(L, "nilIndex"); });
    checkNoThrow("call: nil function returns false", false, [&] { return tcxLua::call(L, "doesNotExist"); });
    checkNoThrow("call: string.char(340) returns false", false, [&] { return tcxLua::call(L, "keyPressed", 340); });
    checkNoThrow("call: string.char(65) returns true", true, [&] { return tcxLua::call(L, "keyPressed", 65); });
    L["notAFunction"] = 42;
    checkNoThrow("call: non-function value returns false", false, [&] { return tcxLua::call(L, "notAFunction"); });
    check("state still usable after errors", L.safe_script("return 1 + 1", sol::script_pass_on_error).get<int>() == 2);

    // ---- Node async members are not exposed ----------------------------------
    auto luaBool = [&](const char* expr) {
        sol::protected_function_result r = L.safe_script(string("return ") + expr, sol::script_pass_on_error);
        return r.valid() && r.get_type() == sol::type::boolean && r.get<bool>();
    };
    check("Node.callAfterAsync == nil", luaBool("Node.callAfterAsync == nil"));
    check("Node.callEveryAsync == nil", luaBool("Node.callEveryAsync == nil"));
    check("Node.cancelAsyncTimer == nil", luaBool("Node.cancelAsyncTimer == nil"));
    check("Node.cancelAllAsyncTimers == nil", luaBool("Node.cancelAllAsyncTimers == nil"));
    check("Node().callAfter ~= nil", luaBool("Node().callAfter ~= nil"));
    check("Node().callEvery ~= nil", luaBool("Node().callEvery ~= nil"));
    check("Node().cancelTimer ~= nil", luaBool("Node().cancelTimer ~= nil"));

    // Exercise the hand-written XML bindings against the vendored pugixml.
    checkNoThrow("Xml: serialization and parsing through Lua", true, [&] {
        auto result = L.safe_script(R"lua(
            local xml = Xml.new()
            local root = xml:addRoot("root")
            root:append_child("empty"):text():set("")
            root:append_child("item"):append_attribute("value"):set("seven")
            root:child("item"):text():set("text & <markup>")
            local serialized = xml:toString()
            assert(serialized:find("<empty />", 1, true))
            local restored = parseXml(serialized)
            assert(restored:root():child("item"):attribute("value"):value() == "seven")
            assert(restored:root():child("item"):text():get() == "text & <markup>")
            return true
        )lua", sol::script_pass_on_error);
        return result.valid() && result.get<bool>();
    });

    // Exercise the bundled sketch's guard, rather than a copy of it.
    fs::path sketch = fs::path(__FILE__).parent_path().parent_path().parent_path() /
        "exampleFileReload/bin/data/sketch.lua";
    checkNoThrow("bundled sketch: loads", true, [&] { return tcxLua::runFile(L, sketch); });
    checkNoThrow("bundled sketch: Shift is ignored", true, [&] { return tcxLua::call(L, "keyPressed", 340); });
    checkNoThrow("bundled sketch: arrow is ignored", true, [&] { return tcxLua::call(L, "keyPressed", 262); });
    checkNoThrow("bundled sketch: ordinary key works", true, [&] { return tcxLua::call(L, "keyPressed", 65); });

    bool defaultHandlerThrows = false;
    try {
        L.safe_script("error('default handler is unchanged')");
    } catch (const sol::error&) {
        defaultHandlerThrows = true;
    }
    check("sol2 default script error still throws", defaultHandlerThrows);

    std::error_code ec;
    fs::remove_all(dir, ec);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
