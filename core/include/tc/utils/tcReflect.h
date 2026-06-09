#pragma once

// =============================================================================
// tcReflect.h - explicit member reflection (TC_REFLECT)
//
// A type exposes editable members for inspectors / serialization by listing
// them. Node is the reflection root; subclasses chain automatically with
// `using Super = Base;`:
//
//     struct Sprite : Node {
//         using Super = Node;          // inherits Node's reflected members
//         Color color;
//         float radius = 30;
//         TC_REFLECT(Sprite)
//             TC_FIELD(color)          // plain field, edited in place
//             TC_FIELD(radius)
//         TC_REFLECT_END
//     };
//
// Backends implement Reflector (one typed visit() per supported type) — e.g.
// an ImGui inspector, a JSON writer, a binary codec. Plain fields use
// TC_FIELD; getter/setter pairs use TC_PROPERTY so invariants (dirty flags,
// clamping, events) run on edit.
// =============================================================================

#include <string>

namespace trussc {

// Reflected types use references to these (declared in tcMath.h / tcColor.h).
struct Vec2;
struct Vec3;
struct Color;

// Visitor over a type's members. One overload per supported type; each returns
// true if the value was edited this call.
struct Reflector {
    virtual ~Reflector() = default;
    virtual bool visit(const char* name, float& v) = 0;
    virtual bool visit(const char* name, int& v) = 0;
    virtual bool visit(const char* name, bool& v) = 0;
    virtual bool visit(const char* name, std::string& v) = 0;
    virtual bool visit(const char* name, Vec2& v) = 0;
    virtual bool visit(const char* name, Vec3& v) = 0;
    virtual bool visit(const char* name, Color& v) = 0;
};

// Chain to the base's members if the type declares `using Super = Base;`.
// Qualified call -> non-virtual, so no infinite recursion. No Super -> no-op.
template <class T>
inline void reflectSuper(T* self, Reflector& r) {
    if constexpr (requires { typename T::Super; }) {
        using S = typename T::Super;
        self->S::reflectMembers(r);
    }
}

} // namespace trussc

// Open the member-enumeration function. TC_REFLECT_ROOT declares the virtual
// (use on the base, e.g. Node); TC_REFLECT overrides it (use on subclasses).
// Between open and TC_REFLECT_END, list members with TC_FIELD / TC_PROPERTY.
#define TC_REFLECT_ROOT(Self) \
    virtual void reflectMembers(::trussc::Reflector& r) { \
        ::trussc::reflectSuper<Self>(this, r);
#define TC_REFLECT(Self) \
    void reflectMembers(::trussc::Reflector& r) override { \
        ::trussc::reflectSuper<Self>(this, r);
#define TC_REFLECT_END }

// A plain data member: edited in place.
#define TC_FIELD(member) r.visit(#member, member);

// A getter/setter property: read via getter; on edit, write via setter so any
// invariants (dirty flags, clamping, change events) run.
#define TC_PROPERTY(name, getter, setter) \
    do { auto _tcv = getter(); if (r.visit(#name, _tcv)) setter(_tcv); } while (0);
