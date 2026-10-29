#ifdef TCXLUA_USE_LUAJIT
// Match the addon's include order, including sol2's Lua 5.1 API shims.
#include "sol/sol.hpp"
#endif
#include "tcxLuaTextOnly.h"
#include "tcxLuaLint.h"
extern "C" {
#include "lualib.h"
}
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
// Remember identities in native memory only; no Lua table/registry references
// keep the original functions alive. GC is paused until the audit finishes so
// their addresses cannot be reused for a replacement closure.
bool reachesOriginalLoader(lua_State* state, int index,
                           const std::vector<const void*>& originals,
                           std::unordered_set<const void*>& seen) {
    if (index < 0 && index > LUA_REGISTRYINDEX) index = lua_gettop(state) + index + 1;
    const int type = lua_type(state, index);
    if (type != LUA_TTABLE && type != LUA_TFUNCTION) return false;
    const void* identity = lua_topointer(state, index);
    for (const void* original : originals)
        if (identity == original) return true;
    if (!seen.insert(identity).second) return false;
    if (!lua_checkstack(state, 4)) return true;
    if (type == LUA_TTABLE) {
        lua_pushnil(state);
        while (lua_next(state, index)) {
            const bool found = reachesOriginalLoader(state, -2, originals, seen) ||
                               reachesOriginalLoader(state, -1, originals, seen);
            lua_pop(state, 1);
            if (found) { lua_pop(state, 1); return true; }
        }
    } else {
        for (int upvalue = 1; lua_getupvalue(state, index, upvalue); ++upvalue) {
            const bool found = reachesOriginalLoader(state, -1, originals, seen);
            lua_pop(state, 1);
            if (found) return true;
        }
    }
    return false;
}
}

#ifndef TCXLUA_TEXT_TEST_ENTRY
#define TCXLUA_TEXT_TEST_ENTRY main
#endif
int TCXLUA_TEXT_TEST_ENTRY() {
    lua_State* state = luaL_newstate();
    if (!state) return 1;
    luaL_openlibs(state);
    lua_gc(state, LUA_GCSTOP, 0);
    std::vector<const void*> originals;
    for (const char* name : {"load", "loadstring", "loadfile", "dofile"}) {
        lua_getglobal(state, name);
        if (lua_isfunction(state, -1)) originals.push_back(lua_topointer(state, -1));
        lua_pop(state, 1);
    }
    lua_getglobal(state, "package");
    for (const char* name : {"loaders", "searchers"}) {
        lua_getfield(state, -1, name);
        if (lua_istable(state, -1)) {
            lua_rawgeti(state, -1, 2);
            originals.push_back(lua_topointer(state, -1));
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    lua_pushcfunction(state, tcxLuaInternal::installTextOnlyLoaders);
    if (lua_pcall(state, 0, 0, 0)) return 1;
    std::unordered_set<const void*> seen;
    lua_getglobal(state, "_G");
    bool reachable = reachesOriginalLoader(state, -1, originals, seen);
    lua_pop(state, 1);
    reachable = reachesOriginalLoader(state, LUA_REGISTRYINDEX, originals, seen) || reachable;
    lua_gc(state, LUA_GCRESTART, 0);
    std::printf("original loaders absent from globals, package and registry: %s\n",
                reachable ? "FAIL" : "PASS");
    // A normal dump from the tested engine, never a crafted binary input.
    const char* setup = "binary=string.dump(function() binaryRan=true; return 42 end)";
    if (luaL_dostring(state, setup)) return 1;
    lua_getglobal(state, "binary");
    size_t size;
    const char* bytes = lua_tolstring(state, -1, &size);
    FILE* file = std::fopen("tc-text-only-module.lua", "wb");
    if (!file) return 1;
    std::fwrite(bytes, 1, size, file);
    std::fclose(file);
    const std::string binary(bytes, size);
    lua_pop(state, 1);
    const char* tests = R"LUA(
        local function rejected(f, detail)
            assert(f == nil and type(detail) == 'string' and detail:find('binary'), detail)
        end
        rejected(load(binary))
        rejected(load(binary, 'chunk', 'bt'))
        rejected(load(binary, 'chunk', 'b'))
        local pieces = 0
        rejected(load(function() pieces=pieces+1; if pieces==1 then return binary end end))
        if loadstring then rejected(loadstring(binary)) end
        rejected(loadfile('tc-text-only-module.lua'))
        rejected(loadfile('tc-text-only-module.lua', 'b'))
        local ok, detail = pcall(dofile, 'tc-text-only-module.lua')
        assert(not ok and detail:find('binary'), detail)
        package.path = './?.lua'
        ok, detail = pcall(require, 'tc-text-only-module')
        assert(not ok and detail:find('binary'), detail)
        assert(not binaryRan)
        assert(load('return math.abs(-42)')() == 42)
        local sent = false
        assert(load(function() if not sent then sent=true; return 'return 42' end end)() == 42)
        assert(load('return answer', 'env', 't', {answer=42})() == 42)
        if loadstring then assert(loadstring('return 42')() == 42) end
    )LUA";
    int failures = reachable ? 1 : 0;
    auto run = [&](const char* label, const char* source) {
        const int result = luaL_dostring(state, source);
        std::printf("%s: %s%s\n", label, result ? "FAIL " : "PASS", result ? lua_tostring(state, -1) : "");
        failures += result != 0;
        lua_settop(state, 0);
    };
    run("debug cannot recover unrestricted loaders", R"LUA(
        assert(type(debug.getupvalue) == 'function')
        local functions = {load, loadfile, dofile}
        if loadstring then functions[#functions+1] = loadstring end
        local searchers = package.searchers or package.loaders
        functions[#functions+1] = searchers[2]
        for _, installed in ipairs(functions) do
            -- The exact recovery route from the review, followed by a binary
            -- request if an original loader were exposed again.
            local name, recovered = debug.getupvalue(installed, 1)
            if type(recovered) == 'function' then
                local ok, chunk = pcall(recovered, binary, 'dump', 'b')
                assert(not (ok and type(chunk) == 'function'), 'recovered binary loader')
                ok, chunk = pcall(recovered, 'tc-text-only-module.lua', 'b')
                assert(not (ok and type(chunk) == 'function'), 'recovered binary file loader')
            end
            -- The searcher captures only package; the other C functions have
            -- no upvalues. Its reachable graph is also audited natively above.
            assert(name == nil or (installed == searchers[2] and recovered == package))
            assert(debug.getupvalue(installed, 2) == nil)
        end
        assert(not binaryRan)
    )LUA");
    run("reader callbacks and explicit environments", R"LUA(
        local sent = false
        local chunk = assert(load(function()
            collectgarbage()
            if not sent then sent=true; return 'return answer' end
        end, 'reader-env', 'b', {answer=42}))
        assert(chunk() == 42)
        assert(load('return answer', 'env', 'b', {answer=43})() == 43)
        if _VERSION ~= 'Lua 5.1' then
            local f = assert(load('return _ENV', 'nil-env', 't', nil))
            assert(f() == nil)
            sent = false
            f = assert(load(function()
                if not sent then sent=true; return 'return _ENV' end
            end, 'reader-nil-env', 't', nil))
            assert(f() == nil)
        end
        local f, detail = load(function() return {} end)
        assert(f == nil and detail:find('reader function must return a string'), detail)
        f, detail = load(function() error('reader failure') end)
        assert(f == nil and detail:find('reader failure'), detail)
        assert(load(function() return nil end)() == nil)
        sent = false
        f, detail = load(function()
            if not sent then sent=true; return binary:sub(1,1) end
            return binary:sub(2)
        end, 'split-dump', 'b')
        assert(f == nil and detail:find('binary'), detail)
        assert(load('return 42')() == 42)
    )LUA");
    run("binary rejected by all installed loaders; text load works", tests);
#ifdef TCXLUA_USE_LUAJIT
    run("LuaJIT buffer sources load as text without consuming data", R"LUA(
        local buffer = require('string.buffer')
        for _, loader in ipairs({load, loadstring}) do
            local source = buffer.new():put('return 42')
            local text = assert(loader(source))
            assert(text() == 42 and debug.getinfo(text, 'S').source == '')
            assert(source:tostring() == 'return 42')
            assert(assert(loader(source, 'buffer-text', 'b'))() == 42)
            source:reset():put('return answer')
            local f = assert(loader(source, 'buffer-env', 'bt', {answer=43}))
            collectgarbage()
            assert(f() == 43)
            assert(source:tostring() == 'return answer')
            source:reset():put('skipreturn 44'):skip(4)
            assert(assert(loader(source))() == 44)
            assert(source:tostring() == 'return 44')
            source:reset()
            assert(assert(loader(source))() == nil)
            source:put('not valid Lua !')
            local chunk, detail = loader(source, 'buffer-syntax')
            assert(chunk == nil and detail:find('buffer%-syntax'), detail)
            source:reset():put(binary)
            for _, mode in ipairs({'t', 'b', 'bt'}) do
                chunk, detail = loader(source, 'buffer-dump', mode)
                assert(chunk == nil and detail:find('binary'), detail)
            end
            chunk, detail = loader(source)
            assert(chunk == nil and detail:find('binary'), detail)
            assert(source:tostring() == binary)
            assert(not binaryRan)
        end
    )LUA");
#endif
    auto issues = tcxLuaLintUndefinedGlobals(state, {{"dumped", binary}, {"text", "return 42"}});
    const bool lintOk = issues.size() == 1 && issues[0].chunk == "dumped" && issues[0].name.find("binary") != std::string::npos;
    std::printf("lint rejects binary clearly and accepts text: %s\n", lintOk ? "PASS" : "FAIL");
    failures += !lintOk;
    file = std::fopen("tc-text-only-module.lua", "wb");
    if (!file) return 1;
    const char* source = "return 42, 43";
    std::fwrite(source, 1, std::strlen(source), file);
    std::fclose(file);
    run("text files and require work", R"LUA(
        assert(loadfile('tc-text-only-module.lua')() == 42)
        local a,b = dofile('tc-text-only-module.lua'); assert(a==42 and b==43)
        assert(require('tc-text-only-module') == 42)
        assert(loadfile('tc-text-only-module.lua', 't', {})() == 42)
        if _VERSION ~= 'Lua 5.1' then
            local co = coroutine.create(function() return dofile('tc-text-only-module.lua') end)
            local ok,a,b = coroutine.resume(co); assert(ok and a==42 and b==43)
        end
    )LUA");
    file = std::fopen("tc-text-only-module.lua", "wb");
    if (!file) return 1;
    source = "return answer";
    std::fwrite(source, 1, std::strlen(source), file);
    std::fclose(file);
    run("file environment is applied", R"LUA(
        answer = 41
        assert(loadfile('tc-text-only-module.lua')() == 41)
        assert(loadfile('tc-text-only-module.lua', 'b', {answer=42})() == 42)
        assert(loadfile('tc-text-only-module.lua', 't', nil))
        if _VERSION ~= 'Lua 5.1' then
            local ok = pcall(assert(loadfile('tc-text-only-module.lua', 't', nil)))
            assert(not ok)
        end
    )LUA");
    file = std::fopen("tc-text-only-module.lua", "wb");
    if (!file) return 1;
    source = "local v=coroutine.yield(42); return v,43";
    std::fwrite(source, 1, std::strlen(source), file);
    std::fclose(file);
    run("PUC dofile retains continuation results", R"LUA(
        if _VERSION ~= 'Lua 5.1' then
            local co = coroutine.create(function() return dofile('tc-text-only-module.lua') end)
            local ok,value = coroutine.resume(co); assert(ok and value==42)
            local a,b
            ok,a,b = coroutine.resume(co,44); assert(ok and a==44 and b==43)
        end
    )LUA");
    std::remove("tc-text-only-module.lua");
    lua_close(state);
    return failures ? 1 : 0;
}
