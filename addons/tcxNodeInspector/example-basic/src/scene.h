#pragma once

#include <TrussC.h>
#include <string>
using namespace std;
using namespace tc;

// =============================================================================
// Demo node types. Node is the reflection root (it reflects pos / rotation /
// scale / visible / active), so a subclass only adds `using Super = Node;` plus
// its own TC_FIELDs. Each VisualNode is also clickable on the canvas: it
// enables events and provides a circle hit-test, so a canvas click selects it —
// the same selection the inspector tree drives.
// =============================================================================

// A circle. Reflects a color, a radius and a filled flag (on top of Node's
// transform), and picks via a circle hit shape.
struct VisualNode : public Node {
    using Super = Node;
    Color color{0.35f, 0.62f, 0.92f, 1.0f};
    float radius = 30.0f;
    bool  filled = true;

    VisualNode() { enableEvents(); }

    void draw() override {
        setColor(color);
        if (filled) fill(); else noFill();
        drawCircle(0, 0, radius);
    }

    bool hitTest(const Ray& localRay, float& outDistance) override {
        float t; Vec3 hp;
        if (!localRay.intersectZPlane(t, hp)) return false;
        if (hp.x * hp.x + hp.y * hp.y <= radius * radius) { outDistance = t; return true; }
        return false;
    }

    TC_REFLECT(VisualNode)         // pos / rotation / scale / ... come from Node
        TC_FIELD(color)
        TC_FIELD(radius)
        TC_FIELD(filled)
    TC_REFLECT_END
};

// Two levels deep: inherits Node + VisualNode members, adds a string + int.
struct LabeledNode : public VisualNode {
    using Super = VisualNode;
    string text = "label";
    int    fontSize = 12;

    void draw() override {
        VisualNode::draw();
        setColor(1, 1, 1);
        setTextAlign(Center, Center);
        drawBitmapString(text, 0, 0);
    }

    TC_REFLECT(LabeledNode)
        TC_FIELD(text)
        TC_FIELD(fontSize)
    TC_REFLECT_END
};

// Plain group (no extra fields) — still reflects Node's transform, and shows up
// in the tree as a parent you can collapse / expand.
struct GroupNode : public Node {
    using Super = Node;
    TC_REFLECT(GroupNode)
    TC_REFLECT_END
};

// =============================================================================
// 3D space: a camera-scope node. Children are drawn between cam.begin()/end(),
// so they are stamped with the EasyCam's CameraContext and picked through it —
// click a cube and the same selection / gizmo / inspector machinery works.
// Camera input needs SHIFT held (setDragModifier), so the plain mouse stays
// free for picking; the camera consumes the gestures it claims, so a
// shift+drag never also selects a node.
// =============================================================================
struct Space3D : public Node {
    using Super = Node;
    EasyCam cam;

    void setup() override {
        cam.setDistance(800);
        cam.setElevation(0.5f);
        cam.setAzimuth(0.5f);
        cam.setDragModifier(EasyCam::Modifier::Shift);   // shift+drag / shift+scroll = camera
        cam.enableMouseInput();
    }

    void beginDraw() override { cam.begin(); }
    void endDraw() override   { cam.end(); }

    // Floor grid on the XZ plane (world origin, Y-up)
    void draw() override {
        setColor(0.30f, 0.33f, 0.38f);
        const float half = 300.0f;
        const int   div  = 10;
        const float step = (half * 2) / div;
        for (int i = 0; i <= div; ++i) {
            float p = -half + i * step;
            drawLine(p, 0, -half, p, 0, half);
            drawLine(-half, 0, p, half, 0, p);
        }
    }

    TC_REFLECT(Space3D)
    TC_REFLECT_END
};

// A cube standing on the floor. Exact ray-vs-box picking (slab test) in local
// space — the incoming ray was already unprojected through the EasyCam and
// transformed by this node's matrix, so the test itself stays simple.
struct CubeNode : public Node {
    using Super = Node;
    Color color{0.8f, 0.8f, 0.8f, 1.0f};
    float size = 80.0f;

    CubeNode() { enableEvents(); }

    void draw() override {
        setColor(color);
        fill();
        drawBox(0, 0, 0, size);
    }

    bool hitTest(const Ray& r, float& outDistance) override {
        const float h = size * 0.5f;
        float tmin = -1e30f, tmax = 1e30f;
        const float o[3] = {r.origin.x, r.origin.y, r.origin.z};
        const float d[3] = {r.direction.x, r.direction.y, r.direction.z};
        for (int i = 0; i < 3; ++i) {
            if (fabsf(d[i]) < 1e-8f) {
                if (o[i] < -h || o[i] > h) return false;
            } else {
                float t1 = (-h - o[i]) / d[i];
                float t2 = ( h - o[i]) / d[i];
                if (t1 > t2) swap(t1, t2);
                tmin = max(tmin, t1);
                tmax = min(tmax, t2);
                if (tmin > tmax) return false;
            }
        }
        if (tmax < 0) return false;
        outDistance = (tmin >= 0) ? tmin : tmax;
        return true;
    }

    TC_REFLECT(CubeNode)
        TC_FIELD(color)
        TC_FIELD(size)
    TC_REFLECT_END
};

// A demo Mod: draws a ring around its owner (mod draw), and reacts to keys when
// its owner is selected (mod input). Press X to remove the mod from itself.
// Its members are reflected (using Super = Mod;), so the inspector shows an
// "OutlineMod" section under the owner's members and the MCP dump carries them.
struct OutlineMod : public Mod {
    using Super = Mod;
    Color color{1.0f, 0.85f, 0.2f, 1.0f};
    float gap = 6.0f;

    void draw() override {
        float r = 30.0f;
        if (auto* v = dynamic_cast<VisualNode*>(getOwner())) r = v->radius;
        noFill();
        setColor(color);
        drawCircle(0, 0, r + gap);
    }
    bool onKeyPress(const KeyEventArgs& e) override {
        if (e.key == 'X' || e.key == 'x') removeSelf();   // self-remove mid-handler (deferred)
        return false;
    }

    TC_REFLECT(OutlineMod)
        TC_FIELD(color)
        TC_FIELD(gap)
    TC_REFLECT_END
};

// A flat colored rectangle. Size comes reflected from RectNode; rect-based hit
// testing makes overlapping rects pick FRONTMOST-first, which is what makes
// the inspector's "To front" / "To back" operations meaningful to play with.
struct ColorRect : public RectNode {
    using Super = RectNode;
    Color color{0.5f, 0.5f, 0.5f, 1.0f};

    ColorRect() {
        enableEvents();
        setSize(90, 90);
    }

    void draw() override {
        setColor(color);
        fill();
        drawRect(0, 0, getWidth(), getHeight());
    }

    TC_REFLECT(ColorRect)
        TC_FIELD(color)
    TC_REFLECT_END
};

// =============================================================================
// One demo scene wired to show every feature at once:
// - a 3D space (EasyCam, shift+drag to orbit) with cubes on a floor grid
// - 2D UI nodes drawn over it (default screen camera, one pushed off z=0)
// - both worlds share selection / picking / gizmo / inspector — each node is
//   picked through the camera it was drawn under
// =============================================================================
inline void buildDemo(Node& parent) {
    // --- 3D space (added first = drawn behind the 2D UI) -------------------
    auto space = make_shared<Space3D>();
    space->setName("space");
    parent.addChild(space);

    const struct { float x, z; Color c; } cubes[] = {
        {-150, -60, Color(0.85f, 0.35f, 0.30f)},
        {  40, 100, Color(0.30f, 0.65f, 0.85f)},
        { 170, -90, Color(0.55f, 0.80f, 0.40f)},
    };
    for (auto& def : cubes) {
        auto cube = make_shared<CubeNode>();
        cube->color = def.c;
        cube->setPos(def.x, cube->size * 0.5f, def.z);   // standing on the floor (Y-up)
        space->addChild(cube);
    }
    // Tilt the blue cube with a COMPOUND rotation: the gizmo handles / rings
    // follow its local frame, and the rendered pose must match them exactly.
    space->getChildren()[1]->setEuler(0.35f, 0.5f, 0.15f);

    // --- 2D UI layer (default screen camera) -------------------------------
    auto ui = make_shared<GroupNode>();
    ui->setName("ui");
    parent.addChild(ui);

    // A cyan dot with the OutlineMod (ring + key handling).
    auto dot = make_shared<VisualNode>();
    dot->setName("dot");
    dot->setPos(220, 510);
    dot->addMod<OutlineMod>();
    ui->addChild(dot);

    // An orange, larger, outlined circle — pushed back off the z=0 plane.
    // Under the default perspective screen it renders displaced toward the
    // screen center and smaller; selection (canvas click), the gizmo and the
    // inspector all still land on it (per-node camera context).
    auto orb = make_shared<VisualNode>();
    orb->setName("orb");
    orb->setPos(400, 510, -250);
    orb->color  = Color(0.95f, 0.55f, 0.25f);
    orb->radius = 46;
    orb->filled = false;
    ui->addChild(orb);

    // A nested group, to show hierarchy depth in the tree.
    auto cluster = make_shared<GroupNode>();
    cluster->setName("cluster");
    cluster->setPos(640, 510);
    ui->addChild(cluster);
    for (int i = 0; i < 3; ++i) {
        auto c = make_shared<VisualNode>();
        c->setPos((i - 1) * 64.0f, 0);
        c->radius = 18;
        c->color  = Color::fromHSB(0.55f + i * 0.08f, 0.5f, 0.95f);
        cluster->addChild(c);
    }

    // A labeled node (string + int fields on top of the circle).
    auto title = make_shared<LabeledNode>();
    title->setName("title");
    title->setPos(505, 510);
    title->text     = "hello";
    title->radius   = 26;
    title->color    = Color(0.6f, 0.4f, 0.85f);
    ui->addChild(title);

    // Overlapping rectangles (bottom right): later children draw on top, and
    // clicking picks the frontmost one — so the inspector's "To front" /
    // "To back" operations visibly reorder the stack.
    auto stack = make_shared<GroupNode>();
    stack->setName("stack");
    stack->setPos(790, 420);
    ui->addChild(stack);
    const Color rectColors[] = {
        Color(0.85f, 0.45f, 0.35f), Color(0.40f, 0.70f, 0.50f), Color(0.45f, 0.55f, 0.85f)};
    for (int i = 0; i < 3; ++i) {
        auto r = make_shared<ColorRect>();
        r->color = rectColors[i];
        r->setPos(i * 38.0f, i * 30.0f);
        stack->addChild(r);
    }
}
