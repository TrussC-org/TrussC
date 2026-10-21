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
size_t allocated = 0;
void recordAllocation(size_t bytes) {
    if (failNext || failEveryAllocation) { failNext = false; throw std::bad_alloc(); }
    if (track) { allocated += bytes; if (allocated > 8u*1024u*1024u) throw std::bad_alloc(); }
}
struct Probe {
    lua_Alloc previous;
    void* data;
    size_t largest = 0;
    static void* allocate(void* context, void* ptr, size_t oldSize, size_t newSize) {
        auto& probe = *static_cast<Probe*>(context);
        probe.largest = std::max(probe.largest, newSize);
        if (newSize > 8192) return nullptr;
        return probe.previous(probe.data, ptr, oldSize, newSize);
    }
};
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

    auto writerLarge = [](lua_State*, const void* bytes, size_t size, void* context) -> int {
        auto* f = static_cast<FILE*>(context);
        return std::fwrite(bytes, 1, size, f) != size;
    };
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
        // In-memory and file routes share the same bytecode checks.
        if (!status) {
            FILE* binaryFile = std::fopen("tc-large-module.luac", "wb");
#if LUA_VERSION_NUM < 503 && !defined(lua_dump)
            int dumped = lua_dump(state, writerLarge, binaryFile);
#else
            int dumped = lua_dump(state, writerLarge, binaryFile, 1);
#endif
            std::fclose(binaryFile);
            lua_pop(state, 1);
            status = dumped ? dumped : luaL_loadfile(state, "tc-large-module.luac");
            check(kind ? "large local code bytecode loads" : "large local data bytecode loads", status == 0);
            std::remove("tc-large-module.luac");
        } else {
            // Still exercise the bytecode regression on the old tree: the
            // review's base-generated 8 MiB bytecode is replayed separately.
            check(kind ? "large local code bytecode loads" : "large local data bytecode loads", false);
        }
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
    status = luaL_dostring(state,
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
        "collectgarbage('restart')");
    std::string gcSource;
    for (int i=0; i<20000; ++i) gcSource += "function f" + std::to_string(i) + "(a,b) local c=a*7+b; if c>10 then return c-1 end return c end\n";
    check("finalizer setup", status == 0);
    status = luaL_loadbuffer(state, gcSource.data(), gcSource.size(), "gc-load");
    lua_getglobal(state, "gcRuns"); int runs = static_cast<int>(lua_tointeger(state,-1)); lua_pop(state,1);
    lua_getglobal(state, "gcFailures"); int gcFailures = static_cast<int>(lua_tointeger(state,-1)); lua_pop(state,1);
    std::printf("finalizers observed during load: %d; allocation failures: %d\n", runs, gcFailures);
    check("finalizers during load retain allocator", !status && runs > 0 && gcFailures == 0);
    lua_settop(state,0); lua_gc(state,LUA_GCCOLLECT,0);
    std::string source;
    for (int i = 0; i < 4000; ++i) source += "value = 7\n";
    check("compile generated chunk", luaL_loadbuffer(state, source.data(), source.size(), "bounds") == 0);
    std::vector<char> binary;
    auto writer = [](lua_State*, const void* bytes, size_t size, void* context) -> int {
        auto& out = *static_cast<std::vector<char>*>(context);
        const auto* begin = static_cast<const char*>(bytes);
        out.insert(out.end(), begin, begin + size);
        return 0;
    };
#if LUA_VERSION_NUM < 503 && !defined(lua_dump)
    status = lua_dump(state, writer, &binary);
#else
    status = lua_dump(state, writer, &binary, 0);
#endif
    lua_pop(state, 1);
    check("dump generated bytecode", status == 0 && binary.size() > 8192);
    status = luaL_loadbuffer(state, binary.data(), binary.size(), "bounds");
    check("complete bytecode loads", status == 0);
    lua_pop(state, 1);
    // Observe native GC accounting while the complete binary buffer is live,
    // before undumping can create any prototype objects.
    struct AccountedReader {
        const std::vector<char>& bytes;
        size_t before;
        bool sent = false, accounted = false;
        static size_t memory(lua_State* L) {
            return size_t(lua_gc(L, LUA_GCCOUNT, 0)) * 1024 + lua_gc(L, LUA_GCCOUNTB, 0);
        }
        static const char* read(lua_State* L, void* context, size_t* size) {
            auto& reader = *static_cast<AccountedReader*>(context);
            if (!reader.sent) {
                reader.sent = true;
                *size = reader.bytes.size();
                return reader.bytes.data();
            }
            reader.accounted = memory(L) >= reader.before + reader.bytes.size();
            *size = 0;
            return nullptr;
        }
    } accounting{binary, AccountedReader::memory(state)};
    lua_gc(state, LUA_GCSTOP, 0);
#if LUA_VERSION_NUM < 502 && !defined(lua_load)
    status = lua_load(state, AccountedReader::read, &accounting, "accounting");
#else
    status = lua_load(state, AccountedReader::read, &accounting, "accounting", nullptr);
#endif
    check("binary buffer participates in native GC accounting", !status && accounting.accounted);
    lua_pop(state, 1);
    lua_gc(state, LUA_GCRESTART, 0);
    Probe probe{};
    probe.previous = lua_getallocf(state, &probe.data);
    lua_setallocf(state, Probe::allocate, &probe);
    status = luaL_loadbuffer(state, binary.data(), 64, "bounds");
    lua_setallocf(state, probe.previous, probe.data);
    lua_pop(state, 1);
    check("incomplete bytecode has no large allocation", status != 0 && probe.largest <= 8192);
    // Every incomplete prefix uses the same protected loader and cleans up.
    bool prefixesRejected = true;
    for (size_t prefix = 1; prefix < binary.size(); prefix += 137) {
        status = luaL_loadbuffer(state, binary.data(), prefix, "bounds");
        if (!status) prefixesRejected = false;
        lua_pop(state, 1);
    }
    check("incomplete prefixes rejected", prefixesRejected);
    // Round-trip ordinary nested functions, captured locals, table constants
    // and fractional numbers through the checked binary reader.
    status = luaL_loadstring(state,
        "local outer = 123.25; local values = {1, 2, 3, label='value'}; "
        "return function(x) return outer + values[2] + x end");
    binary.clear();
#if LUA_VERSION_NUM < 503 && !defined(lua_dump)
    if (!status) status = lua_dump(state, writer, &binary);
#else
    if (!status) status = lua_dump(state, writer, &binary, 0);
#endif
    lua_pop(state, 1);
    if (!status) status = luaL_loadbuffer(state, binary.data(), binary.size(), "nested-bounds");
    if (!status) status = lua_pcall(state, 0, 1, 0);
    if (!status) { lua_pushnumber(state, 4.5); status = lua_pcall(state, 1, 1, 0); }
    check("nested bytecode preserves values", !status && lua_tonumber(state, -1) == 129.75);
    lua_settop(state, 0);
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
    failNext = true;
    auto allocationFailure = tcxLuaLintUndefinedGlobals(state, files);
    failNext = false;
    check("lint allocation failure is diagnostic", allocationFailure.size() == 1 && allocationFailure[0].chunk == "long-name");
    std::vector<LuaLintIssue> unavailableDiagnostic;
    bool persistentThrew = false;
    failEveryAllocation = true;
    try { unavailableDiagnostic = tcxLuaLintUndefinedGlobals(state, files); }
    catch (...) { persistentThrew = true; }
    failEveryAllocation = false;
    check("persistent lint/report allocation failure contained", !persistentThrew && unavailableDiagnostic.empty());
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
    status = luaL_loadbuffer(state, nested.data(), nested.size(), "depth");
    binary.clear();
    if (!status) status = lua_dump(state, writer, &binary, 0);
    lua_pop(state, 1);
    if (!status) status = luaL_loadbuffer(state, binary.data(), binary.size(), "depth-roundtrip");
    check("90-level bytecode round trip", status == 0);
    lua_pop(state, 1);
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
