#pragma once

// =============================================================================
// tcTypeName.h - Human-readable type names from RTTI
// =============================================================================
//
// Turns a std::type_info into a demangled, readable type name (e.g.
// "trussc::RectNode" instead of the mangled "N6trussc8RectNodeE").
//
// The demangled string is cached per type, so the (relatively expensive)
// demangle runs at most once per type for the whole process — calling this
// for tens of thousands of instances costs one lookup each, no per-instance
// storage. Used by Node::getTypeName() for the scene-graph debugger.
//
// Requires RTTI (enabled by default). On a polymorphic object,
// typeName(typeid(*ptr)) yields the dynamic (most-derived) type.

#include <typeinfo>
#include <typeindex>
#include <string>
#include <unordered_map>
#include <mutex>

#if defined(__GNUG__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace trussc {

// Demangle a raw type_info name into a readable string (platform-specific).
inline std::string demangleTypeName(const char* mangled) {
#if defined(__GNUG__) || defined(__clang__)
    int status = 0;
    char* d = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    std::string result = (status == 0 && d) ? d : mangled;
    std::free(d);
    return result;
#else
    // MSVC: name() is already readable, e.g. "class trussc::RectNode".
    // Strip the leading "class "/"struct "/"enum " keyword for cleanliness.
    std::string s = mangled;
    for (const char* kw : {"class ", "struct ", "enum "}) {
        if (s.rfind(kw, 0) == 0) { s.erase(0, std::string(kw).size()); break; }
    }
    return s;
#endif
}

// Readable name for a type, cached per type (one demangle per type, ever).
// Returns a reference into a process-wide cache, valid for the program's life.
inline const std::string& typeName(const std::type_info& ti) {
    static std::unordered_map<std::type_index, std::string> cache;
    static std::mutex mutex;  // contended only on first sight of a new type
    std::lock_guard<std::mutex> lock(mutex);
    std::type_index key(ti);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    return cache.emplace(key, demangleTypeName(ti.name())).first->second;
}

// Convenience: readable name for a static type T.
template <typename T>
inline const std::string& typeName() {
    return typeName(typeid(T));
}

} // namespace trussc
