#pragma once

// =============================================================================
// example-serialize - reflect once, get the inspector AND JSON save/load
//
// One TC_REFLECT block per type is all it takes. From that single declaration:
//   * the node inspector (F1) edits every field live, and
//   * reflectToJson / JsonReadReflector save and restore the node as JSON,
// with no per-field code on either side.
//
// SpriteNode shows all three TC_VALUE forms, an enum (TC_ENUM_LABELS), and a
// plain struct reflected from the outside (TC_REFLECT_FREE). The composite
// shows up as a collapsible group in the inspector and as a nested object in
// the JSON — the same grouping, two backends.
//
//   F1            toggle the inspector (click the sprite to select it)
//   S             snapshot the sprite to JSON (logged + kept in memory)
//   L             restore the sprite from the last snapshot
// =============================================================================

#include <TrussC.h>
#include <tcxImGui.h>
#include <tcxNodeInspector.h>
using namespace std;
using namespace tc;
using namespace tcx;

// An enum with human-readable labels (combo box in the inspector, label string
// in the JSON).
enum class Shape { Circle, Square, Triangle };
TC_ENUM_LABELS(Shape, "Circle", "Square", "Triangle")

// A plain value type, reflected from the outside with TC_REFLECT_FREE. Used as
// a field below, it nests into its own collapsible group / JSON object.
struct Style {
    Color fill   = colors::cornflowerBlue;
    Color stroke = colors::white;
    float strokeWidth = 2.0f;
};
TC_REFLECT_FREE(Style) {
    TC_VALUE(fill)
    TC_VALUE(stroke)
    TC_VALUE(strokeWidth)
}

// A node that reflects its own members on top of Node's transform.
class SpriteNode : public Node {
public:
    Shape shape = Shape::Circle;   // 1-arg enum
    Style style;                   // nested TC_REFLECT_FREE composite

    SpriteNode() { enableEvents(); }   // make it clickable on the canvas

    float getRadius() const { return radius_; }
    void  setRadius(float r) { radius_ = max(4.0f, r); }   // setter clamps
    float diameter() const { return radius_ * 2.0f; }      // computed, read-only

    void draw() override {
        const float r = radius_;
        setColor(style.fill);   fill();   drawShape(r);
        setColor(style.stroke); noFill(); drawShape(r);
    }

    bool hitTest(const Ray& localRay, float& outDistance) override {
        float t; Vec3 hp;
        if (!localRay.intersectZPlane(t, hp)) return false;
        if (hp.x * hp.x + hp.y * hp.y <= radius_ * radius_) { outDistance = t; return true; }
        return false;
    }

    TC_REFLECT(SpriteNode, Node) {              // pos / rotation / scale come from Node
        TC_VALUE(shape)                         // enum     -> combo / label
        TC_VALUE(radius, getRadius, setRadius)  // 3-arg    -> setter clamps
        TC_VALUE(diameter, diameter)            // 2-arg    -> read-only (greyed)
        TC_VALUE(style)                         // composite-> collapsible group
    }

private:
    float radius_ = 80.0f;

    void drawShape(float r) {
        switch (shape) {
            case Shape::Circle:   drawCircle(0, 0, r); break;
            case Shape::Square:   drawRect(-r, -r, r * 2, r * 2); break;
            case Shape::Triangle: drawTriangle(0, -r, r, r, -r, r); break;
        }
    }
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    shared_ptr<RectNode>   sceneRoot_;
    shared_ptr<SpriteNode> sprite_;
    Json   snapshot_;          // last saved state (S), restored by L
    string status_ = "press S to snapshot, L to restore, F1 for inspector";
};
