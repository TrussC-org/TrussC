#pragma once

// =============================================================================
// reflectExample - a guided tour of TrussC reflection (TC_REFLECT / TC_VALUE)
//
// Reflection lets a type list its values ONCE. That single declaration then
// powers JSON save/load, the node inspector, and MCP tooling for free — you
// never write per-field serialization code. This example declares a few
// reflected types, then self-tests the whole round-trip at startup.
//
// The three building blocks:
//   TC_ENUM_LABELS(Enum, ...)   - optional human-readable names for an enum
//   TC_REFLECT_FREE(Type) {...} - reflect a plain / external struct from outside
//   TC_REFLECT_ROOT(Self) {...} - a type that reflects its own members
//                                 (a Node/Mod subclass uses TC_REFLECT(Self, Node))
//
// Inside a reflect block, TC_VALUE has three forms, picked by argument count:
//   TC_VALUE(member)                 1-arg: read + write the member in place
//   TC_VALUE(name, getter)           2-arg: read-only (no setter -> greyed out)
//   TC_VALUE(name, getter, setter)   3-arg: write through the setter (runs
//                                           clamping / dirty flags / events)
// =============================================================================

#include <TrussC.h>
using namespace std;
using namespace tc;

// -----------------------------------------------------------------------------
// 1. An enum. TC_ENUM_LABELS is optional: with it, the value saves as "Square"
//    instead of 1, and inspectors show a combo box.
// -----------------------------------------------------------------------------
enum class Shape { Circle, Square, Triangle };
TC_ENUM_LABELS(Shape, "Circle", "Square", "Triangle")

// -----------------------------------------------------------------------------
// 2. A plain struct with no reflect block of its own. Reflect it from the
//    outside with TC_REFLECT_FREE at namespace scope — the SAME TC_VALUE
//    spelling works here as inside a member block. Use this for your own value
//    types, or for third-party types you can't edit.
// -----------------------------------------------------------------------------
struct Transform {
    Vec2  position{220, 260};
    float rotation = 0;   // radians (TAU = one full turn)
    float scale = 1;
};
TC_REFLECT_FREE(Transform) {
    TC_VALUE(position)
    TC_VALUE(rotation)
    TC_VALUE(scale)
}

// -----------------------------------------------------------------------------
// 3. A type that reflects its own members. It shows all three TC_VALUE forms,
//    an enum, and a nested composite (the Transform above) — which serializes
//    into its own nested JSON object automatically.
// -----------------------------------------------------------------------------
struct Sprite {
    virtual ~Sprite() = default;

    Color     color = colors::cornflowerBlue;  // 1-arg: edited in place
    Shape     shape = Shape::Circle;           // enum: saved as a label
    Transform transform;                       // nested TC_REFLECT_FREE composite

    float getRadius() const { return radius_; }
    void  setRadius(float r) { radius_ = max(1.0f, r); }  // setter clamps to >= 1
    float diameter() const { return radius_ * 2.0f; }     // computed, read-only

    TC_REFLECT_ROOT(Sprite) {
        TC_VALUE(color)                         // 1-arg -> read+write in place
        TC_VALUE(shape)                         // enum  -> label string
        TC_VALUE(transform)                     // nested -> its own JSON object
        TC_VALUE(radius, getRadius, setRadius)  // 3-arg -> setter runs (clamps)
        TC_VALUE(diameter, diameter)            // 2-arg -> read-only
    }

private:
    float radius_ = 60;
};

// -----------------------------------------------------------------------------
// The app: runs the save/edit/load self-test once at setup, then draws the live
// sprite (rendered straight from its reflected data) next to the JSON it
// produced and a pass/fail report.
// -----------------------------------------------------------------------------
class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    Sprite sprite_;                  // the original, drawn on the left
    Sprite loaded_;                  // rebuilt from edited JSON, drawn on the right
    string savedJson_, loadedJson_;  // dumps shown on screen
    vector<string> report_;          // per-check pass/fail lines
    bool allPassed_ = false;

    void runSelfTest();
};
