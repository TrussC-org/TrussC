// Optional compatibility check: a valid LuaJIT dump larger than 2 GiB.
// Repeated literals are interned, keeping compiler/engine memory modest while
// the serialized file grows. Kept out of the daily addon harness due to disk I/O.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <cstdio>
#include <string>
struct LargeSource {
    std::string body = "t[#t+1]=function() return [=[" + std::string(8u*1024u*1024u, 'x') + "]=] end\n";
    int part = 0;
    static const char* read(lua_State*, void* data, size_t* size) {
        auto& source = *static_cast<LargeSource*>(data);
        ++source.part;
        if (source.part == 1) { *size = 11; return "local t={}\n"; }
        if (source.part <= 258) { *size = source.body.size(); return source.body.data(); }
        if (source.part == 259) { *size = 9; return "return t\n"; }
        *size = 0; return nullptr;
    }
};
struct Dump {
    FILE* file;
    size_t bytes = 0;
    static int write(lua_State*, const void* data, size_t size, void* context) {
        auto& dump = *static_cast<Dump*>(context);
        dump.bytes += size;
        return std::fwrite(data, 1, size, dump.file) != size;
    }
};
struct FileSource {
    FILE* file;
    char buffer[8192];
    static const char* read(lua_State*, void* data, size_t* size) {
        auto& source = *static_cast<FileSource*>(data);
        *size = std::fread(source.buffer, 1, sizeof(source.buffer), source.file);
        return *size ? source.buffer : nullptr;
    }
};
int main() {
    lua_State* state = luaL_newstate();
    if (!state) return 1;
    LargeSource source;
    int status = lua_load(state, LargeSource::read, &source, "large-valid-module");
    FILE* file = std::tmpfile();
    if (!file) { lua_close(state); return 1; }
    Dump dump{file};
    if (!status) status = lua_dump(state, Dump::write, &dump);
    std::printf("dump bytes=%zu status=%d\n", dump.bytes, status);
    lua_settop(state, 0);
    lua_gc(state, LUA_GCCOLLECT, 0);
    std::rewind(file);
    FileSource input{file, {}};
    if (!status) status = lua_load(state, FileSource::read, &input, "large-valid-bytecode");
    std::printf("reload status=%d %s\n", status, status ? lua_tostring(state, -1) : "ok");
    bool ok = !status && dump.bytes >= size_t(2)*1024*1024*1024;
    std::fclose(file);
    lua_close(state);
    return ok ? 0 : 1;
}
