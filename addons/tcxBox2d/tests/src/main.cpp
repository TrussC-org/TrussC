// =============================================================================
// tcxBox2d tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR via examples/build_all.py
// --addon-tests-only (exit 0 = pass, non-zero = fail). Console only: the world
// is stepped by hand and nothing is drawn.
//
// Polygon points (#342). PolyShape::setup() and RigidBody2D with
// Shape2D::polygon() share one check, run before the body is created:
//   - fewer than 3 or more than 8 points, collinear points, points Box2D would
//     merge, and a hull with (almost) no area give one warning and no body,
//     in Debug and Release alike (no Box2D assert, no 2x2 m fallback box);
//   - concave input with 3..8 points becomes its convex hull, and
//     getVertices() / shape().verts hold that hull, so what draws collides;
//   - setupConvex() / Shape2D::convex() take any number of points and make one
//     fixture of at most 8 points whose mass is close to the outline's.
// =============================================================================

#include <tcxBox2d.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using namespace tc;
using namespace tcx;

static int g_fail = 0;
static void check(const string& name, bool ok) {
    printf("%-72s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);  // flush each line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// Counts warnings logged while it is alive, and keeps the last one.
struct WarningCapture {
    int count = 0;
    string last;
    EventListener listener;
    WarningCapture() {
        listener = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level == LogLevel::Warning) {
                ++count;
                last = e.message;
            }
        });
    }
    bool lastContains(const string& s) const { return last.find(s) != string::npos; }
};

static int fixtureCount(const b2Body* body) {
    int n = 0;
    if (!body) return 0;
    for (const b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext()) ++n;
    return n;
}

static int polygonVertexCount(const b2Body* body) {
    if (!body || !body->GetFixtureList()) return -1;
    const b2Shape* s = body->GetFixtureList()->GetShape();
    if (s->GetType() != b2Shape::e_polygon) return -1;
    return static_cast<const b2PolygonShape*>(s)->m_count;
}

static vector<Vec2> circlePoints(int n, float r) {
    vector<Vec2> pts;
    for (int i = 0; i < n; ++i) {
        float a = TAU * i / n;
        pts.push_back(Vec2(cos(a) * r, sin(a) * r));
    }
    return pts;
}

static bool hasPoint(const vector<Vec2>& pts, float x, float y) {
    for (const auto& p : pts) {
        if (abs(p.x - x) < 1e-3f && abs(p.y - y) < 1e-3f) return true;
    }
    return false;
}

// Mass of a disc of radius r px at density 1 (Box2D units: kg per m^2).
static float discMass(float r) {
    float m = box2d::World::toBox2d(r);
    return TAU * 0.5f * m * m;
}

// ---------------------------------------------------------------------------
// Inputs that can't make a polygon
// ---------------------------------------------------------------------------
struct BadInput {
    const char* name;
    vector<Vec2> pts;
};

static vector<BadInput> badInputs() {
    vector<BadInput> v;
    v.push_back({"2 points", {{0, 0}, {10, 0}}});
    v.push_back({"3 collinear points", {{0, 0}, {10, 0}, {20, 0}}});
    v.push_back({"2 coincident points + 1", {{0, 0}, {0, 0}, {10, 10}}});
    v.push_back({"2 points closer than the weld distance + 1", {{0, 0}, {0.01f, 0}, {10, 10}}});
    // Hull of 3 points, area ~1.1e-7 m^2 < FLT_EPSILON at 30 px/m.
    v.push_back({"3 nearly collinear points (area < b2_epsilon)", {{0, 0}, {20, 0}, {10, 0.00001f}}});
    v.push_back({"9 points", circlePoints(9, 40)});
    return v;
}

static void testPolyShapeRefuses(box2d::World& world) {
    for (const auto& in : badInputs()) {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, in.pts, 400, 300);
        check(string("PolyShape::setup ") + in.name + ": no body", !poly.isCreated());
        check(string("PolyShape::setup ") + in.name + ": one warning", w.count == 1);
        check(string("PolyShape::setup ") + in.name + ": no vertices", poly.getVertices().empty());
    }

    // More than 8 points: the warning points to setupConvex().
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, circlePoints(9, 40), 400, 300);
        check("PolyShape::setup 9 points: warning names setupConvex()",
              w.lastContains("setupConvex()"));
    }

    // A 20-point circle Path: every point of the path counts, so it is refused.
    {
        Path path(circlePoints(20, 40));
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, path, 400, 300);
        check("PolyShape::setup 20-point Path: no body", !poly.isCreated());
        check("PolyShape::setup 20-point Path: one warning", w.count == 1);
    }

    check("refused setups left no bodies in the world", world.getBodyCount() == 0);
}

static void testPolyShapeValid(box2d::World& world) {
    // A convex 5-point polygon: created with one fixture, drawn as given.
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, circlePoints(5, 40), 400, 300);
        check("PolyShape::setup convex 5 points: created", poly.isCreated());
        check("PolyShape::setup convex 5 points: one fixture", fixtureCount(poly.getBody()) == 1);
        check("PolyShape::setup convex 5 points: 5 vertices", poly.getNumVertices() == 5);
        check("PolyShape::setup convex 5 points: no warning", w.count == 0);
    }

    // The notched 5-point polygon (#342 repro 3): built, drawn and colliding as
    // its 4-point square hull.
    {
        vector<Vec2> notched = {{-20, -20}, {20, -20}, {0, 0}, {20, 20}, {-20, 20}};
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, notched, 400, 300);
        const auto& v = poly.getVertices();
        check("PolyShape::setup notched: created", poly.isCreated());
        check("PolyShape::setup notched: getVertices() is the 4-point hull",
              v.size() == 4 && hasPoint(v, -20, -20) && hasPoint(v, 20, -20) &&
              hasPoint(v, 20, 20) && hasPoint(v, -20, 20) && !hasPoint(v, 0, 0));
        check("PolyShape::setup notched: Box2D polygon has 4 points",
              polygonVertexCount(poly.getBody()) == 4);
        check("PolyShape::setup notched: collider vertex count 4",
              poly.getCollider() &&
              static_cast<box2d::PolygonCollider2D*>(poly.getCollider())->getVertexCount() == 4);
        // A point inside the notch collides, since the hull filled it.
        check("PolyShape::setup notched: the notch is solid", poly.containsPoint(410, 300));
        float squareMass = box2d::World::toBox2d(40.0f) * box2d::World::toBox2d(40.0f);
        check("PolyShape::setup notched: mass of the square", abs(poly.getMass() - squareMass) < 1e-4f);
        check("PolyShape::setup notched: no warning", w.count == 0);
    }

    // setupRegular() still works.
    {
        box2d::PolyShape poly;
        poly.setupRegular(world, 400, 300, 30, 6);
        check("PolyShape::setupRegular 6 sides: created", poly.isCreated());
    }
}

static void testSetupConvex(box2d::World& world) {
    const float r = 50;
    // 20-point circle, vector and Path overloads.
    for (int usePath = 0; usePath < 2; ++usePath) {
        string tag = usePath ? "Path" : "vector";
        WarningCapture w;
        box2d::PolyShape poly;
        if (usePath) poly.setupConvex(world, Path(circlePoints(20, r)), 400, 300);
        else         poly.setupConvex(world, circlePoints(20, r), 400, 300);
        check("setupConvex 20-point circle (" + tag + "): created", poly.isCreated());
        check("setupConvex 20-point circle (" + tag + "): one fixture", fixtureCount(poly.getBody()) == 1);
        check("setupConvex 20-point circle (" + tag + "): at most 8 vertices",
              poly.getNumVertices() >= 3 && poly.getNumVertices() <= 8 &&
              polygonVertexCount(poly.getBody()) == poly.getNumVertices());
        float ratio = poly.getMass() / discMass(r);
        check("setupConvex 20-point circle (" + tag + "): mass within 15% of the disc",
              ratio > 0.85f && ratio <= 1.0f);
        check("setupConvex 20-point circle (" + tag + "): no warning", w.count == 0);
    }

    // The reduction keeps the extreme points: a long thin diamond with many
    // points along its edges keeps its 4 tips.
    {
        vector<Vec2> pts;
        Vec2 tips[4] = {{-100, 0}, {0, -10}, {100, 0}, {0, 10}};
        for (int e = 0; e < 4; ++e) {
            Vec2 a = tips[e], b = tips[(e + 1) % 4];
            for (int i = 0; i < 6; ++i) {
                float t = i / 6.0f;
                // Push the edge points slightly out so they are on the hull.
                Vec2 p = a + (b - a) * t;
                Vec2 out = p * (1.0f + 0.02f * sin(TAU * 0.5f * t));
                pts.push_back(i == 0 ? a : out);
            }
        }
        box2d::PolyShape poly;
        poly.setupConvex(world, pts, 400, 300);
        const auto& v = poly.getVertices();
        check("setupConvex 24-point diamond: at most 8 vertices", poly.isCreated() && v.size() <= 8);
        check("setupConvex 24-point diamond: keeps the 4 tips",
              hasPoint(v, -100, 0) && hasPoint(v, 0, -10) && hasPoint(v, 100, 0) && hasPoint(v, 0, 10));
    }

    // Degenerate hulls fail like setup().
    {
        vector<Vec2> line;
        for (int i = 0; i < 12; ++i) line.push_back(Vec2(i * 5.0f, i * 2.0f));
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupConvex(world, line, 400, 300);
        check("setupConvex 12 collinear points: no body", !poly.isCreated());
        check("setupConvex 12 collinear points: one warning", w.count == 1);
    }
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupConvex(world, {{0, 0}, {10, 0}}, 400, 300);
        check("setupConvex 2 points: no body", !poly.isCreated());
        check("setupConvex 2 points: one warning", w.count == 1);
    }
}

// ---------------------------------------------------------------------------
// Mod API: RigidBody2D with Shape2D::polygon() / Shape2D::convex()
// ---------------------------------------------------------------------------
static box2d::RigidBody2D* attach(box2d::World& world, shared_ptr<Node>& node, const box2d::Shape2D& shape) {
    node = make_shared<Node>();
    node->setPos(400, 300);
    return node->addMod<box2d::RigidBody2D>(world, shape);
}

static void testRigidBody2D(box2d::World& world) {
    int bodiesBefore = world.getBodyCount();
    for (const auto& in : badInputs()) {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(in.pts));
        check(string("RigidBody2D polygon ") + in.name + ": no body", rb->getBody() == nullptr);
        check(string("RigidBody2D polygon ") + in.name + ": one warning", w.count == 1);
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        attach(world, node, box2d::Shape2D::polygon(circlePoints(9, 40)));
        check("RigidBody2D polygon 9 points: warning names Shape2D::convex()",
              w.lastContains("Shape2D::convex()"));
    }
    check("RigidBody2D refused polygons left no bodies", world.getBodyCount() == bodiesBefore);

    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(circlePoints(5, 40)));
        check("RigidBody2D polygon convex 5 points: one fixture", fixtureCount(rb->getBody()) == 1);
        check("RigidBody2D polygon convex 5 points: no warning", w.count == 0);
    }
    {
        vector<Vec2> notched = {{-20, -20}, {20, -20}, {0, 0}, {20, 20}, {-20, 20}};
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(notched));
        const auto& v = rb->shape().verts;
        check("RigidBody2D polygon notched: shape() is the 4-point hull",
              rb->getBody() && v.size() == 4 && !hasPoint(v, 0, 0) &&
              polygonVertexCount(rb->getBody()) == 4);
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::convex(circlePoints(20, 50)));
        check("RigidBody2D Shape2D::convex 20 points: one fixture", fixtureCount(rb->getBody()) == 1);
        check("RigidBody2D Shape2D::convex 20 points: at most 8 vertices",
              rb->shape().verts.size() <= 8 && polygonVertexCount(rb->getBody()) <= 8);
        float ratio = rb->getBody() ? rb->getBody()->GetMass() / discMass(50) : 0.0f;
        check("RigidBody2D Shape2D::convex 20 points: mass within 15% of the disc",
              ratio > 0.85f && ratio <= 1.0f);
        check("RigidBody2D Shape2D::convex 20 points: no warning", w.count == 0);
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::convex(Path(circlePoints(20, 50))));
        check("RigidBody2D Shape2D::convex Path: created", rb->getBody() != nullptr);
    }
    {
        vector<Vec2> line;
        for (int i = 0; i < 12; ++i) line.push_back(Vec2(i * 5.0f, 0));
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::convex(line));
        check("RigidBody2D Shape2D::convex collinear: no body", rb->getBody() == nullptr);
        check("RigidBody2D Shape2D::convex collinear: one warning", w.count == 1);
    }
}

int main() {
    box2d::World world;
    world.setup(0, 0);
    world.setAutoUpdate(false);

    testPolyShapeRefuses(world);
    testPolyShapeValid(world);
    testSetupConvex(world);
    testRigidBody2D(world);

    if (g_fail) {
        printf("\n%d check(s) FAILED\n", g_fail);
        return 1;
    }
    printf("\nAll checks passed\n");
    return 0;
}
