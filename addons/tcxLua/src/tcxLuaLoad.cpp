#include "tcxLua.h"

namespace {
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
        struct Reader {
            std::FILE* file;
            char bytes[8 * 1024];
            size_t total = 0;
        } reader{file.get(), {}, 0};
        auto read = [](lua_State*, void* data, size_t* size) -> const char* {
            auto& r = *static_cast<Reader*>(data);
            *size = std::fread(r.bytes, 1, sizeof(r.bytes), r.file);
            // Keep this C callback allocation-free: logging occurs only after
            // the protected C parser returns.
            if (r.total < 256u * 1024u * 1024u) r.total += *size;
            return *size ? r.bytes : nullptr;
        };
        const auto name = "@" + trussc::pathToUtf8(path);
        // sol's compatibility layer supplies the five-argument spelling on
        // LuaJIT too; the bundled engine still owns the protected load.
        int status = lua_load(state, read, &reader, name.c_str(), nullptr);
        if (reader.total >= 256u * 1024u * 1024u)
            luaLoadLog(trussc::LogLevel::Notice, "Loading a large local script (at least 256 MiB)");
        if (std::ferror(file.get()) || status != 0) {
            luaLoadLog(trussc::LogLevel::Error, status ? lua_tostring(state, -1) : "Cannot read complete Lua file");
            lua_pop(state, 1);
            return false;
        }
        sol::protected_function function = sol::stack::pop<sol::protected_function>(state);
        auto result = function();
        if (!result.valid()) {
            sol::error err = result;
            luaLoadLog(trussc::LogLevel::Error, err.what());
            return false;
        }
        return true;
    } catch (const std::exception&) {
        lua_settop(state, originalTop);
        luaLoadLog(trussc::LogLevel::Error, "Cannot load complete Lua file (I/O or allocation failure)");
        return false;
    }
}
