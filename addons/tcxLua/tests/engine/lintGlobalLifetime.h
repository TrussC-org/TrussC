#pragma once
#include "tcxLuaLint.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// The review regression: an app's global lookup removes a candidate and runs GC.
// The suggestion must use its own snapshot of the name throughout the lookup.
inline bool lintGlobalLifetime() {
    lua_State* state = luaL_newstate();
    if (!state) return false;
    luaL_openlibs(state);
    const std::string candidate(30, 'v');
    lua_pushinteger(state, 1);
    lua_setglobal(state, candidate.c_str());
    int status = luaL_dostring(state,
        "setmetatable(_G, {__index=function(t,k) "
        "rawset(t,string.rep('v',30),nil); collectgarbage(); collectgarbage() end})");
    auto issues = tcxLuaLintUndefinedGlobals(state, {{"lifetime", "return " + std::string(29, 'v') + "w"}});
    bool ok = !status && issues.size() == 1 && issues[0].suggestion == candidate;
    lua_close(state);
    return ok;
}
