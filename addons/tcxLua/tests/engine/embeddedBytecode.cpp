extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#ifdef _WIN32
__declspec(dllexport)
#endif
unsigned char luaJIT_BC_trussc_embedded[1024] = {};
}
#include <cstdio>
#include <cstring>
struct Dump {
    size_t size = 0;
    static int write(lua_State*, const void* data, size_t size, void* context) {
        auto& dump = *static_cast<Dump*>(context);
        if (size > sizeof(luaJIT_BC_trussc_embedded) - dump.size) return 1;
        std::memcpy(luaJIT_BC_trussc_embedded + dump.size, data, size);
        dump.size += size;
        return 0;
    }
};
int main() {
    lua_State* L = luaL_newstate();
    if (!L) return 1;
    luaL_openlibs(L);
    Dump dump;
    int status = luaL_loadstring(L, "return 'embedded ok'");
    if (!status) status = lua_dump(L, Dump::write, &dump);
    lua_settop(L, 0);
    if (!status) status = luaL_loadbuffer(L, reinterpret_cast<const char*>(luaJIT_BC_trussc_embedded), ~(size_t)0, "trussc_embedded");
    bool direct = status == 0;
    std::printf("embedded bytecode SIZE_MAX convention: %s\n", direct ? "PASS" : "FAIL");
    lua_settop(L, 0);
    status = luaL_dostring(L, "return require('trussc_embedded')");
    bool required = !status && std::strcmp(lua_tostring(L, -1), "embedded ok") == 0;
    std::printf("require exported luaJIT_BC module: %s\n", required ? "PASS" : "FAIL");
    lua_close(L);
    return direct && required ? 0 : 1;
}
