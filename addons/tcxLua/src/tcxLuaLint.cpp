#include "tcxLuaLint.h"

#ifdef TCXLUA_USE_LUAJIT

// LuaJIT bytecode is a different beast; no static lint there (yet).
std::vector<LuaLintIssue> tcxLuaLintUndefinedGlobals(
    lua_State*, const std::vector<std::pair<std::string, std::string>>&) {
    return {};
}

#else

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

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

// Candidate name pool for did-you-mean. rank orders sources: 0 = names the user
// wrote (global writes / locals), 1 = built-in globals already in the state.
// Lower rank wins ties so "the name you typed elsewhere" beats a stdlib symbol.
constexpr int kRankUser    = 0;
constexpr int kRankBuiltin = 1;

// Recursively scan a compiled function: collect global reads (with location),
// global writes, and every local variable name from the debug info. A "global"
// access is GETTABUP/SETTABUP through the _ENV upvalue with a constant string
// key — exactly what `name` / `name = v` compile to. Dynamic access (_G[expr])
// uses register keys and is skipped.
void scanProto(const Proto* p, const std::string& chunk,
               std::vector<GlobalRead>& reads, std::set<std::string>& writes,
               std::set<std::string>& locals) {
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
    // Local variable names (debug info) — the killer suggestion source: a stale
    // `bh` next to a live `local zh` should point back at zh.
    for (int i = 0; i < p->sizelocvars; ++i) {
        TString* vn = p->locvars[i].varname;
        if (vn) locals.insert(getstr(vn));
    }
    for (int i = 0; i < p->sizep; ++i)
        scanProto(p->p[i], chunk, reads, writes, locals);
}

// Damerau–Levenshtein (optimal string alignment) edit distance. Counts an
// adjacent transposition as one edit, so "teh"->"the" costs 1.
int damerauLevenshtein(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    if (n == 0) return static_cast<int>(m);
    if (m == 0) return static_cast<int>(n);
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = 0; i <= n; ++i) d[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= m; ++j) d[0][j] = static_cast<int>(j);
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int best = std::min({d[i - 1][j] + 1,       // deletion
                                 d[i][j - 1] + 1,       // insertion
                                 d[i - 1][j - 1] + cost});  // substitution
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                best = std::min(best, d[i - 2][j - 2] + 1);  // transposition
            d[i][j] = best;
        }
    }
    return d[n][m];
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// Best candidate for `name` under the scoring rules: distance <= 2 AND
// distance <= max(1, len/2). Ties broken by (1) smaller distance, (2) lower
// source rank (user names over built-ins), (3) case-insensitive equality.
std::string bestSuggestion(const std::string& name,
                           const std::map<std::string, int>& candidates) {
    const int limit = std::min(2, std::max(1, static_cast<int>(name.size()) / 2));
    std::string best;
    int bestDist = 1 << 30, bestRank = 1 << 30, bestCi = 0;
    for (const auto& c : candidates) {
        if (c.first == name) continue;  // identical: not a suggestion
        int dist = damerauLevenshtein(name, c.first);
        if (dist > limit) continue;
        int ci = iequals(name, c.first) ? 1 : 0;  // prefer pure case fixes
        bool better = dist < bestDist ||
                      (dist == bestDist && c.second < bestRank) ||
                      (dist == bestDist && c.second == bestRank && ci > bestCi);
        if (better) {
            best = c.first; bestDist = dist; bestRank = c.second; bestCi = ci;
        }
    }
    return best;
}

}  // namespace

std::vector<LuaLintIssue> tcxLuaLintUndefinedGlobals(
    lua_State* lua,
    const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<GlobalRead> reads;
    std::set<std::string> writes;
    std::set<std::string> locals;

    for (const auto& f : files) {
        if (luaL_loadbuffer(lua, f.second.c_str(), f.second.size(),
                            f.first.c_str()) != LUA_OK) {
            lua_pop(lua, 1);  // syntax error: the host's load path reports it
            continue;
        }
        // The loaded chunk stays on the stack while we scan, keeping its
        // Proto alive against GC.
        const LClosure* cl = static_cast<const LClosure*>(lua_topointer(lua, -1));
        scanProto(cl->p, f.first, reads, writes, locals);
        lua_pop(lua, 1);
    }

    // Build the did-you-mean candidate pool: user-written names (writes/locals)
    // first, then every string key already in _G (~600 built-ins). Lower rank
    // wins ties, so a lower rank set for a name is never overwritten by builtin.
    std::map<std::string, int> candidates;
    for (const auto& w : writes) candidates[w] = kRankUser;
    for (const auto& l : locals) candidates[l] = kRankUser;
    lua_pushglobaltable(lua);
    lua_pushnil(lua);
    while (lua_next(lua, -2) != 0) {
        if (lua_type(lua, -2) == LUA_TSTRING) {
            const char* key = lua_tostring(lua, -2);
            if (key) candidates.emplace(key, kRankBuiltin);  // keep lower rank
        }
        lua_pop(lua, 1);  // pop value, keep key for next iteration
    }
    lua_pop(lua, 1);  // pop the globals table

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
        issues.push_back({r.chunk, r.line, r.name, bestSuggestion(r.name, candidates)});
    }
    return issues;
}

#endif  // TCXLUA_USE_LUAJIT
