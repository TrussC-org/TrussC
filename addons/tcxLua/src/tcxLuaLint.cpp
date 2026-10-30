#include "tcxLuaLint.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <list>
#include <cstdio>
#include <map>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>

extern "C" {
#ifdef TCXLUA_USE_LUAJIT
#include "../LuaJIT/src/lua.h"
#include "../LuaJIT/src/lauxlib.h"
#else
#include "lua.h"
#include "lauxlib.h"
#include "../lua/src/lobject.h"
#include "../lua/src/lopcodes.h"
#include "../lua/src/ldebug.h"
#include "../lua/src/ldo.h"
#endif
}

namespace {
using View = std::string_view;

struct LintWork {
    void ensureStack(lua_State* lua) {
        if (!lua_checkstack(lua, 4)) throw std::runtime_error("lint stack allocation failed");
    }
    int compile(lua_State* lua, const std::pair<std::string, std::string>& file) {
        return luaL_loadbufferx(lua, file.second.data(), file.second.size(), file.first.c_str(), "t");
    }
};

#ifndef TCXLUA_USE_LUAJIT
// Keep global-table metatable behavior, but contain Lua errors and allocator
// failures without creating an unprotected C closure or interning a name.
bool globalDefined(lua_State* lua, const char* name) {
    struct Lookup { const char* name; bool defined; } lookup{name, false};
    auto query = [](lua_State* state, void* context) {
        auto& value = *static_cast<Lookup*>(context);
        lua_getglobal(state, value.name);
        value.defined = !lua_isnil(state, -1);
        lua_pop(state, 1);
    };
    int status = luaD_pcall(lua, query, &lookup, savestack(lua, lua->top.p), 0);
    if (status) {
        lua_pop(lua, 1);
        throw std::length_error("lint global lookup failed");
    }
    return lookup.defined;
}

struct GlobalRead {
    View chunk;
    int line;
    View name;
    GlobalRead(View file, int sourceLine, const char* identifier)
        : chunk(file), line(sourceLine), name(identifier) {}
};
using Names = std::set<std::string, std::less<>>;
using Reads = std::vector<GlobalRead>;
using Candidates = std::map<View, int>;

void scanProto(const Proto* p, View chunk, Reads& reads, Names& writes,
               Names& locals, Names& readNames, unsigned depth = 0) {
    // Bound (security): native-stack traversal allows LUAI_MAXCCALLS levels, covering parser-produced prototypes. Overflow rejects this file and is counted by its diagnostic result.
    if (depth >= LUAI_MAXCCALLS) throw std::length_error("lint prototype depth: capacity=" + std::to_string(LUAI_MAXCCALLS) + " needed=" + std::to_string(depth + 1));
    for (int pc = 0; pc < p->sizecode; ++pc) {
        Instruction ins = p->code[pc];
        OpCode op = GET_OPCODE(ins);
        if (op != OP_GETTABUP && op != OP_SETTABUP) continue;
        int upIdx = op == OP_GETTABUP ? GETARG_B(ins) : GETARG_A(ins);
        int keyIdx = op == OP_GETTABUP ? GETARG_C(ins) : GETARG_B(ins);
        if (upIdx >= p->sizeupvalues || keyIdx >= p->sizek) continue;
        TString* upName = p->upvalues[upIdx].name;
        if (!upName || std::strcmp(getstr(upName), "_ENV")) continue;
        const TValue* key = &p->k[keyIdx];
        if (!ttisstring(key)) continue;
        const char* name = getstr(tsvalue(key));
        // Intern read names once: bytecode can reuse one long constant in
        // many instructions. Per-read copies would amplify scratch storage.
        if (op == OP_GETTABUP) reads.emplace_back(chunk, luaG_getfuncline(p, pc), readNames.emplace(name).first->c_str());
        else writes.emplace(name);
    }
    for (int i = 0; i < p->sizelocvars; ++i) {
        if (p->locvars[i].varname) locals.emplace(getstr(p->locvars[i].varname));
    }
    for (int i = 0; i < p->sizep; ++i)
        scanProto(p->p[i], chunk, reads, writes, locals, readNames, depth + 1);
}

// Optimal string alignment for distance <= 2, with a five-cell diagonal
// band and three rows. Storage is constant; work is linear in name length.
int editDistance(View a, View b) {
    if (a.size() > b.size() + 2 || b.size() > a.size() + 2) return 3;
    std::array<std::array<int, 5>, 3> rows;
    for (auto& row : rows) row.fill(3);
    std::array<size_t, 3> starts{};
    std::array<size_t, 3> ends{};
    ends[0] = std::min(size_t(2), b.size());
    for (size_t j = 0; j <= ends[0]; ++j) rows[0][j] = static_cast<int>(j);
    auto at = [&](size_t i, size_t j) {
        size_t row = i % 3;
        return j < starts[row] || j > ends[row] ? 3 : rows[row][j - starts[row]];
    };
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t row = i % 3;
        starts[row] = i > 2 ? i - 2 : 0;
        ends[row] = std::min(b.size(), i + 2);
        rows[row].fill(3);
        for (size_t j = starts[row]; j <= ends[row]; ++j) {
            int value;
            if (!j) value = static_cast<int>(i);
            else {
                value = std::min({at(i-1, j)+1, at(i, j-1)+1,
                                  at(i-1, j-1)+(a[i-1] != b[j-1])});
                if (i > 1 && j > 1 && a[i-1] == b[j-2] && a[i-2] == b[j-1])
                    value = std::min(value, at(i-2, j-2)+1);
            }
            rows[row][j - starts[row]] = std::min(3, value);
        }
    }
    return at(a.size(), b.size());
}

bool iequals(View a, View b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
View bestSuggestion(View name, const Candidates& candidates) {
    const int limit = name.size() < 4 ? 1 : 2;
    View best;
    int bestDist = 3, bestRank = 2, bestCi = 0;
    for (const auto& candidate : candidates) {
        if (candidate.first == name) continue;
        int distance = editDistance(name, candidate.first);
        if (distance > limit) continue;
        int ci = iequals(name, candidate.first) ? 1 : 0;
        if (distance < bestDist || (distance == bestDist && candidate.second < bestRank) ||
            (distance == bestDist && candidate.second == bestRank && ci > bestCi)) {
            best = candidate.first; bestDist = distance; bestRank = candidate.second; bestCi = ci;
        }
    }
    return best;
}
#endif
} // namespace

std::vector<LuaLintIssue> tcxLuaLintUndefinedGlobals(
    lua_State* lua, const std::vector<std::pair<std::string, std::string>>& files) {
    const int originalTop = lua_gettop(lua);
    try {
        LintWork budget;
        budget.ensureStack(lua);
#ifndef TCXLUA_USE_LUAJIT
        struct FileScan {
            View chunk;
            Reads reads;
            Names writes, locals, readNames;
        };
        // Keep scans in place: read-name views must survive even when an STL
        // set has a throwing move constructor (as on MSVC).
        std::list<FileScan> scans;
#endif
        std::vector<LuaLintIssue> issues;
        for (const auto& file : files) {
            try {
                const int status = budget.compile(lua, file);
                if (status) {
                    if (status != LUA_ERRMEM && (file.second.empty() || file.second[0] != 27)) {
                        lua_pop(lua, 1);
                        continue; // source syntax errors are reported by the host load path
                    }
                    const char* detail = lua_tostring(lua, -1);
                    if (detail && std::strstr(detail, "attempt to load chunk with wrong mode"))
                        detail = "binary chunks are rejected; only text Lua scripts are accepted (mode 't')";
                    throw std::runtime_error(detail ? detail : "lint compilation failed");
                }
#ifndef TCXLUA_USE_LUAJIT
                FileScan scan{file.first, {}, {}, {}, {}};
                const auto* closure = static_cast<const LClosure*>(lua_topointer(lua, -1));
                scanProto(closure->p, file.first, scan.reads, scan.writes, scan.locals, scan.readNames);
                // Moving the sets preserves the interned name storage referenced
                // by reads. Publish only a completely scanned file.
                scans.push_back(std::move(scan));
#endif
                lua_pop(lua, 1);
            } catch (const std::exception& error) {
                lua_settop(lua, originalTop);
                issues.push_back({file.first, 0, std::string("lint check failed: ") + error.what(), ""});
            }
        }
#ifndef TCXLUA_USE_LUAJIT
        Candidates candidates;
        Names writes, globals;
        for (const auto& scan : scans) {
            writes.insert(scan.writes.begin(), scan.writes.end());
            for (const auto& name : scan.writes) candidates.emplace(View(name), 0);
            for (const auto& name : scan.locals) candidates.emplace(View(name), 0);
        }
        lua_pushglobaltable(lua);
        lua_pushnil(lua);
        while (lua_next(lua, -2)) {
            if (lua_type(lua, -2) == LUA_TSTRING) {
                size_t length;
                const char* key = lua_tolstring(lua, -2, &length);
                globals.emplace(key, length);
            }
            lua_pop(lua, 1);
        }
        lua_pop(lua, 1);
        // Own every global key before lua_getglobal can run app code or GC.
        for (const auto& name : globals) candidates.emplace(View(name), 1);
        std::set<std::tuple<View, int, View>> reported;
        for (const auto& scan : scans) {
            try {
                std::vector<LuaLintIssue> fileIssues;
                for (const auto& read : scan.reads) {
                    if (writes.count(read.name)) continue;
                    if (globalDefined(lua, read.name.data()) ||
                        !reported.emplace(read.chunk, read.line, View(read.name)).second) continue;
                    View suggestion = bestSuggestion(read.name, candidates);
                    fileIssues.push_back({std::string(read.chunk), read.line, std::string(read.name), std::string(suggestion)});
                }
                issues.insert(issues.end(), std::make_move_iterator(fileIssues.begin()), std::make_move_iterator(fileIssues.end()));
            } catch (const std::exception& error) {
                lua_settop(lua, originalTop);
                issues.push_back({std::string(scan.chunk), 0, std::string("lint check failed: ") + error.what(), ""});
            }
        }
#endif
        return issues;
    } catch (const std::exception& error) {
        lua_settop(lua, originalTop);
        try {
            return {{"[lint]", 0, std::string("lint check failed: ") + error.what(), ""}};
        } catch (...) {
            // The existing vector result itself needs storage for a diagnostic.
            // If even that cannot be allocated, still report Error and return
            // no usable prefix without allowing an exception to escape.
            std::fputs("[ERROR] [tcxLuaLint] Cannot allocate lint failure diagnostic\n", stderr); // log-check: allow (allocation-free fallback after Logger/diagnostic allocation fails)
            return {};
        }
    }
}
