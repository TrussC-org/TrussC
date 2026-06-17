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

    bool visit(const char* name, float& v) override { cur()[name] = v; return false; }
    bool visit(const char* name, int& v) override { cur()[name] = v; return false; }
    bool visit(const char* name, bool& v) override { cur()[name] = v; return false; }
    bool visit(const char* name, std::string& v) override { cur()[name] = v; return false; }
    bool visit(const char* name, Vec2& v) override { cur()[name] = Json::array({v.x, v.y}); return false; }
    bool visit(const char* name, Vec3& v) override { cur()[name] = Json::array({v.x, v.y, v.z}); return false; }
    bool visit(const char* name, Color& v) override { cur()[name] = Json::array({v.r, v.g, v.b, v.a}); return false; }

    // Enums encode as their label string (readable dumps); out-of-range values
    // fall back to the raw int.
    bool visit(const char* name, int& v, const EnumLabelSpan& labels) override {
        if (labels.labels && v >= 0 && v < labels.count) {
            cur()[name] = labels.labels[v];
        } else {
            cur()[name] = v;
        }
        return false;
    }

    // A composite member nests into its own object. Subsequent visits land in
    // the child until endGroup(). We only ever add keys to the deepest open
    // object, so the parent pointers on the stack stay valid.
    void beginGroup(const char* name) override {
        Json& child = (cur()[name] = Json::object());
        stack_.push_back(&child);
    }
    void endGroup() override {
        if (!stack_.empty()) stack_.pop_back();
    }

private:
    std::vector<Json*> stack_;
    Json& cur() { return stack_.empty() ? members : *stack_.back(); }
};

// ---------------------------------------------------------------------------
// JsonReadReflector - Json object -> reflected members (through setters)
// ---------------------------------------------------------------------------
class JsonReadReflector : public Reflector {
public:
    // Holds a copy of the source object (member sets are small) — a reference
    // would dangle when constructed from a temporary, e.g.
    // `JsonReadReflector r(Json{{"pos", ...}});`.
    explicit JsonReadReflector(Json src) : src_(std::move(src)) {}

    std::vector<std::string> applied;   // keys that matched a member and were written
    std::vector<std::string> skipped;   // keys that matched a member but had the wrong JSON shape
    std::vector<std::string> readOnly;  // keys that matched a read-only member (not written)

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
        if (isReadOnly()) return ro(name);
        if (!j->is_number()) return skip(name);
        v = j->get<float>();
        return apply(name);
    }

    bool visit(const char* name, int& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (isReadOnly()) return ro(name);
        if (!j->is_number()) return skip(name);
        v = j->get<int>();
        return apply(name);
    }

    bool visit(const char* name, bool& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (isReadOnly()) return ro(name);
        if (!j->is_boolean()) return skip(name);
        v = j->get<bool>();
        return apply(name);
    }

    bool visit(const char* name, std::string& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (isReadOnly()) return ro(name);
        if (!j->is_string()) return skip(name);
        v = j->get<std::string>();
        return apply(name);
    }

    bool visit(const char* name, Vec2& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (isReadOnly()) return ro(name);
        if (!isNumberArray(*j, 2)) return skip(name);
        v.x = (*j)[0].get<float>();
        v.y = (*j)[1].get<float>();
        return apply(name);
    }

    bool visit(const char* name, Vec3& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (isReadOnly()) return ro(name);
        if (!isNumberArray(*j, 3)) return skip(name);
        v.x = (*j)[0].get<float>();
        v.y = (*j)[1].get<float>();
        v.z = (*j)[2].get<float>();
        return apply(name);
    }

    bool visit(const char* name, Color& v) override {
        const Json* j = find(name);
        if (!j) return false;
        if (isReadOnly()) return ro(name);
        if (!isNumberArray(*j, 3)) return skip(name);
        v.r = (*j)[0].get<float>();
        v.g = (*j)[1].get<float>();
        v.b = (*j)[2].get<float>();
        if (j->size() >= 4 && (*j)[3].is_number()) v.a = (*j)[3].get<float>();
        return apply(name);
    }

    // Enums accept the label string ("Fill") or the raw int index.
    bool visit(const char* name, int& v, const EnumLabelSpan& labels) override {
        const Json* j = find(name);
        if (!j) return false;
        if (isReadOnly()) return ro(name);
        if (j->is_string()) {
            const std::string s = j->get<std::string>();
            for (int i = 0; i < labels.count; i++) {
                if (s == labels.labels[i]) { v = i; return apply(name); }
            }
            return skip(name);
        }
        if (j->is_number_integer()) {
            int iv = j->get<int>();
            if (iv >= 0 && iv < labels.count) { v = iv; return apply(name); }
        }
        return skip(name);
    }

    // A composite member reads from its nested object. If the key is absent or
    // not an object, the child is null and its members simply find nothing.
    void beginGroup(const char* name) override {
        const Json* s = curSrc();
        const Json* child = nullptr;
        if (s && s->is_object()) {
            auto it = s->find(name);
            if (it != s->end() && it->is_object()) child = &*it;
        }
        srcStack_.push_back(child);
    }
    void endGroup() override {
        if (!srcStack_.empty()) srcStack_.pop_back();
    }

private:
    Json src_;
    std::set<std::string> seen_;          // top-level member names encountered
    std::vector<const Json*> srcStack_;   // nested source objects (null = absent)

    const Json* curSrc() const {
        return srcStack_.empty() ? &src_ : srcStack_.back();
    }

    const Json* find(const char* name) {
        if (srcStack_.empty()) seen_.insert(name);  // unknownKeys is top-level
        const Json* s = curSrc();
        if (!s || !s->is_object()) return nullptr;
        auto it = s->find(name);
        return it == s->end() ? nullptr : &*it;
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
    bool ro(const char* name) { readOnly.push_back(name); return false; }
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
