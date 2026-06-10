#pragma once

// =============================================================================
// tcJsonReflect.h - JSON backend for TC_REFLECT reflection
//
// Two Reflector backends turn any reflected type into JSON and back:
//
//     JsonWriteReflector w;                // members -> JSON
//     node.reflectMembers(w);
//     Json j = w.members;                  // {"pos":[0,0,0],"visible":true,...}
//
//     JsonReadReflector r(j);              // JSON -> members. Values go
//     node.reflectMembers(r);              // through TC_PROPERTY setters, so
//                                          // invariants / dirty flags run.
//
// Encoding (matches the inspector's member model):
//     float/int/bool/string -> the JSON scalar
//     Vec2  -> [x, y]
//     Vec3  -> [x, y, z]
//     Color -> [r, g, b, a]   (floats 0-1; reading accepts 3 elems, alpha kept)
//
// The reader applies only the keys present in the source object and records
// what happened per key (applied / skipped on type mismatch / unknown), so
// callers — e.g. the MCP set_node_members tool — can report typos honestly.
//
// Deserialization that *creates* objects (type-name -> instance factory) is
// out of scope here; these reflectors only read/write members of objects that
// already exist.
// =============================================================================

#include <set>
#include <vector>
#include "tcJson.h"
#include "tcReflect.h"
#include "../../tcMath.h"
#include "../../tcColor.h"

namespace trussc {

// ---------------------------------------------------------------------------
// JsonWriteReflector - reflected members -> Json object
// ---------------------------------------------------------------------------
struct JsonWriteReflector : Reflector {
    Json members = Json::object();

    bool visit(const char* name, float& v) override { members[name] = v; return false; }
    bool visit(const char* name, int& v) override { members[name] = v; return false; }
    bool visit(const char* name, bool& v) override { members[name] = v; return false; }
    bool visit(const char* name, std::string& v) override { members[name] = v; return false; }
    bool visit(const char* name, Vec2& v) override { members[name] = Json::array({v.x, v.y}); return false; }
    bool visit(const char* name, Vec3& v) override { members[name] = Json::array({v.x, v.y, v.z}); return false; }
    bool visit(const char* name, Color& v) override { members[name] = Json::array({v.r, v.g, v.b, v.a}); return false; }
};

// ---------------------------------------------------------------------------
// JsonReadReflector - Json object -> reflected members (through setters)
// ---------------------------------------------------------------------------
class JsonReadReflector : public Reflector {
public:
    explicit JsonReadReflector(const Json& src) : src_(src) {}

    std::vector<std::string> applied;   // keys that matched a member and were written
    std::vector<std::string> skipped;   // keys that matched a member but had the wrong JSON shape

    // Keys in the source object that matched no reflected member (typos etc.).
    // Valid after reflectMembers() has run.
    std::vector<std::string> unknownKeys() const {
        std::vector<std::string> out;
        if (src_.is_object()) {
            for (auto it = src_.begin(); it != src_.end(); ++it) {
                if (!seen_.count(it.key())) out.push_back(it.key());
            }
        }
        return out;
    }

    bool visit(const char* name, float& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (!j->is_number()) return skip(name);
        v = j->get<float>();
        return apply(name);
    }

    bool visit(const char* name, int& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (!j->is_number()) return skip(name);
        v = j->get<int>();
        return apply(name);
    }

    bool visit(const char* name, bool& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (!j->is_boolean()) return skip(name);
        v = j->get<bool>();
        return apply(name);
    }

    bool visit(const char* name, std::string& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (!j->is_string()) return skip(name);
        v = j->get<std::string>();
        return apply(name);
    }

    bool visit(const char* name, Vec2& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (!isNumberArray(*j, 2)) return skip(name);
        v.x = (*j)[0].get<float>();
        v.y = (*j)[1].get<float>();
        return apply(name);
    }

    bool visit(const char* name, Vec3& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (!isNumberArray(*j, 3)) return skip(name);
        v.x = (*j)[0].get<float>();
        v.y = (*j)[1].get<float>();
        v.z = (*j)[2].get<float>();
        return apply(name);
    }

    bool visit(const char* name, Color& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (!isNumberArray(*j, 3)) return skip(name);
        v.r = (*j)[0].get<float>();
        v.g = (*j)[1].get<float>();
        v.b = (*j)[2].get<float>();
        if (j->size() >= 4 && (*j)[3].is_number()) v.a = (*j)[3].get<float>();
        return apply(name);
    }

private:
    const Json& src_;
    std::set<std::string> seen_;   // member names encountered while reflecting

    const Json* find(const char* name) {
        seen_.insert(name);
        if (!src_.is_object()) return nullptr;
        auto it = src_.find(name);
        return it == src_.end() ? nullptr : &*it;
    }

    static bool isNumberArray(const Json& j, size_t minSize) {
        if (!j.is_array() || j.size() < minSize) return false;
        for (size_t i = 0; i < minSize; i++) {
            if (!j[i].is_number()) return false;
        }
        return true;
    }

    bool apply(const char* name) { applied.push_back(name); return true; }
    bool skip(const char* name) { skipped.push_back(name); return false; }
};

// ---------------------------------------------------------------------------
// Convenience wrappers
// ---------------------------------------------------------------------------

// All reflected members of obj as a Json object.
template <class T>
inline Json reflectToJson(T& obj) {
    JsonWriteReflector w;
    obj.reflectMembers(w);
    return w.members;
}

// Apply the keys of a Json object onto obj's reflected members. Returns the
// reflector so callers can inspect applied / skipped / unknownKeys().
template <class T>
inline JsonReadReflector reflectFromJson(T& obj, const Json& j) {
    JsonReadReflector r(j);
    obj.reflectMembers(r);
    return r;
}

} // namespace trussc
