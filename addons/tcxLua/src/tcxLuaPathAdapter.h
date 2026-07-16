#pragma once

// tcxLuaPathAdapter.h
//
// sol2 stack customization for std::filesystem::path.
//
// In C++ a std::filesystem::path is implicitly constructible from a string, so
// saveScreenshot("shot.png") "just works". Lua has no such implicit conversion,
// and sol2 (3.5.0, vendored under addons/tcxLua/include/sol) ships NO built-in
// stack adapter for std::filesystem::path. Every generated binding that takes a
// `const std::filesystem::path&` therefore falls back to sol's generic
// as_value_tag<path> path, which treats the Lua argument as a path *userdata*:
// passing a Lua string makes lua_touserdata() return NULL and sol dereferences
// it -> SIGSEGV
// (unqualified_getter<as_value_tag<std::filesystem::path>>::get_no_lua_nil).
//
// The three specializations below are the sol2-level equivalent of C++'s
// implicit string->path conversion. They are the sol 3.5.0 stack customization
// points (see sol/sol.hpp forward declarations):
//     template <typename T, typename = void>      struct unqualified_getter;
//     template <typename T, typename = void>      struct unqualified_pusher;
//     template <typename T, type t, typename = void> struct unqualified_checker;
// lua_type_of_v<std::filesystem::path> defaults to type::userdata (sol's generic
// class fallback), so the checker is specialized on type::userdata.
//
// This header is included from tcxLua.h immediately after sol/sol.hpp, before
// TrussC.h, so that EVERY binding translation unit (trussc_generated.cpp,
// trussctype_generated.cpp, tcxLua.cpp) instantiates path getters/checkers/
// pushers through these specializations. A per-.cpp fix would not be visible to
// the generated TUs where the crashing instantiations live.
//
// UTF-8 semantics: Lua strings are UTF-8 byte sequences. We build the path from
// a std::u8string (char8_t ctor) so the bytes are interpreted as UTF-8 rather
// than the platform's narrow ANSI codepage -- required for correct non-ASCII
// paths on Windows. The pusher mirrors this by emitting path.u8string().

#include <cstddef>
#include <filesystem>
#include <string>

#include "sol/sol.hpp"

namespace sol {

    // std::filesystem::path has begin()/end() (component iteration), so sol2's
    // container detection would otherwise treat it as a Lua container and try
    // to instantiate container_traits (which needs operator[] -> hard compile
    // error, and would be wrong semantics anyway). A path is a VALUE here --
    // pushed/read as a UTF-8 Lua string via the stack specializations below.
    template <>
    struct is_container<std::filesystem::path> : std::false_type {};

    // Tell sol2 a path IS a Lua string (value semantics). Without this the
    // default lua_type_of = userdata makes usertype member fields of type
    // fs::path (e.g. FileDialogResult.filePath) go through the by-reference
    // userdata machinery, which cannot bind our by-value getter result
    // (non-const lvalue ref to temporary) and fails to compile.
    template <>
    struct lua_type_of<std::filesystem::path>
        : std::integral_constant<type, type::string> {};

namespace stack {

    // Lua string (LUA_TSTRING) -> std::filesystem::path.
    template <>
    struct unqualified_getter<std::filesystem::path> {
        static std::filesystem::path get(lua_State* L, int index, record& tracking) {
            tracking.use(1);
            std::size_t len = 0;
            const char* str = lua_tolstring(L, index, &len);
            if (str == nullptr) {
                return std::filesystem::path();
            }
            // Interpret the Lua bytes as UTF-8 (char8_t ctor) for Windows correctness.
            return std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(str), len));
        }
    };

    // Accept a Lua string where a std::filesystem::path is expected. Anything
    // else is rejected through the handler with a useful message.
    template <>
    struct unqualified_checker<std::filesystem::path, type::string> {
        template <typename Handler>
        static bool check(lua_State* L, int index, Handler&& handler, record& tracking) {
            tracking.use(1);
            const type t = type_of(L, index);
            if (t == type::string) {
                return true;
            }
            handler(L, index, type::string, t, "expected a file path (string)");
            return false;
        }
    };

    // std::filesystem::path -> Lua string (UTF-8), mirror of pathToString.
    template <>
    struct unqualified_pusher<std::filesystem::path> {
        static int push(lua_State* L, const std::filesystem::path& p) {
            std::u8string u8 = p.u8string();
            lua_pushlstring(L, reinterpret_cast<const char*>(u8.data()), u8.size());
            return 1;
        }
    };

} // namespace stack
} // namespace sol
