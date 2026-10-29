#include "tcxLuaLint.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <cstdio>
int main() {
    lua_State* L = luaL_newstate();
    if (!L) return 1;
    std::vector<std::pair<std::string, std::string>> files;
    for (int i = 0; i < 65; ++i)
        files.emplace_back("file" + std::to_string(i), "return absentName" + std::to_string(i));
    auto issues = tcxLuaLintUndefinedGlobals(L, files);
    bool ok = issues.size() == files.size();
    for (size_t i = 0; ok && i < issues.size(); ++i)
        ok = issues[i].chunk == files[i].first && issues[i].name == "absentName" + std::to_string(i);
    lua_close(L);
    std::printf("per-file read names survive throwing set moves: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
