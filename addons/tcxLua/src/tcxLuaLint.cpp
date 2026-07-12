#include "tcxLuaLint.h"

#ifdef TCXLUA_USE_LUAJIT

// LuaJIT bytecode is a different beast; no static lint there (yet).
std::vector<LuaLintIssue> tcxLuaLintUndefinedGlobals(
    lua_State*, const std::vector<std::pair<std::string, std::string>>&) {
    return {};
}

#else

#include <cstring>
#include <set>

// Lua internals (Proto/bytecode layout). These headers live in lua/src which
// is not on the public include path — reach them relative to this file. Lua
// is compiled as C in this addon, so wrap for linkage.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "../lua/src/lobject.h"
#include "../lua/src/lopcodes.h"
#include "../lua/src/ldebug.h"  // luaG_getfuncline
}

namespace {

struct GlobalRead {
    std::string chunk;
    int line;
    std::string name;
};

// Recursively scan a compiled function: collect global reads (with location)
// and global writes. A "global" access is GETTABUP/SETTABUP through the _ENV
// upvalue with a constant string key — exactly what `name` / `name = v`
// compile to. Dynamic access (_G[expr]) uses register keys and is skipped.
void scanProto(const Proto* p, const std::string& chunk,
               std::vector<GlobalRead>& reads, std::set<std::string>& writes) {
    for (int pc = 0; pc < p->sizecode; ++pc) {
        Instruction ins = p->code[pc];
        OpCode op = GET_OPCODE(ins);
        if (op != OP_GETTABUP && op != OP_SETTABUP) continue;

        int upIdx  = (op == OP_GETTABUP) ? GETARG_B(ins) : GETARG_A(ins);
        int keyIdx = (op == OP_GETTABUP) ? GETARG_C(ins) : GETARG_B(ins);
        if (upIdx >= p->sizeupvalues || keyIdx >= p->sizek) continue;

        TString* upName = p->upvalues[upIdx].name;
        if (!upName || std::strcmp(getstr(upName), "_ENV") != 0) continue;

        const TValue* k = &p->k[keyIdx];
        if (!ttisstring(k)) continue;
        const char* name = getstr(tsvalue(k));

        if (op == OP_GETTABUP) {
            reads.push_back({chunk, luaG_getfuncline(p, pc), name});
        } else {
            writes.insert(name);
        }
    }
    for (int i = 0; i < p->sizep; ++i) scanProto(p->p[i], chunk, reads, writes);
}

}  // namespace

std::vector<LuaLintIssue> tcxLuaLintUndefinedGlobals(
    lua_State* lua,
    const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<GlobalRead> reads;
    std::set<std::string> writes;

    for (const auto& f : files) {
        if (luaL_loadbuffer(lua, f.second.c_str(), f.second.size(),
                            f.first.c_str()) != LUA_OK) {
            lua_pop(lua, 1);  // syntax error: the host's load path reports it
            continue;
        }
        // The loaded chunk stays on the stack while we scan, keeping its
        // Proto alive against GC.
        const LClosure* cl = static_cast<const LClosure*>(lua_topointer(lua, -1));
        scanProto(cl->p, f.first, reads, writes);
        lua_pop(lua, 1);
    }

    std::vector<LuaLintIssue> issues;
    std::set<std::string> reported;  // dedupe identical chunk:line:name
    for (const auto& r : reads) {
        if (writes.count(r.name)) continue;
        lua_getglobal(lua, r.name.c_str());
        bool defined = !lua_isnil(lua, -1);
        lua_pop(lua, 1);
        if (defined) continue;
        std::string key = r.chunk + ":" + std::to_string(r.line) + ":" + r.name;
        if (!reported.insert(key).second) continue;
        issues.push_back({r.chunk, r.line, r.name});
    }
    return issues;
}

#endif  // TCXLUA_USE_LUAJIT
