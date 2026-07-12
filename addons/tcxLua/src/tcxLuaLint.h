#pragma once
// tcxLuaLint — static "undefined global" lint for Lua chunks.
//
// Lua itself has no compile-time check for undefined variables: reading an
// unknown name silently yields nil at runtime. For live-coding hosts
// (TrussSketch hot reload, exampleLiveUpdate, ...) that is a poor experience —
// a typo'd name only surfaces as a nil-arithmetic error mid-frame, if at all.
//
// This lint compiles (WITHOUT running) each chunk and scans the resulting
// bytecode: every global read (GETTABUP _ENV "name") whose name is neither
// assigned anywhere in the given chunks (SETTABUP _ENV) nor already present
// in the state's globals is reported with its chunk name and line.
//
// Notes:
//  * Plain Lua 5.4 only. With LuaJIT (TCXLUA_USE_LUAJIT) the bytecode format
//    differs, so the lint is a no-op that reports no issues.
//  * Dynamic global access (_G["x"], rawset) is invisible to the scan; a read
//    of such a name is a false positive. Escape hatch: assign the name once
//    (even `name = nil` counts as a definition).

#include <string>
#include <utility>
#include <vector>

struct lua_State;

struct LuaLintIssue {
    std::string chunk;  // chunk (file) name the read occurs in
    int line;           // 1-based line of the read
    std::string name;   // the undefined global being read
};

// Lint the given (chunkName, code) list against lua's current globals
// (bind everything BEFORE calling). Chunks that fail to compile are skipped —
// the host's normal load path reports syntax errors with better context.
std::vector<LuaLintIssue> tcxLuaLintUndefinedGlobals(
    lua_State* lua,
    const std::vector<std::pair<std::string, std::string>>& files);
