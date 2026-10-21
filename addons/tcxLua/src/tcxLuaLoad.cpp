#include "tcxLua.h"

namespace {
int luaLoadTraceback(lua_State* state) {
    // Only Lua operations here: allocation failures stay in lua_pcall's native
    // protection, with no C++ objects or exceptions crossing the C callback.
    const char* detail = lua_type(state, 1) == LUA_TSTRING
        ? lua_tostring(state, 1) : "Lua execution failed (non-string error)";
    luaL_traceback(state, state, detail, 1);
    return 1;
}

void luaLoadLog(trussc::LogLevel level, const char* message,
                const std::filesystem::path* path = nullptr) noexcept {
    try {
        std::string text = "[tcxLua] ";
        text += message ? message : "Lua load failed";
        if (path) text += trussc::internal::pathToDisplayUtf8(*path);
        trussc::getLogger().log(level, text);
    } catch (...) {
        std::fputs(level == trussc::LogLevel::Notice ? "[NOTICE] [tcxLua] " : "[ERROR] [tcxLua] ", stderr); // log-check: allow (allocation-free fallback after Logger/diagnostic allocation fails)
        std::fputs(message ? message : "Lua load failed", stderr); // log-check: allow (allocation-free fallback after Logger/diagnostic allocation fails)
        std::fputc('\n', stderr);
    }
}

struct LuaFileLoad {
    std::FILE* file;
    const char* name;
    char bytes[8 * 1024];
    size_t total = 0;
    bool readError = false;
};

int luaLoadFileProtected(lua_State* state) {
    // This frame owns no C++ objects: Lua may leave it by a native error jump.
    auto& load = *static_cast<LuaFileLoad*>(lua_touserdata(state, 1));
    lua_pushcfunction(state, luaLoadTraceback);
    const int handler = lua_gettop(state);
    auto read = [](lua_State*, void* data, size_t* size) noexcept -> const char* {
        auto& r = *static_cast<LuaFileLoad*>(data);
        *size = std::fread(r.bytes, 1, sizeof(r.bytes), r.file);
        if (r.total < 256u * 1024u * 1024u) r.total += *size;
        return *size ? r.bytes : nullptr;
    };
    // sol supplies the five-argument spelling for LuaJIT as well.
    int status = lua_load(state, read, &load, load.name, nullptr);
    if (load.total >= 256u * 1024u * 1024u)
        luaLoadLog(trussc::LogLevel::Notice, "Loading a large local script (at least 256 MiB)");
    load.readError = std::ferror(load.file) != 0;
    if (status != 0) return lua_error(state);
    if (load.readError) return 0;
    status = lua_pcall(state, 0, 0, handler);
    if (status != 0) return lua_error(state);
    return 0;
}
}


bool tcxLua::runFile(sol::state& lua, const std::filesystem::path& path) {
    lua_State* state = lua.lua_state();
    const int originalTop = lua_gettop(state);
    try {
        const auto fullPath = trussc::getDataPath(path);
        std::unique_ptr<std::FILE, int(*)(std::FILE*)> file(
            trussc::internal::isLoadFile(fullPath)
                ? trussc::internal::openFile(fullPath, "rb") : nullptr, std::fclose);
        if (!file) {
            luaLoadLog(trussc::LogLevel::Error, "Cannot read file: ", &path);
            return false;
        }
        const auto name = "@" + trussc::pathToUtf8(path);
        LuaFileLoad load{file.get(), name.c_str(), {}};
#if LUA_VERSION_NUM < 502
        // LuaJIT allocates even for a zero-upvalue C function. lua_cpcall
        // protects creation of the entry closure as well as its entire body.
        const int status = lua_cpcall(state, luaLoadFileProtected, &load);
#else
        // PUC Lua's zero-upvalue C function and light userdata pushes do not
        // allocate. lua_checkstack handles its own allocation failure.
        if (!lua_checkstack(state, 2)) throw std::bad_alloc();
        lua_pushcfunction(state, luaLoadFileProtected);
        lua_pushlightuserdata(state, &load);
        const int status = lua_pcall(state, 1, 0, 0);
#endif
        if (status != 0) {
            // Converting a numeric error with lua_tostring can itself allocate.
            const char* detail = lua_type(state, -1) == LUA_TSTRING
                ? lua_tostring(state, -1) : "Lua execution failed (non-string error)";
            luaLoadLog(trussc::LogLevel::Error, detail);
        } else if (load.readError) {
            luaLoadLog(trussc::LogLevel::Error, "Cannot read complete Lua file");
        }
        lua_settop(state, originalTop);
        return status == 0 && !load.readError;
    } catch (const std::exception&) {
        lua_settop(state, originalTop);
        luaLoadLog(trussc::LogLevel::Error, "Cannot load complete Lua file (I/O or allocation failure)");
        return false;
    }
}
