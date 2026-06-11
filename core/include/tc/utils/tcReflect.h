#pragma once

// =============================================================================
// tcReflect.h - explicit member reflection (TC_REFLECT / TC_VALUE)
//
// A type exposes values for inspectors / serialization by listing them in a
// TC_REFLECT block — a normal function body in disguise, so the braces are
// real braces:
//
//     enum class WeaponType { Sword, Bow, Staff };
//     TC_ENUM_LABELS(WeaponType, "Sword", "Bow", "Staff")   // optional
//
//     struct Enemy : Node {
//         float hp = 100;
//         WeaponType weapon = WeaponType::Sword;
//         bool isAlive() const { return hp > 0; }
//
//         TC_REFLECT(Enemy, Node) {                  // list the DIRECT bases
//             TC_VALUE(hp)                           // member, edited in place
//             TC_VALUE(weapon)                       // enums auto-detected
//             TC_VALUE(alive, isAlive)               // getter only = read-only
//             TC_VALUE(speed, getSpeed, setSpeed)    // setter runs on edit
//         }
//     };
//
// TC_VALUE arity is the access mode:
//     TC_VALUE(member)                // a data member, edited in place
//     TC_VALUE(name, getter)          // read-only: no setter exists, so it
//                                     // can't be written — shown greyed out,
//                                     // write tools refuse it
//     TC_VALUE(name, getter, setter)  // editable: read via getter, write via
//                                     // setter so invariants (dirty flags,
//                                     // clamping, events) run
//
// Bases chain recursively: each class lists only its DIRECT bases, and gets
// the whole ancestor chain through them (multiple direct bases are all
// chained). TC_REFLECT(Self) with no bases chains nothing. Reflection roots
// (Node, Mod, or a standalone mixin) use TC_REFLECT_ROOT(Self) instead, which
// declares the virtual.
//
// Enums work in any TC_VALUE (they travel as an int). To get human-readable
// presentation — a combo box in inspectors, label strings in JSON — declare
// the label table once, next to the enum:
//
//     TC_ENUM_LABELS(BlendMode, "Alpha", "Add", "Multiply", ...)
//
// The labels are found by ADL, so invoke TC_ENUM_LABELS in the enum's own
// namespace. The default enum visit() falls back to the plain int visit, so
// backends that don't know about enums keep working unchanged.
//
// Backends implement Reflector (one typed visit() per supported type) — e.g.
// an ImGui inspector, a JSON writer, a binary codec.
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

// Visitor over a type's values. One overload per supported type; each returns
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

    // Read-only scope — the 2-arg TC_VALUE visits inside push/pop. Backends
    // check isReadOnly() to render disabled / refuse writes; edits are
    // discarded regardless (there is no setter to receive them).
    bool isReadOnly() const { return readOnlyDepth_ > 0; }
    void pushReadOnly() { ++readOnlyDepth_; }
    void popReadOnly() { if (readOnlyDepth_ > 0) --readOnlyDepth_; }

private:
    int readOnlyDepth_ = 0;
};

// Chain the listed direct bases' reflect blocks, in order. Qualified calls ->
// non-virtual, so each base contributes exactly its own chain (no recursion
// through the vtable). The single-base helper exists because clang refuses a
// pack used as the nested-name-specifier of a member access inside a fold.
template <class Base, class T>
inline void reflectOneBase(T* self, Reflector& r) {
    self->Base::reflectMembers(r);
}
template <class... Bases, class T>
inline void reflectBases(T* self, Reflector& r) {
    (reflectOneBase<Bases>(self, r), ...);
}

// Visit one value of any reflectable type — the single funnel TC_VALUE goes
// through. Enums are detected here: they travel as an int, through the
// labeled enum channel when the enum has a TC_ENUM_LABELS declaration (found
// by ADL), as a plain int otherwise. Being a template matters: if constexpr
// discards the non-matching branch without type-checking it.
template <class T>
inline bool reflectValue(Reflector& r, const char* name, T& v) {
    if constexpr (std::is_enum_v<T>) {
        int i = static_cast<int>(v);
        bool edited;
        if constexpr (requires { tcEnumLabelsAdl(T{}); }) {
            edited = r.visit(name, i, tcEnumLabelsAdl(T{}));
        } else {
            edited = r.visit(name, i);
        }
        if (edited) v = static_cast<T>(i);
        return edited;
    } else {
        return r.visit(name, v);
    }
}

} // namespace trussc

// Declare the label table for an enum type. Invoke at namespace scope in the
// enum's own namespace (found by ADL from TC_VALUE). Labels are listed in
// declaration order: labels[(int)value] == name.
#define TC_ENUM_LABELS(EnumType, ...) \
    inline ::trussc::EnumLabelSpan tcEnumLabelsAdl(EnumType) { \
        static constexpr const char* labels_[] = {__VA_ARGS__}; \
        return { labels_, static_cast<int>(sizeof(labels_) / sizeof(labels_[0])) }; \
    }

// Open a reflect block. Both expand to reflectMembers() (chains the bases,
// then this class's own values) plus the declaration of reflectOwnMembers();
// the block braces the user writes right after become its body:
//
//     TC_REFLECT(Self, DirectBases...) { TC_VALUE(...) ... }
//     TC_REFLECT_ROOT(Self)            { TC_VALUE(...) ... }   // declares the virtual
//
#define TC_REFLECT_ROOT(Self) \
    virtual void reflectMembers(::trussc::Reflector& r) { reflectOwnMembers(r); } \
    void reflectOwnMembers(::trussc::Reflector& r)

#define TC_REFLECT(Self, ...) \
    void reflectMembers(::trussc::Reflector& r) override { \
        ::trussc::reflectBases<__VA_ARGS__>(this, r); \
        reflectOwnMembers(r); \
    } \
    void reflectOwnMembers(::trussc::Reflector& r)

// One reflected value (see the header comment for the arity rule). A trailing
// semicolon after the macro is harmless.
#define TC_VALUE_1_(member) ::trussc::reflectValue(r, #member, member);
#define TC_VALUE_2_(name, getter) \
    do { auto _tcv = getter(); \
         r.pushReadOnly(); \
         ::trussc::reflectValue(r, #name, _tcv); \
         r.popReadOnly(); } while (0);
#define TC_VALUE_3_(name, getter, setter) \
    do { auto _tcv = getter(); \
         if (::trussc::reflectValue(r, #name, _tcv)) setter(_tcv); } while (0);

// Arity dispatch (the extra expansion keeps MSVC's traditional preprocessor
// from passing __VA_ARGS__ through as a single token).
#define TC_VALUE_SELECT_(_1, _2, _3, NAME, ...) NAME
#define TC_VALUE_EXPAND_(x) x
#define TC_VALUE(...) \
    TC_VALUE_EXPAND_(TC_VALUE_SELECT_(__VA_ARGS__, TC_VALUE_3_, TC_VALUE_2_, TC_VALUE_1_)(__VA_ARGS__))
