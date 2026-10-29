#include "../../../../core/tests/common/tcTestFailure.h"
#include "tcxLuaLint.h"
#ifndef TCXLUA_USE_LUAJIT
#include "lintGlobalLifetime.h"
#endif
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
bool track = false, failNext = false, failEveryAllocation = false;
size_t failNextMinBytes = 0;
size_t allocated = 0, injectedRefusals = 0;
void recordAllocation(size_t bytes) {
    // Persistent faults must allow MSVC Debug's small iterator proxies, whose
    // allocation is inside noexcept empty-container constructors.
    if ((failNext && bytes >= failNextMinBytes) || (failEveryAllocation && bytes >= 64)) {
        ++injectedRefusals; failNext = false; throw std::bad_alloc();
    }
    if (track) { allocated += bytes; if (allocated > 8u*1024u*1024u) throw std::bad_alloc(); }
}

}
void* operator new(size_t bytes) {
    recordAllocation(bytes);
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
// PMR containers use aligned new, so instrument that route as well.
void* operator new(size_t bytes, std::align_val_t alignment) {
    recordAllocation(bytes);
    const size_t align = static_cast<size_t>(alignment);
#ifdef _WIN32
    if (void* p = _aligned_malloc(bytes ? bytes : 1, align)) return p;
#else
    if (void* p = std::aligned_alloc(align, ((bytes ? bytes : 1) + align - 1) / align * align)) return p;
#endif
    throw std::bad_alloc();
}
void* operator new[](size_t bytes, std::align_val_t align) { return ::operator new(bytes, align); }
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t align) noexcept { ::operator delete(p, align); }
void operator delete(void* p, size_t, std::align_val_t align) noexcept { ::operator delete(p, align); }
void operator delete[](void* p, size_t, std::align_val_t align) noexcept { ::operator delete(p, align); }
#ifndef TCXLUA_ENGINE_TEST_ENTRY
#define TCXLUA_ENGINE_TEST_ENTRY main
#endif
int TCXLUA_ENGINE_TEST_ENTRY() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) { std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL"); failures += !ok; };
    lua_State* state = luaL_newstate();
    if (!state) return 1;
    // A reader supplies a large source without retaining the full input.
    struct Source {
        std::string part;
        int count;
        static const char* read(lua_State*, void* data, size_t* size) {
            auto& source = *static_cast<Source*>(data);
            if (!source.count--) { *size = 0; return nullptr; }
            *size = source.part.size();
            return source.part.data();
        }
    } comments{"--" + std::string(65534, 'x') + "\n", 1296};
#if LUA_VERSION_NUM < 502 && !defined(lua_load)
    int status = lua_load(state, Source::read, &comments, "large-comment");
#else
    int status = lua_load(state, Source::read, &comments, "large-comment", nullptr);
#endif
    check("large streamed source has no input cap", status == 0);
    lua_pop(state, 1);
    luaL_openlibs(state);
    Source textMode{std::string("\x1b") + std::string(1023, 'x'), 8};
#if LUA_VERSION_NUM < 502
    status = lua_loadx(state, Source::read, &textMode, "text-mode", "t");
#else
    status = lua_load(state, Source::read, &textMode, "text-mode", "t");
#endif
    check("text mode rejects binary on first reader call", status == LUA_ERRSYNTAX && textMode.count == 7);
    lua_pop(state, 1);

    status = luaL_loadstring(state, "return 42");
    std::string binary;
    auto writer = [](lua_State*, const void* bytes, size_t size, void* data) -> int {
        try { static_cast<std::string*>(data)->append(static_cast<const char*>(bytes), size); }
        catch (...) { return 1; }
        return 0;
    };
#if LUA_VERSION_NUM < 503 && !defined(lua_dump)
    if (!status) status = lua_dump(state, writer, &binary);
#else
    if (!status) status = lua_dump(state, writer, &binary, 0);
#endif
    check("generate valid binary lint input", status == 0 && !binary.empty());
    lua_pop(state, 1);
    auto textIssues = tcxLuaLintUndefinedGlobals(state, {{"text", "return 42"}});
    check("lint accepts text", textIssues.empty());
    auto binaryIssues = tcxLuaLintUndefinedGlobals(state, {{"dumped", binary}});
    check("lint rejects valid binary with clear error", binaryIssues.size() == 1 &&
        binaryIssues[0].chunk == "dumped" && binaryIssues[0].name.find("binary") != std::string::npos &&
        (binaryIssues[0].name.find("text") != std::string::npos || binaryIssues[0].name.find("'t'") != std::string::npos));

    // Review's 8 MiB chunked data and 11 MiB code modules, generated locally.
    for (int kind = 0; kind < 2; ++kind) {
        std::string module = "local d = {}\n";
        int row = 0;
        const size_t target = (kind ? 11u : 8u)*1024u*1024u;
        while (module.size() < target) {
            if (!kind) {
                module += "d[#d+1] = (function() return {\n";
                for (int r = 0; r < 1000; ++r, ++row)
                    module += "{" + std::to_string(row) + "," + std::to_string(row*3) + ".5,\"k" + std::to_string(row) + "\"},\n";
                module += "} end)()\n";
            } else {
#if LUA_VERSION_NUM < 502
                if (row % 500 == 0) module += "(function()\n";
#endif
                module += "function d.f" + std::to_string(row) + "(a,b) local c = a*" + std::to_string(row) + " + b; if c > 10 then return c - 1 end return c end\n";
                ++row;
#if LUA_VERSION_NUM < 502
                if (row % 500 == 0) module += "end)();\n";
#endif
            }
        }
#if LUA_VERSION_NUM < 502
        if (kind && row % 500) module += "end)();\n";
#endif
        module += "return d\n";
        const char* path = kind ? "tc-code-module.lua" : "tc-data-module.lua";
        FILE* file = std::fopen(path, "wb");
        if (!file) return 1;
        std::fwrite(module.data(), 1, module.size(), file); std::fclose(file);
        status = luaL_loadfile(state, path);
        if (status) std::printf("module error: %s\n", lua_tostring(state,-1));
        check(kind ? "large local code module loads" : "large local data module loads", status == 0);
        lua_pop(state, 1);
        std::remove(path);
        lua_gc(state, LUA_GCCOLLECT, 0);
    }
    {
        FILE* file = std::fopen("tc-reader.lua", "wb");
        std::string comment = "--" + std::string(8190, 'x') + "\n";
        for (int i = 0; i < 5120; ++i) std::fwrite(comment.data(), 1, comment.size(), file);
        std::fclose(file);
        status = luaL_dostring(state, "assert(load(io.lines('tc-reader.lua', 8192)))");
        check("load io.lines 8192 has no cumulative cap", status == 0);
        lua_settop(state, 0); std::remove("tc-reader.lua");
    }
    const char* finalizerSource =
        "gcRuns=0; gcFailures=0; collectgarbage('stop'); "
#if LUA_VERSION_NUM >= 502
        "for i=1,2000 do setmetatable({}, {__gc=function() gcRuns=gcRuns+1; "
#else
        "for i=1,2000 do local p=newproxy(true); getmetatable(p).__gc=function() gcRuns=gcRuns+1; "
#endif
        "local ok=pcall(string.rep,'x',64*1024); if not ok then gcFailures=gcFailures+1 end "
#if LUA_VERSION_NUM >= 502
        "end}) end; "
#else
        "end end; "
#endif
        "collectgarbage('restart')";
    status = luaL_dostring(state, finalizerSource);
    std::string gcSource;
    for (int i=0; i<20000; ++i) gcSource += "function f" + std::to_string(i) + "(a,b) local c=a*7+b; if c>10 then return c-1 end return c end\n";
    check("finalizer setup", status == 0);
    status = luaL_loadbuffer(state, gcSource.data(), gcSource.size(), "gc-load");
    lua_getglobal(state, "gcRuns"); int runs = static_cast<int>(lua_tointeger(state,-1)); lua_pop(state,1);
    lua_getglobal(state, "gcFailures"); int gcFailures = static_cast<int>(lua_tointeger(state,-1)); lua_pop(state,1);
    std::printf("finalizers observed during load: %d; allocation failures: %d\n", runs, gcFailures);
    check("finalizers during load retain allocator", !status && runs > 0 && gcFailures == 0);
    lua_settop(state,0); lua_gc(state,LUA_GCCOLLECT,0);
#if LUA_VERSION_NUM < 502
    auto mixedJit = tcxLuaLintUndefinedGlobals(state, {
        {"valid-before", "return 1"}, {"stale", std::string("\x1bLua", 4)},
        {"valid-after", "return 2"}});
    check("lint binary failure is per file", mixedJit.size() == 1 && mixedJit[0].chunk == "stale" && mixedJit[0].line == 0);
#endif
#if LUA_VERSION_NUM >= 502
    std::string suggestionSource = "local t={}\n";
    for (int i=0; i<5000; ++i) { lua_pushinteger(state,i); lua_setglobal(state,("bindingFn"+std::to_string(10000+i)).c_str()); }
    for (int i=0; i<200; ++i) suggestionSource += "t.v=dynamicName"+std::to_string(10000+i)+"\n";
    auto manyIssues = tcxLuaLintUndefinedGlobals(state, {{"suggestions",suggestionSource}});
    check("suggestion exhaustion retains every diagnostic", manyIssues.size()==200 && manyIssues.front().chunk=="suggestions");
    std::string lintModule="local M={}\n";
    for(int i=0;i<20000;++i) lintModule += "function M.f"+std::to_string(i)+"(a,b) local c=a*7+b; if c>10 then return c-1 end return c end\n";
    lintModule += "return M";
    auto multiLint=tcxLuaLintUndefinedGlobals(state, {{"one",lintModule},{"two",lintModule},{"three",lintModule}});
    check("lint modules have independent compilation", multiLint.empty());
    const std::string identifier(40000, 'x');
    std::vector<std::pair<std::string, std::string>> files{{"long-name", "local " + identifier + " = 1; return missingName"}};
    track = true; allocated = 0;
    std::vector<LuaLintIssue> issues;
    bool threw = false;
    try { issues = tcxLuaLintUndefinedGlobals(state, files); } catch (...) { threw = true; }
    track = false;
    check("long identifier suggestion uses linear storage", !threw && allocated < 1024u*1024u &&
          issues.size() == 1 && issues[0].name == "missingName" && issues[0].suggestion.empty());
    // Lua allocates through its native allocator, independently of global new.
    // Arm only once compilation starts, after the list sentinel exists. Skip
    // small allocations so FileScan's Debug iterator proxies finish construction;
    // repeated reads then force a recoverable reads.emplace_back growth >1 KiB.
    std::string faultSource;
    for (int i = 0; i < 128; ++i) faultSource += "local value = missingName\n";
    const std::vector<std::pair<std::string, std::string>> faultFiles{{"scan-storage", faultSource}};
    struct ArmLintFailure {
        lua_Alloc previous;
        void* data;
        bool armed = false;
        static void* allocate(void* context, void* ptr, size_t oldSize, size_t size) noexcept {
            auto& hook = *static_cast<ArmLintFailure*>(context);
            if (size && !hook.armed) { hook.armed = true; failNext = true; }
            return hook.previous(hook.data, ptr, oldSize, size);
        }
    };
    check("prepare lint fault stack", lua_checkstack(state, 64) != 0);
    ArmLintFailure hook{};
    hook.previous = lua_getallocf(state, &hook.data);
    injectedRefusals = 0;
    failNextMinBytes = 1024;
    lua_setallocf(state, ArmLintFailure::allocate, &hook);
    auto allocationFailure = tcxLuaLintUndefinedGlobals(state, faultFiles);
    lua_setallocf(state, hook.previous, hook.data);
    failNext = false;
    failNextMinBytes = 0;
    check("lint allocation failure is diagnostic", hook.armed && injectedRefusals > 0 &&
          allocationFailure.size() == 1 && allocationFailure[0].chunk == "scan-storage" &&
          allocationFailure[0].line == 0 && allocationFailure[0].name.find("lint check failed: ") == 0);
    std::vector<LuaLintIssue> unavailableDiagnostic;
    bool persistentThrew = false;
    injectedRefusals = 0;
    failEveryAllocation = true;
    try { unavailableDiagnostic = tcxLuaLintUndefinedGlobals(state, files); }
    catch (...) { persistentThrew = true; }
    failEveryAllocation = false;
    check("persistent lint/report allocation failure contained", injectedRefusals > 0 && !persistentThrew && unavailableDiagnostic.empty());
    auto typo = tcxLuaLintUndefinedGlobals(state, {{"typo", "local thing = 1; return thign"}});
    check("transposition suggestion", typo.size() == 1 && typo[0].suggestion == "thing");
    const std::pair<const char*, const char*> suggestionCases[] = {
        {"thing", "thingx"}, {"thing", "thig"}, {"Thing", "thing"},
        {"abcd", "abcf"}, {"abcd", "acbd"}, {"ab", "ba"}
    };
    bool suggestionsPreserved = true;
    for (const auto& item : suggestionCases) {
        auto result = tcxLuaLintUndefinedGlobals(state, {{"suggestion", std::string("local ") + item.first + " = 1; return " + item.second}});
        suggestionsPreserved = suggestionsPreserved && result.size() == 1 && result[0].suggestion == item.first;
    }
    check("insertion/deletion/case/transposition suggestions", suggestionsPreserved);
    std::string nested;
    for (int i = 0; i < 90; ++i) nested += "return function() ";
    nested += "return missingName ";
    for (int i = 0; i < 90; ++i) nested += "end ";
    auto depth = tcxLuaLintUndefinedGlobals(state, {{"depth", nested}});
    check("parser depth accepted by lint", depth.size() == 1 && depth[0].chunk == "depth" && depth[0].name == "missingName");
    // A failed binary file must not hide diagnostics in adjacent source files.
    auto mixed = tcxLuaLintUndefinedGlobals(state, {
        {"before", "return absentBefore"}, {"stale", std::string("\x1bLua", 4)},
        {"after", "return absentAfter"}});
    check("lint binary failure is per file", mixed.size() == 3 &&
        std::count_if(mixed.begin(), mixed.end(), [](const auto& i) { return i.chunk == "before" && i.name == "absentBefore"; }) == 1 &&
        std::count_if(mixed.begin(), mixed.end(), [](const auto& i) { return i.chunk == "after" && i.name == "absentAfter"; }) == 1 &&
        std::count_if(mixed.begin(), mixed.end(), [](const auto& i) { return i.chunk == "stale" && i.line == 0; }) == 1);
    check("global candidate survives lookup and GC", lintGlobalLifetime());
    luaopen_base(state);
    lua_pop(state, 1);
    int metatableStatus = luaL_dostring(state,
        "setmetatable(_G, {__index=function(_, key) "
        "if key == 'virtualName' then return 1 end "
        "if key == 'lookupError' then error('lookup failed') end end})");
    check("set up virtual globals", metatableStatus == 0);
    auto virtualGlobal = tcxLuaLintUndefinedGlobals(state, {{"virtual", "return virtualName"}});
    check("global lookup metatable behavior preserved", virtualGlobal.empty());
    auto lookupFailure = tcxLuaLintUndefinedGlobals(state, {{"lookup", "return lookupError"}});
    check("global lookup error is diagnostic", lookupFailure.size() == 1 && lookupFailure[0].chunk == "lookup");
    auto lookupMixed = tcxLuaLintUndefinedGlobals(state, {
        {"before-lookup", "return stillMissing"}, {"lookup", "return lookupError"},
        {"after-lookup", "return alsoMissing"}});
    check("lint lookup failure preserves other files", lookupMixed.size() == 3 &&
        std::count_if(lookupMixed.begin(), lookupMixed.end(), [](const auto& i) { return i.chunk == "before-lookup" && i.name == "stillMissing"; }) == 1 &&
        std::count_if(lookupMixed.begin(), lookupMixed.end(), [](const auto& i) { return i.chunk == "after-lookup" && i.name == "alsoMissing"; }) == 1);

#endif
    check("stack restored after lint", lua_gettop(state) == 0);
    lua_close(state);
    return failures ? 1 : 0;
}
