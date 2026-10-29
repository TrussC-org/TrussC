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
#define TCXLUA_ENGINE_TEST_ENTRY runLuaEngineTests
#include "../engine/main.cpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
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

static bool preCompileAllocationFailure(const fs::path& dir, bool solPanic) {
    struct Memory {
        bool deny = false;
        bool solPanic = false;
        size_t refused = 0, panics = 0;
        static void* allocate(void* context, void* ptr, size_t oldSize, size_t size) noexcept {
            auto& memory = *static_cast<Memory*>(context);
            if (!size) { std::free(ptr); return nullptr; }
            if (memory.deny && (!ptr || size > oldSize)) { ++memory.refused; return nullptr; }
            return std::realloc(ptr, size);
        }
        static int panic(lua_State* state) {
            void* context = nullptr;
            lua_getallocf(state, &context);
            auto& memory = *static_cast<Memory*>(context);
            ++memory.panics;
            if (memory.solPanic) return sol::default_at_panic(state);
            std::fputs("FAIL: panic during pre-compile allocation regression\n", stderr);
            std::_Exit(87);
        }
    } memory;
    memory.solPanic = solPanic;
    const auto path = writeFile(dir, "pre-compile.lua", "");
    sol::state lua(sol::default_at_panic, Memory::allocate, &memory);
    lua_State* raw = lua.lua_state();
    lua_atpanic(raw, Memory::panic);
    if (!lua_checkstack(raw, 64)) return false;
    lua_gc(raw, LUA_GCCOLLECT, 0);
    // Preserve several different values, including values below the top. A
    // restored height alone cannot detect a panic that overwrote caller slots.
    const int base = lua_gettop(raw);
    lua_pushboolean(raw, 1);
    lua_pushinteger(raw, 973);
    lua_pushliteral(raw, "caller sentinel");
    lua_newtable(raw);
    const void* table = lua_topointer(raw, -1);
    const int top = lua_gettop(raw);
    bool result = true, threw = false;
    // State setup, stack reservation and every C++ object above predate the
    // fault. Refuse native Lua growth before even constructing the handler.
    memory.deny = true;
    try { result = tcxLua::runFile(lua, path); } catch (...) { threw = true; }
    memory.deny = false;
    return memory.refused > 0 && memory.panics == 0 && !result && !threw && lua_gettop(raw) == top &&
        lua_type(raw, base + 1) == LUA_TBOOLEAN && lua_toboolean(raw, base + 1) &&
        lua_type(raw, base + 2) == LUA_TNUMBER && lua_tointeger(raw, base + 2) == 973 &&
        lua_type(raw, base + 3) == LUA_TSTRING &&
        string_view(lua_tostring(raw, base + 3)) == "caller sentinel" &&
        lua_type(raw, base + 4) == LUA_TTABLE && lua_topointer(raw, base + 4) == table;
}

// The large-load Notice is delivered after compilation and before execution.
// Use that existing boundary to refuse native Lua growth, not C++ allocations.
static bool postCompileAllocationFailure(const fs::path& dir) {
    struct Memory {
        bool deny = false;
        size_t refused = 0;
        static void* allocate(void* context, void* ptr, size_t oldSize, size_t size) noexcept {
            auto& memory = *static_cast<Memory*>(context);
            if (!size) { std::free(ptr); return nullptr; }
            if (memory.deny && (!ptr || size > oldSize)) { ++memory.refused; return nullptr; }
            return std::realloc(ptr, size);
        }
    } memory;
    sol::state lua(sol::default_at_panic, Memory::allocate, &memory);
    lua_State* raw = lua.lua_state();
    int ref;
    do { lua_pushboolean(raw, 1); ref = luaL_ref(raw, LUA_REGISTRYINDEX); } while (ref < 1024);
    luaL_unref(raw, LUA_REGISTRYINDEX, 1024);
    const auto path = dir / "post-compile.lua";
    {
        ofstream file(path, ios::binary);
        const string comment = "--" + string(8190, 'x') + "\n";
        for (int i = 0; i < 32768; ++i) file << comment;
        file << "return {}\n";
        if (!file) return false;
    }
    const int top = lua_gettop(raw);
    int compiled = 0, errors = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Notice &&
            event.message.find("Loading a large local script") != string::npos &&
            // The protected setup frame contains its context, handler and chunk.
            lua_gettop(raw) == 3 && lua_isfunction(raw, -2) && lua_isfunction(raw, -1)) {
            ++compiled;
            memory.deny = true;
        }
        if (memory.deny && event.level == tc::LogLevel::Error) ++errors;
    });
    bool result = true, threw = false;
    try { result = tcxLua::runFile(lua, path); } catch (...) { threw = true; }
    memory.deny = false;
    fs::remove(path);
    return compiled == 1 && memory.refused > 0 && errors == 1 &&
        !result && !threw && lua_gettop(raw) == top;
}

int main(int argc, char** argv) {
    fs::path dir = fs::temp_directory_path() /
        ("tcxLua-tests-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    check("pre-compile allocation failure: custom panic", preCompileAllocationFailure(dir, false));
    check("pre-compile allocation failure: sol panic", preCompileAllocationFailure(dir, true));
    if (argc > 1 && string(argv[1]) == "--pre-compile-allocation") {
        fs::remove_all(dir);
        return g_fail ? 1 : 0;
    }
    check("post-compile Lua allocation failure contained", postCompileAllocationFailure(dir));
    if (argc > 1 && string(argv[1]) == "--post-compile-allocation") {
        fs::remove_all(dir);
        return g_fail ? 1 : 0;
    }
    check("engine load/lint regressions", runLuaEngineTests() == 0);

    tcxLua tcx;
    tcxLua::LuaModulePreferences preferences;
    preferences.package = true;
    auto lua = tcx.getLuaState(preferences);
    sol::state& L = *lua;
    vector<string> errors;
    auto logListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
        if (e.level == tc::LogLevel::Error) errors.push_back(e.message);
    });

    // Local modules have no input size cap; allocation errors remain protected.
    const auto largeComment = dir / "large-comment.lua";
    {
        ofstream file(largeComment, ios::binary);
        const string comment = "--" + string(65534, 'c') + "\n";
        for (int i = 0; i < 144; ++i) file << comment;
        file << "largeCommentLoaded = 1\n";
    }
    checkNoThrow("local source above 8 MiB loads completely", true, [&] { return tcxLua::runFile(L, largeComment); });
    check("large source ran", L["largeCommentLoaded"].get_or(0) == 1);
    const int beforeFailedLoad = lua_gettop(L.lua_state());
    bool persistentResult = true, persistentThrew = false;
    injectedRefusals = 0;
    failEveryAllocation = true;
    try { persistentResult = tcxLua::runFile(L, largeComment); } catch (...) { persistentThrew = true; }
    failEveryAllocation = false;
    check("persistent load/report allocation failure contained", injectedRefusals > 0 && !persistentResult && !persistentThrew &&
          lua_gettop(L.lua_state()) == beforeFailedLoad);
    const auto largeLiteral = dir / "large-literal.lua";
    {
        ofstream file(largeLiteral, ios::binary);
        file << "return [[";
        const string piece(65536, 's');
        for (int i = 0; i < 1280; ++i) file << piece;
        file << "]]";
    }
    checkNoThrow("large local literal loads", true, [&] { return tcxLua::runFile(L, largeLiteral); });
    // Lua's other loading routes use the engine's same protected loader.
    const auto literalPath = tc::pathToUtf8(largeLiteral);
    L["loadTestPath"] = literalPath;
    auto moduleLoad = L.safe_script("local f = loadfile(loadTestPath); assert(f)", sol::script_pass_on_error);
    check("dependency file load accepts large local literal", moduleLoad.valid());

    string bytecodeSource;
    for (int i = 0; i < 4000; ++i) bytecodeSource += "binaryLoaded = 7\n";
    lua_State* raw = L.lua_state();
    int compileStatus = luaL_loadbuffer(raw, bytecodeSource.data(), bytecodeSource.size(), "bounds");
    check("compile generated chunk", compileStatus == 0);
    vector<char> bytecode;
    auto writer = [](lua_State*, const void* bytes, size_t size, void* data) noexcept -> int {
        try {
            auto& output = *static_cast<vector<char>*>(data);
            const auto* begin = static_cast<const char*>(bytes);
            output.insert(output.end(), begin, begin + size);
            return 0;
        } catch (...) { return 1; }
    };
#if LUA_VERSION_NUM < 503 && !defined(lua_dump)
    int dumpStatus = lua_dump(raw, writer, &bytecode);
#else
    int dumpStatus = lua_dump(raw, writer, &bytecode, 0);
#endif
    lua_pop(raw, 1);
    check("dump generated chunk", dumpStatus == 0 && bytecode.size() > 8192);
    const auto binaryFile = dir / "generated.luac";
    { ofstream file(binaryFile, ios::binary); file.write(bytecode.data(), bytecode.size()); }
    errors.clear();
    checkNoThrow("generated bytecode rejected", false, [&] { return tcxLua::runFile(L, binaryFile); });
    check("bytecode did not run", L["binaryLoaded"].get_or(0) == 0);
    check("binary rejection logged clearly", !errors.empty() && errors.back().find("binary") != string::npos);
    check("binary rejection restores stack", lua_gettop(raw) == beforeFailedLoad);
    L["binaryChunk"] = string(bytecode.begin(), bytecode.end());
    L["binaryPath"] = tc::pathToUtf8(binaryFile);
    auto standardLoaders = L.safe_script(
        "assert(load(binaryChunk, 'dump', 'b'))(); "
        "assert(binaryLoaded == 7); binaryLoaded = nil; "
        "assert(loadfile(binaryPath, 'b'))(); "
        "assert(binaryLoaded == 7); binaryLoaded = nil; "
        "dofile(binaryPath); assert(binaryLoaded == 7); binaryLoaded = nil; "
        "local previousPath = package.path; package.path = binaryPath; "
        "require('generated'); assert(binaryLoaded == 7); binaryLoaded = nil; "
        "package.path = previousPath; "
        "if loadstring then assert(loadstring(binaryChunk))(); binaryLoaded = nil end",
        sol::script_pass_on_error, "standard-loaders", sol::load_mode::text);
    check("getLuaState preserves standard binary loaders", standardLoaders.valid());

    // Startup and hot reload use the same runFile entry point and path.
    const auto sketchFile = writeFile(dir, "reload.lua", "reloadValue = 1");
    checkNoThrow("initial sketch text loads", true, [&] { return tcxLua::runFile(L, sketchFile); });
    check("initial sketch ran", L["reloadValue"].get_or(0) == 1);
    { ofstream file(sketchFile, ios::binary); file.write(bytecode.data(), bytecode.size()); }
    errors.clear();
    checkNoThrow("reload rejects binary replacement", false, [&] { return tcxLua::runFile(L, sketchFile); });
    check("reload rejection names binary and text mode", errors.size() == 1 &&
        errors.back().find("binary") != string::npos &&
        (errors.back().find("text") != string::npos || errors.back().find("'t'") != string::npos));
    { ofstream file(sketchFile); file << "reloadValue = 2"; }
    checkNoThrow("reload accepts restored text", true, [&] { return tcxLua::runFile(L, sketchFile); });
    check("restored sketch ran", L["reloadValue"].get_or(0) == 2);
    // ---- runFile -------------------------------------------------------------
    fs::path okFile = writeFile(dir, "ok.lua",
        "loaded = 1\n"
        "function add(a, b) result = a + b end\n"
        "function fails() error('raised by the test') end\n"
        "function nilIndex() local a = nil; a.x = 1 end\n"
        "function keyPressed(key) return string.char(key) end\n");
    fs::path syntaxFile = writeFile(dir, "syntax.lua", "function draw(\n  x = = 1\n");
    fs::path runtimeFile = writeFile(dir, "runtime.lua", "error('top-level error')\n");
    fs::path tracebackFile = writeFile(dir, "startup.lua",
        "local function initialize() error('startup failed', 0) end;\ninitialize()\n");
    fs::path numericErrorFile = writeFile(dir, "numeric-error.lua", "error(42)\n");
    fs::path returnsFile = writeFile(dir, "returns.lua", "return 1, 2, 3\n");
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
    checkNoThrow("runFile: startup error returns false", false, [&] { return tcxLua::runFile(L, tracebackFile); });
    check("runFile: startup traceback names file and both frames", errors.size() == 1 &&
        errors.back().find("startup failed") != string::npos &&
        errors.back().find("stack traceback:") != string::npos &&
        errors.back().find("startup.lua:1:") != string::npos &&
        errors.back().find("initialize") != string::npos &&
        errors.back().find("startup.lua:2:") != string::npos &&
        errors.back().find("main chunk") != string::npos);
    lua_pushboolean(raw, 1);
    const int sentinelTop = lua_gettop(raw);
    checkNoThrow("runFile: numeric error returns false", false, [&] { return tcxLua::runFile(L, numericErrorFile); });
    check("runFile: error preserves caller stack", lua_gettop(raw) == sentinelTop && lua_toboolean(raw, -1));
    checkNoThrow("runFile: returned values are discarded", true, [&] { return tcxLua::runFile(L, returnsFile); });
    check("runFile: success preserves caller stack", lua_gettop(raw) == sentinelTop && lua_toboolean(raw, -1));
    lua_pop(raw, 1);
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

#if defined(__linux__) && !defined(__ANDROID__)
    check("window position funcs absent on Linux",
        luaBool("getWindowPosition == nil and setWindowPosition == nil"));
#endif

    // A nested result uses its declaration's registration, including inherited returns.
    checkNoThrow("NodeHitResult: findHitNode exposes hit and distance", true, [&] {
        auto result = L.safe_script(R"lua(
            assert(NodeHitResult ~= nil)
            assert(AppHitResult == nil and RectNodeHitResult == nil)
            assert(RectNodeButtonHitResult == nil and ScrollBarHitResult == nil)
            assert(ScrollContainerHitResult == nil)
            local hit = Node():findHitNode(Ray(Vec3(0, 0, 10), Vec3(0, 0, -1)))
            assert(not hit:hit() and hit.distance == 0)
            local inheritedHit = RectNode.new():findHitNode(Ray(Vec3(0, 0, 10), Vec3(0, 0, -1)))
            assert(type(inheritedHit:hit()) == "boolean")
            assert(type(inheritedHit.distance) == "number")
            return true
        )lua", sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error error = result;
            std::cerr << "NodeHitResult script: " << error.what() << std::endl;
            return false;
        }
        return result.get<bool>();
    });

    // #778/#808: inherited bindings survive regeneration without video hardware.
    checkNoThrow("VideoPlayer: inherited playback bindings", true, [&] {
        auto result = L.safe_script(R"lua(
            local player = VideoPlayer.new()
            assert(not player:isLoaded() and not player:isPlaying())
            player:stop()
            player:setPaused(true)
            player:setPosition(0.5)
            player:setVolume(0.25)
            player:setSpeed(0.5)
            player:setLoop(true)
            assert(player:getVolume() == 0.25)
            assert(player:getSpeed() == 0.5 and player:isLoop())
            assert(not player:isFrameNew() and not player:isReady())
            assert(VideoPlayerBase ~= nil)
            assert(not pcall(function() return VideoPlayerBase.new() end))
            return true
        )lua", sol::script_pass_on_error);
        return result.valid() && result.get<bool>();
    });

    // #236: the generated degree companions are callable from Lua.
    checkNoThrow("angles: radians and Deg Lua bindings", true, [&] {
        auto result = L.safe_script(R"lua(
            assert(type(setupScreenPerspectiveDeg) == "function")
            assert(type(setupScreenFovDeg) == "function")
            assert(type(setupScreenFovWithSize) == "function")
            assert(type(setupScreenFovWithSizeDeg) == "function")
            setDefaultScreenFovDeg(60)
            assert(math.abs(getDefaultScreenFov() - TAU / 6) < 0.0001)
            setDefaultScreenFov(0)
            assert(getDefaultScreenFov() == 0)
            setDefaultScreenFovDeg(30)
            setDefaultScreenFovDeg(0)
            assert(getDefaultScreenFov() == 0 and getDefaultScreenFovDeg() == 0)
            setDefaultScreenFov(TAU / 8)
            setDefaultScreenOrtho()
            assert(getDefaultScreenFov() == 0)
            setDefaultScreenFov(TAU / 8)
            assert(math.abs(getDefaultScreenFovDeg() - 45) < 0.0001)
            -- Invalid calls are safe without a GPU and leave state unchanged.
            setupScreenPerspective(60)
            setupScreenFovDeg(180)
            setupScreenFovWithSize(60, 64, 64)
            setupScreenFovWithSizeDeg(180, 64, 64)
            local r, d = Path(), Path()
            r:arc(Vec3(0, 0, 0), 20, 10, 0, TAU / 4, true, 32)
            d:arcDeg(Vec3(0, 0, 0), 20, 10, 0, 90, true, 32)
            assert(r:size() == d:size())
            for i = 0, r:size() - 1 do
                assert(math.abs(r[i].x - d[i].x) < 0.0001)
                assert(math.abs(r[i].y - d[i].y) < 0.0001)
            end
            return true
        )lua", sol::script_pass_on_error);
        return result.valid() && result.get<bool>();
    });

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
