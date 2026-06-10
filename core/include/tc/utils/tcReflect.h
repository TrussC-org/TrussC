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
//         BlendMode blend = BlendMode::Alpha;
//         TC_REFLECT(Sprite)
//             TC_FIELD(color)          // plain field, edited in place
//             TC_FIELD(radius)
//             TC_ENUM(blend)           // enum field (labels via TC_ENUM_LABELS)
//         TC_REFLECT_END
//     };
//
// Backends implement Reflector (one typed visit() per supported type) — e.g.
// an ImGui inspector, a JSON writer, a binary codec. Plain fields use
// TC_FIELD; getter/setter pairs use TC_PROPERTY so invariants (dirty flags,
// clamping, events) run on edit; getter-only values use TC_PROPERTY_RO.
//
// Enums travel through one extra visit() channel as an int plus a label table
// (index == enum value), declared once per enum type next to the enum:
//
//     TC_ENUM_LABELS(BlendMode, "Alpha", "Add", "Multiply", ...)
//
// The labels are found by ADL, so invoke TC_ENUM_LABELS in the enum's own
// namespace. The default enum visit() falls back to the plain int visit, so
// backends that don't know about enums keep working unchanged.
// =============================================================================

#include <string>
#include <type_traits>

namespace trussc {

// Reflected types use references to these (declared in tcMath.h / tcColor.h).
struct Vec2;
struct Vec3;
struct Color;

// Label table for one enum type: labels[value] is the human-readable name.
struct EnumLabelSpan {
    const char* const* labels = nullptr;
    int count = 0;
};

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

    // Enum channel: the value as an int plus its label table. Backends that
    // understand enums override this (combo box, string encoding, ...); the
    // default falls back to the plain int visit.
    virtual bool visit(const char* name, int& v, const EnumLabelSpan& labels) {
        (void)labels;
        return visit(name, v);
    }

    // Read-only scope — TC_PROPERTY_RO visits inside push/pop. Backends check
    // isReadOnly() to render disabled / refuse writes; edits are discarded
    // regardless (there is no setter to receive them).
    bool isReadOnly() const { return readOnlyDepth_ > 0; }
    void pushReadOnly() { ++readOnlyDepth_; }
    void popReadOnly() { if (readOnlyDepth_ > 0) --readOnlyDepth_; }

private:
    int readOnlyDepth_ = 0;
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

// Declare the label table for an enum type. Invoke at namespace scope in the
// enum's own namespace (found by ADL from TC_ENUM / TC_ENUM_PROPERTY).
// Labels are listed in declaration order: labels[(int)value] == name.
#define TC_ENUM_LABELS(EnumType, ...) \
    inline ::trussc::EnumLabelSpan tcEnumLabelsAdl(EnumType) { \
        static constexpr const char* labels_[] = {__VA_ARGS__}; \
        return { labels_, static_cast<int>(sizeof(labels_) / sizeof(labels_[0])) }; \
    }

// Open the member-enumeration function. TC_REFLECT_ROOT declares the virtual
// (use on the base, e.g. Node); TC_REFLECT overrides it (use on subclasses).
// Between open and TC_REFLECT_END, list members with TC_FIELD / TC_PROPERTY /
// TC_PROPERTY_RO / TC_ENUM / TC_ENUM_PROPERTY.
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

// A getter-only property: shown but not editable (runtime state like a tween's
// progress). Backends see isReadOnly() == true while visiting.
#define TC_PROPERTY_RO(name, getter) \
    do { auto _tcv = getter(); r.pushReadOnly(); r.visit(#name, _tcv); r.popReadOnly(); } while (0);

// An enum data member: travels as int + label table (TC_ENUM_LABELS).
#define TC_ENUM(member) \
    do { using _tcE = std::decay_t<decltype(member)>; \
         int _tci = static_cast<int>(member); \
         if (r.visit(#member, _tci, tcEnumLabelsAdl(_tcE{}))) \
             member = static_cast<_tcE>(_tci); } while (0);

// An enum getter/setter property.
#define TC_ENUM_PROPERTY(name, getter, setter) \
    do { auto _tce = getter(); using _tcE = decltype(_tce); \
         int _tci = static_cast<int>(_tce); \
         if (r.visit(#name, _tci, tcEnumLabelsAdl(_tcE{}))) \
             setter(static_cast<_tcE>(_tci)); } while (0);
