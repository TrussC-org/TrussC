#pragma once

// Internal policy for states created by tcxLua. The engine and sol2 remain
// upstream-compatible for applications that deliberately use their raw APIs.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cstring>

namespace tcxLuaInternal {
inline const char* textOnlyError(const char* detail) {
    // LuaJIT's upstream mode error does not say which input was rejected.
    return detail && std::strstr(detail, "attempt to load chunk with wrong mode")
        ? "binary chunks are disabled; only text Lua scripts are accepted (mode 't')"
        : detail;
}
inline void clarifyTextOnlyError(lua_State* state) {
    if (lua_type(state, -1) != LUA_TSTRING) return;
    const char* detail = lua_tostring(state, -1);
    const char* clarified = textOnlyError(detail);
    if (clarified != detail) {
        lua_pushstring(state, clarified);
        lua_replace(state, -2);
    }
}
// Return the compiled chunk or nil plus its diagnostic. Keep an omitted env
// distinct from an explicit nil, as in the engine's base library.
inline int textLoadResult(lua_State* state, int status, int environment) {
    if (status == 0) {
#if LUA_VERSION_NUM >= 502
        if (environment) {
            lua_pushvalue(state, environment);
            if (!lua_setupvalue(state, -2, 1)) lua_pop(state, 1);
        }
#else
        // LuaJIT's load/loadfile only use an explicitly supplied table env.
        if (environment && lua_istable(state, environment)) {
            lua_pushvalue(state, environment);
            lua_setfenv(state, -2);
        }
#endif
        return 1;
    }
    clarifyTextOnlyError(state);
    lua_pushnil(state);
    lua_insert(state, -2);
    return 2;
}
inline const char* textReader(lua_State* state, void*, size_t* size) {
    luaL_checkstack(state, 2, "too many nested functions");
    lua_pushvalue(state, 1);
    lua_call(state, 0, 1);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        *size = 0;
        return nullptr;
    }
    if (!lua_isstring(state, -1))
        luaL_error(state, "reader function must return a string");
    // Slot 5 is reserved before entering the parser. Root each reader result
    // there without disturbing the parser's stack or retaining older pieces.
    lua_replace(state, 5);
    return lua_tolstring(state, 5, size);
}
inline int textLoad(lua_State* state) {
    const int environment = lua_isnone(state, 4) ? 0 : 4;
    const char* defaultName = nullptr;
#ifdef TCXLUA_USE_LUAJIT
    // LuaJIT accepts string.buffer sources. Use its documented __tostring
    // behavior through the public API, without depending on buffer internals.
    // Root the resulting string in the source slot across parser allocations.
    if (lua_type(state, 1) == LUA_TUSERDATA && luaL_callmeta(state, 1, "__tostring")) {
        luaL_checktype(state, -1, LUA_TSTRING);
        lua_replace(state, 1);
        defaultName = ""; // Match LuaJIT's default chunk name for buffers.
    }
#endif
    size_t size;
    const char* source = lua_tolstring(state, 1, &size);
    int status;
    if (source) {
        const char* name = luaL_optstring(state, 2, defaultName ? defaultName : source);
        status = luaL_loadbufferx(state, source, size, name, "t");
    } else {
        const char* name = luaL_optstring(state, 2, "=(load)");
        luaL_checktype(state, 1, LUA_TFUNCTION);
        lua_settop(state, 5);
#if LUA_VERSION_NUM >= 502
        status = lua_load(state, textReader, nullptr, name, "t");
#else
        status = lua_loadx(state, textReader, nullptr, name, "t");
#endif
    }
    return textLoadResult(state, status, environment);
}
inline int textLoadFile(lua_State* state) {
    const char* path = luaL_optstring(state, 1, nullptr);
    const int environment = lua_isnone(state, 3) ? 0 : 3;
    const int status = luaL_loadfilex(state, path, "t");
    return textLoadResult(state, status, environment);
}
#if LUA_VERSION_NUM >= 502
inline int textDoFileResult(lua_State* state, int, lua_KContext) {
    return lua_gettop(state);
}
#endif
inline int textDoFile(lua_State* state) {
    const char* path = luaL_optstring(state, 1, nullptr);
    lua_settop(state, 1); // Keep the filename rooted across loader allocations.
    if (luaL_loadfilex(state, path, "t")) {
        clarifyTextOnlyError(state);
        return lua_error(state);
    }
    lua_remove(state, 1);
#if LUA_VERSION_NUM >= 502
    lua_callk(state, 0, LUA_MULTRET, 0, textDoFileResult);
#else
    lua_call(state, 0, LUA_MULTRET);
#endif
    return lua_gettop(state);
}
inline int textSearcher(lua_State* state) {
    // Preserve package.path and searchpath's normal missing-module diagnostics.
    lua_getfield(state, lua_upvalueindex(1), "searchpath");
    lua_pushvalue(state, 1);
    lua_getfield(state, lua_upvalueindex(1), "path");
    lua_call(state, 2, 2);
    if (lua_isnil(state, -2)) return 1;
    lua_pop(state, 1);
    const char* path = lua_tostring(state, -1);
    if (luaL_loadfilex(state, path, "t")) {
        clarifyTextOnlyError(state);
        return lua_error(state);
    }
    lua_insert(state, -2); // loader, filename (Lua 5.4's loader data)
    return 2;
}

// Called through lua_pcall: closure allocation and setup errors stay protected.
inline int installTextOnlyLoaders(lua_State* state) {
    // Never retain the base library's binary-capable functions: debug can
    // inspect C-closure upvalues and registry tables in supported states.
    lua_getglobal(state, "load");
    const bool hasLoad = lua_isfunction(state, -1);
    lua_pop(state, 1);
    if (hasLoad) {
        lua_pushcfunction(state, textLoad);
        lua_setglobal(state, "load");
        lua_getglobal(state, "loadstring");
        const bool hasLoadString = lua_isfunction(state, -1);
        lua_pop(state, 1);
        if (hasLoadString) {
            lua_pushcfunction(state, textLoad);
            lua_setglobal(state, "loadstring");
        }
    }
    lua_getglobal(state, "loadfile");
    const bool hasLoadFile = lua_isfunction(state, -1);
    lua_pop(state, 1);
    if (hasLoadFile) {
        lua_pushcfunction(state, textLoadFile);
        lua_setglobal(state, "loadfile");
        lua_pushcfunction(state, textDoFile);
        lua_setglobal(state, "dofile");
    }
    lua_getglobal(state, "package");
    if (lua_istable(state, -1)) {
#if LUA_VERSION_NUM < 502
        lua_getfield(state, -1, "loaders");
#else
        lua_getfield(state, -1, "searchers");
#endif
        if (lua_istable(state, -1)) {
            lua_pushvalue(state, -2);
            lua_pushcclosure(state, textSearcher, 1);
            lua_rawseti(state, -2, 2);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    return 0;
}
}
