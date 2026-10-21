extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>
struct Memory {
    size_t live = 0, limit = std::numeric_limits<size_t>::max();
    int refused = 0;
    static void* alloc(void* context, void* ptr, size_t oldSize, size_t size) {
        auto& m = *static_cast<Memory*>(context);
        if (!ptr) oldSize = 0;
        if (!size) { m.live -= oldSize; std::free(ptr); return nullptr; }
        if (size > oldSize && size - oldSize > m.limit - m.live) { ++m.refused; return nullptr; }
        void* result = std::realloc(ptr, size);
        if (result) m.live = m.live - oldSize + size;
        return result;
    }
};
int main() {
    Memory memory;
    lua_State* L = lua_newstate(Memory::alloc, &memory);
    if (!L) return 1;
    std::string source;
    for (int i = 0; i < 4000; ++i) source += "value = 7\n";
    int status = luaL_loadbuffer(L, source.data(), source.size(), "memory");
    std::vector<char> binary;
    auto writer = [](lua_State*, const void* bytes, size_t size, void* context) noexcept -> int {
        try {
            auto& out = *static_cast<std::vector<char>*>(context);
            const auto* begin = static_cast<const char*>(bytes);
            out.insert(out.end(), begin, begin + size);
            return 0;
        } catch (...) { return 1; }
    };
    if (!status) status = lua_dump(L, writer, &binary, 0);
    if (status || binary.empty()) {
        std::fputs("compile/dump tight-memory fixture: FAIL\n", stderr);
        lua_close(L);
        return 1;
    }
    lua_settop(L, 0);
    lua_gc(L, LUA_GCCOLLECT);
    const size_t before = memory.live;
    lua_gc(L, LUA_GCSTOP);
    lua_newuserdatauv(L, 1024 * 1024, 0);
    lua_pop(L, 1); // Unreachable memory is available to emergency collection.
    memory.limit = memory.live + 1024;
    if (!status) status = luaL_loadbuffer(L, binary.data(), binary.size(), "memory");
    bool ok = !status && memory.refused > 0 && memory.live < before + 1024 * 1024;
    std::printf("binary buffer uses emergency GC under tight memory: %s\n", ok ? "PASS" : "FAIL");
    memory.limit = std::numeric_limits<size_t>::max();
    lua_close(L);
    ok = ok && memory.live == 0;
    std::printf("binary buffer allocation accounting balances: %s\n", memory.live == 0 ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
