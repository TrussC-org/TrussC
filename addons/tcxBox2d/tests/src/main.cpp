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
//     convex input already in outline order keeps its order, and hull points
//     in a crossing order come back in hull order;
//   - setupConvex() / Shape2D::convex() take any number of points and make one
//     fixture of at most 8 points whose mass is close to the outline's; the
//     reduction is O(h log h) and picks the same points as the plain
//     O(h^2) scan it replaced.
//
// Compound bodies (#427). setupCompound() / Shape2D::compound() keep any
// outline exactly, one fixture per triangle of Path::buildFillTriangles():
//   - a 20-point circle gives many fixtures and the 20-gon's mass; a convex
//     outline of at most 8 points gives exactly one fixture;
//   - concave notches and holes stay empty: a ball in the hole falls to the
//     hole's floor, a ball above the solid part lands on it;
//   - slivers are skipped with one warning; nothing usable gives no body;
//   - Collider2D filter and trigger settings reach every fixture;
//   - a compound touching a box with several fixtures at once gives exactly
//     one Enter / Began, one Stay per update, and one Exit / Ended.
// =============================================================================

#include <tcxBox2d.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
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

// z of (b - a) x (c - b), in double.
static double turn(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (double(b.x) - a.x) * (double(c.y) - b.y) - (double(b.y) - a.y) * (double(c.x) - b.x);
}

// True when v, read in order, is a simple convex polygon: every corner turns
// the same way and the edges turn exactly once around (a star-ordered
// pentagon turns the same way at every corner but goes around twice).
static bool isSimpleConvexCycle(const vector<Vec2>& v) {
    const size_t n = v.size();
    if (n < 3) return false;
    int sign = 0;
    double angle = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = v[i];
        const Vec2& b = v[(i + 1) % n];
        const Vec2& c = v[(i + 2) % n];
        double t = turn(a, b, c);
        int s = (t > 0) - (t < 0);
        if (s == 0 || (sign != 0 && s != sign)) return false;
        sign = s;
        double d = (double(b.x) - a.x) * (double(c.x) - b.x) + (double(b.y) - a.y) * (double(c.y) - b.y);
        angle += atan2(t, d);
    }
    return abs(abs(angle) - TAU) < 1e-3;
}

// Area a triangle fan from v[0] covers (how draw() / drawFill() /
// ColliderRenderer2D fill a polygon): the sum of the fan triangles' areas when
// they all face the same way (so they don't overlap), otherwise 0.
static double fanArea(const vector<Vec2>& v) {
    double area = 0.0;
    int sign = 0;
    for (size_t i = 1; i + 1 < v.size(); ++i) {
        double t = turn(v[0], v[i], v[i + 1]);
        int s = (t > 0) - (t < 0);
        if (s == 0 || (sign != 0 && s != sign)) return 0.0;
        sign = s;
        area += 0.5 * abs(t);
    }
    return area;
}

// Area of the regular n-gon circlePoints(n, r).
static double regularArea(int n, float r) {
    return 0.5 * n * double(r) * r * sin(TAU / n);
}

// Hull points in a crossing order: Box2D keeps every point, but the order
// does not go around the outline.
struct CrossingInput {
    const char* name;
    vector<Vec2> pts;
    double area;
};

static vector<CrossingInput> crossingInputs() {
    vector<CrossingInput> v;
    v.push_back({"Z-ordered square", {{-20, -20}, {-20, 20}, {20, -20}, {20, 20}}, 1600.0});
    v.push_back({"bowtie-ordered square", {{-20, -20}, {20, 20}, {20, -20}, {-20, 20}}, 1600.0});
    vector<Vec2> penta = circlePoints(5, 40);
    v.push_back({"star-ordered pentagon", {penta[0], penta[2], penta[4], penta[1], penta[3]},
                 regularArea(5, 40)});
    return v;
}

// Hull points already in outline order: both windings, rotated starts.
static vector<pair<string, vector<Vec2>>> cyclicInputs() {
    vector<pair<string, vector<Vec2>>> v;
    vector<Vec2> penta = circlePoints(5, 40);
    v.push_back({"pentagon, start 2", {penta[2], penta[3], penta[4], penta[0], penta[1]}});
    v.push_back({"pentagon reversed, start 3", {penta[3], penta[2], penta[1], penta[0], penta[4]}});
    v.push_back({"square, other winding, start 1", {{20, 20}, {-20, 20}, {-20, -20}, {20, -20}}});
    return v;
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
    // Tiny next to their distance from the local origin: a dynamic body's
    // inertia about its centroid rounds to <= 0 (Box2D assert in Debug, NaN
    // motion in Release).
    v.push_back({"0.5 px triangle at (600, 600)", {{600, 600}, {600.5f, 600}, {600, 600.5f}}});
    v.push_back({"0.25 px triangle at (800, 800)", {{800, 800}, {800.25f, 800}, {800, 800.25f}}});
    v.push_back({"0.5 px triangle at (1600, 1600)", {{1600, 1600}, {1600.5f, 1600}, {1600, 1600.5f}}});
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

    // A tiny polygon far from the origin: the warning says why.
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, {{600, 600}, {600.5f, 600}, {600, 600.5f}}, 0, 0);
        check("PolyShape::setup tiny far triangle: warning says too small",
              w.lastContains("too small for its distance"));
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

    // setupRegular() still works, and its first vertex is still the top one.
    {
        box2d::PolyShape poly;
        poly.setupRegular(world, 400, 300, 30, 6);
        check("PolyShape::setupRegular 6 sides: created", poly.isCreated());
    }
    {
        box2d::PolyShape poly;
        poly.setupRegular(world, 400, 300, 30, 3);
        const auto& v = poly.getVertices();
        check("PolyShape::setupRegular 3 sides: getVertices()[0] is the top",
              v.size() == 3 && abs(v[0].x) < 1e-3f && abs(v[0].y + 30) < 1e-3f);
    }

    // Convex input comes back as given, in its order (Box2D itself starts at
    // the rightmost point and flips the winding of this square).
    {
        vector<Vec2> square = {{-20, -20}, {-20, 20}, {20, 20}, {20, -20}};
        box2d::PolyShape poly;
        poly.setup(world, square, 400, 300);
        check("PolyShape::setup convex square: getVertices() keeps the input order",
              poly.isCreated() && poly.getVertices() == square);
    }

    // Hull points in a crossing order come back in hull order, so draw()
    // outlines the fixture and the fan fill covers all of it.
    for (const auto& in : crossingInputs()) {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, in.pts, 400, 300);
        const auto& v = poly.getVertices();
        check(string("PolyShape::setup ") + in.name + ": created, no warning",
              poly.isCreated() && w.count == 0 && v.size() == in.pts.size());
        check(string("PolyShape::setup ") + in.name + ": getVertices() is a simple convex cycle",
              isSimpleConvexCycle(v));
        check(string("PolyShape::setup ") + in.name + ": fan fill covers the whole polygon",
              abs(fanArea(v) - in.area) < 1e-3 * in.area);
    }

    // Hull points already in outline order keep that order.
    for (const auto& in : cyclicInputs()) {
        box2d::PolyShape poly;
        poly.setup(world, in.second, 400, 300);
        check("PolyShape::setup " + in.first + ": getVertices() keeps the input order",
              poly.isCreated() && poly.getVertices() == in.second);
    }

    // A collinear middle point is not concave: the square is built, and the
    // hull (without that point) is what getVertices() returns.
    {
        vector<Vec2> pts = {{-20, -20}, {0, -20}, {20, -20}, {20, 20}, {-20, 20}};
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, pts, 400, 300);
        check("PolyShape::setup collinear middle point: 4-point square",
              poly.isCreated() && poly.getNumVertices() == 4 && !hasPoint(poly.getVertices(), 0, -20) &&
              w.count == 0);
    }

    // The same tiny triangle at the origin is fine.
    {
        box2d::PolyShape poly;
        poly.setup(world, {{0, 0}, {0.5f, 0}, {0, 0.5f}}, 400, 300);
        check("PolyShape::setup 0.5 px triangle at the origin: created", poly.isCreated());
    }

    // A normal-sized polygon far from its origin is fine: it is created and
    // steps without NaN.
    {
        box2d::PolyShape poly;
        poly.setup(world, {{600, 600}, {640, 600}, {600, 640}}, 0, 0);
        poly.getBody()->SetAngularVelocity(1.0f);
        for (int i = 0; i < 60; ++i) world.getWorld()->Step(1.0f / 60.0f, 8, 3);
        check("PolyShape::setup 40 px triangle at (600, 600): created and finite",
              poly.isCreated() && isfinite(poly.getBody()->GetPosition().x) &&
              isfinite(poly.getBody()->GetAngle()));
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

    // Sharp tips cost the most area to drop: a long thin diamond with many
    // points along its edges keeps its 4 tips. (Blunt tips are not kept on
    // purpose.)
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
        check("setupConvex 12 collinear points: warning says they collapsed",
              w.lastContains("got 12 points: fewer than 3 distinct, non-collinear points") &&
              w.lastContains("drops duplicate and collinear points"));
    }
    {
        vector<Vec2> same(50, Vec2(7, 3));
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupConvex(world, same, 400, 300);
        check("setupConvex 50 copies of one point: no body", !poly.isCreated());
        check("setupConvex 50 copies of one point: one warning", w.count == 1);
        check("setupConvex 50 copies of one point: warning says they collapsed",
              w.lastContains("got 50 points: fewer than 3 distinct, non-collinear points") &&
              w.lastContains("drops duplicate and collinear points"));
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
    {
        WarningCapture w;
        shared_ptr<Node> node;
        attach(world, node, box2d::Shape2D::polygon({{0, 0}, {10, 0}}));
        check("RigidBody2D polygon 2 points: warning says Shape2D::convex() may have dropped points",
              w.lastContains("fewer than 3 distinct, non-collinear points") &&
              w.lastContains("Shape2D::convex() may have dropped duplicate and collinear points"));
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
        vector<Vec2> square = {{-20, -20}, {-20, 20}, {20, 20}, {20, -20}};
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(square));
        check("RigidBody2D polygon convex square: shape() keeps the input order",
              rb->getBody() && rb->shape().verts == square);
    }
    for (const auto& in : crossingInputs()) {
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(in.pts));
        const auto& v = rb->shape().verts;
        check(string("RigidBody2D polygon ") + in.name + ": shape() is a simple convex cycle",
              rb->getBody() && v.size() == in.pts.size() && isSimpleConvexCycle(v));
        check(string("RigidBody2D polygon ") + in.name + ": fan fill covers the whole polygon",
              abs(fanArea(v) - in.area) < 1e-3 * in.area);
    }
    for (const auto& in : cyclicInputs()) {
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(in.second));
        check("RigidBody2D polygon " + in.first + ": shape() keeps the input order",
              rb->getBody() && rb->shape().verts == in.second);
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
        check("RigidBody2D Shape2D::convex collinear: warning says they collapsed",
              w.lastContains("fewer than 3 distinct, non-collinear points") &&
              w.lastContains("Shape2D::convex() may have dropped duplicate and collinear points"));
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::convex(vector<Vec2>(50, Vec2(7, 3))));
        check("RigidBody2D Shape2D::convex 50 copies of one point: no body", rb->getBody() == nullptr);
        check("RigidBody2D Shape2D::convex 50 copies of one point: one warning", w.count == 1);
        check("RigidBody2D Shape2D::convex 50 copies of one point: warning says they collapsed",
              w.lastContains("fewer than 3 distinct, non-collinear points") &&
              w.lastContains("Shape2D::convex() may have dropped duplicate and collinear points"));
    }
}

// ---------------------------------------------------------------------------
// Compound bodies (#427)
// ---------------------------------------------------------------------------
static float polygonArea(const vector<Vec2>& pts) {
    double a = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2& p = pts[i];
        const Vec2& q = pts[(i + 1) % pts.size()];
        a += double(p.x) * q.y - double(q.x) * p.y;
    }
    return float(abs(a) * 0.5);
}

// Mass of an area in px^2 at density 1.
static float areaMass(float areaPx) {
    return areaPx / (box2d::World::scale * box2d::World::scale);
}

static void step(box2d::World& world, int n) {
    for (int i = 0; i < n; ++i) {
        world.getWorld()->Step(1.0f / 60.0f, 8, 3);
        world.getCollisionManager()->update();
    }
}

// Touching contacts between two bodies (one per touching fixture pair).
static int touchingContacts(box2d::World& world, const b2Body* a, const b2Body* b) {
    int n = 0;
    for (b2Contact* c = world.getWorld()->GetContactList(); c; c = c->GetNext()) {
        const b2Body* ba = c->GetFixtureA()->GetBody();
        const b2Body* bb = c->GetFixtureB()->GetBody();
        if (c->IsTouching() && ((ba == a && bb == b) || (ba == b && bb == a))) ++n;
    }
    return n;
}

// A square ring: outer square with a square hole wound the other way.
static Path squareRing(float outer, float inner) {
    Path p;
    float o = outer * 0.5f, i = inner * 0.5f;
    p.moveTo(-o, -o); p.lineTo(o, -o); p.lineTo(o, o); p.lineTo(-o, o); p.close();
    p.moveTo(-i, -i); p.lineTo(-i, i); p.lineTo(i, i); p.lineTo(i, -i); p.close();
    return p;
}

static void testCompoundShapes(box2d::World& world) {
    // 20-point circle: many fixtures, the mass of the 20-gon.
    {
        vector<Vec2> pts = circlePoints(20, 50);
        Path path(pts);
        path.close();
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, path, 400, 300);
        check("setupCompound 20-point circle: created", poly.isCreated());
        check("setupCompound 20-point circle: one fixture per triangle (18)",
              fixtureCount(poly.getBody()) == 18);
        float ratio = poly.getMass() / areaMass(polygonArea(pts));
        check("setupCompound 20-point circle: mass = area x density (1%)", abs(ratio - 1.0f) < 0.01f);
        check("setupCompound 20-point circle: getVertices() is the outline", poly.getNumVertices() == 20);
        check("setupCompound 20-point circle: no warning", w.count == 0);
    }

    // Convex outlines of at most 8 points: exactly one ordinary fixture.
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, circlePoints(5, 40), 400, 300);
        check("setupCompound convex 5 points: one fixture", fixtureCount(poly.getBody()) == 1);
        check("setupCompound convex 5 points: a 5-point polygon", polygonVertexCount(poly.getBody()) == 5);
        check("setupCompound convex 5 points: no warning", w.count == 0);
    }
    {
        vector<Vec2> pts = circlePoints(8, 40);
        pts.push_back(pts[0]);   // closing point, as drawn paths often have
        box2d::PolyShape poly;
        poly.setupCompound(world, Path(pts), 400, 300);
        check("setupCompound convex 8 points + closing point: one fixture", fixtureCount(poly.getBody()) == 1);
    }
    {
        // A pentagram turns the same way at every corner but is not convex.
        vector<Vec2> star;
        for (int i = 0; i < 5; ++i) {
            float a = TAU * (i * 2 % 5) / 5;
            star.push_back(Vec2(cos(a) * 40, sin(a) * 40));
        }
        box2d::PolyShape poly;
        poly.setupCompound(world, star, 400, 300);
        check("setupCompound pentagram: triangulated, not one polygon", fixtureCount(poly.getBody()) > 1);
    }

    // The notched 5-point polygon keeps its notch.
    {
        vector<Vec2> notched = {{-20, -20}, {20, -20}, {0, 0}, {20, 20}, {-20, 20}};
        box2d::PolyShape poly;
        poly.setupCompound(world, notched, 400, 300);
        check("setupCompound notched: several fixtures", fixtureCount(poly.getBody()) > 1);
        check("setupCompound notched: the notch is empty", !poly.containsPoint(415, 300));
        check("setupCompound notched: the body is solid", poly.containsPoint(390, 300));
        check("setupCompound notched: mass = area x density",
              abs(poly.getMass() / areaMass(polygonArea(notched)) - 1.0f) < 0.01f);
    }

    // A ring: the hole is empty.
    {
        box2d::PolyShape poly;
        poly.setupCompound(world, squareRing(200, 80), 400, 300);
        check("setupCompound ring: created", poly.isCreated());
        check("setupCompound ring: the hole is empty", !poly.containsPoint(400, 300));
        check("setupCompound ring: the ring is solid", poly.containsPoint(400, 230));
        check("setupCompound ring: mass = (outer - hole) x density",
              abs(poly.getMass() / areaMass(200 * 200 - 80 * 80) - 1.0f) < 0.01f);
        check("setupCompound ring: getVertices() has both subpaths", poly.getNumVertices() == 8);
    }

    // A sliver (a spike whose base points nearly coincide) is skipped with one
    // warning; the rest of the body is built.
    {
        vector<Vec2> spiked = {{-50, -50}, {50, -50}, {50, 50}, {0.001f, 50},
                               {0, 150}, {-0.001f, 50}, {-50, 50}};
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, spiked, 400, 300);
        check("setupCompound sliver: created", poly.isCreated());
        check("setupCompound sliver: one warning", w.count == 1 && w.lastContains("skipped"));
    }

    // Nothing usable: no body, one warning.
    {
        vector<Vec2> line;
        for (int i = 0; i < 12; ++i) line.push_back(Vec2(i * 5.0f, 0));
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, line, 400, 300);
        check("setupCompound collinear: no body", !poly.isCreated());
        check("setupCompound collinear: one warning", w.count == 1);
    }

    // setup() with more than 8 points now also points to setupCompound().
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, circlePoints(9, 40), 400, 300);
        check("PolyShape::setup 9 points: warning names setupCompound()", w.lastContains("setupCompound()"));
    }

    // Mod API
    {
        vector<Vec2> pts = circlePoints(20, 50);
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(pts));
        check("RigidBody2D compound 20 points: 18 fixtures", fixtureCount(rb->getBody()) == 18);
        float mass = rb->getBody() ? rb->getBody()->GetMass() : 0.0f;
        check("RigidBody2D compound 20 points: mass = area x density",
              abs(mass / areaMass(polygonArea(pts)) - 1.0f) < 0.01f);
        check("RigidBody2D compound 20 points: no warning", w.count == 0);
    }
    {
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(circlePoints(5, 40)));
        check("RigidBody2D compound convex 5 points: one fixture", fixtureCount(rb->getBody()) == 1);
    }
    {
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(squareRing(200, 80)));
        bool holeEmpty = true, ringSolid = false;
        if (rb->getBody()) {
            for (b2Fixture* f = rb->getBody()->GetFixtureList(); f; f = f->GetNext()) {
                if (f->TestPoint(box2d::World::toBox2d(400, 300))) holeEmpty = false;
                if (f->TestPoint(box2d::World::toBox2d(400, 230))) ringSolid = true;
            }
        }
        check("RigidBody2D compound ring: the hole is empty", rb->getBody() && holeEmpty && ringSolid);
    }
    {
        vector<Vec2> line;
        for (int i = 0; i < 12; ++i) line.push_back(Vec2(i * 5.0f, 0));
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(line));
        check("RigidBody2D compound collinear: no body", rb->getBody() == nullptr);
        check("RigidBody2D compound collinear: one warning", w.count == 1);
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        attach(world, node, box2d::Shape2D::polygon(circlePoints(9, 40)));
        check("RigidBody2D polygon 9 points: warning names Shape2D::compound()",
              w.lastContains("Shape2D::compound()"));
    }
}

// Balls in and above a ring's hole, under gravity.
static void testCompoundHole() {
    box2d::World world;
    world.setup(0, 300);   // 10 m/s^2 at 30 px/m
    world.setAutoUpdate(false);

    box2d::PolyShape ring;
    ring.setupCompound(world, squareRing(200, 80), 400, 300);   // hole: y 260..340
    ring.setStatic();

    box2d::CircleBody inHole, onTop;
    inHole.setup(world, 400, 275, 10);   // inside the hole
    onTop.setup(world, 400, 150, 10);    // above the solid top (y 200)
    step(world, 180);

    float yHole = inHole.getPhysicsPosition().y;
    float yTop = onTop.getPhysicsPosition().y;
    check("ring: a ball in the hole falls to the hole's floor (y 330)", abs(yHole - 330) < 1.5f);
    check("ring: a ball above the solid part lands on it (y 190)", abs(yTop - 190) < 1.5f);
}

static void testCompoundFilters(box2d::World& world) {
    box2d::PolyShape poly;
    poly.setupCompound(world, squareRing(200, 80), 400, 300);
    auto* collider = poly.getCollider();
    bool allLinked = true;
    for (b2Fixture* f = poly.getBody()->GetFixtureList(); f; f = f->GetNext()) {
        if (f->GetUserData().pointer != reinterpret_cast<uintptr_t>(collider)) allLinked = false;
    }
    check("compound collider: every fixture points to the collider", collider && allLinked);

    collider->setCategoryBits(0x0004);
    collider->setMaskBits(0x0003);
    collider->setGroupIndex(-2);
    poly.setSensor(true);
    bool filters = true, sensors = true;
    for (b2Fixture* f = poly.getBody()->GetFixtureList(); f; f = f->GetNext()) {
        const b2Filter& fd = f->GetFilterData();
        if (fd.categoryBits != 0x0004 || fd.maskBits != 0x0003 || fd.groupIndex != -2) filters = false;
        if (!f->IsSensor()) sensors = false;
    }
    check("compound collider: filter setters reach every fixture", filters);
    check("compound: setSensor reaches every fixture", sensors);

    shared_ptr<Node> node;
    auto* rb = attach(world, node, box2d::Shape2D::compound(squareRing(200, 80)));
    rb->setTrigger(true);
    bool triggers = rb->getBody() != nullptr;
    if (rb->getBody()) {
        for (b2Fixture* f = rb->getBody()->GetFixtureList(); f; f = f->GetNext()) {
            if (!f->IsSensor()) triggers = false;
        }
    }
    check("RigidBody2D compound: setTrigger reaches every fixture", triggers);
}

// A bar 200 x 40 with a notch in its bottom (concave, several triangles), and
// a box 220 x 20 pressed 5 px into its top across the whole width.
static const vector<Vec2> kNotchedBar = {{-100, -20}, {100, -20}, {100, 20}, {10, 20},
                                         {0, 5}, {-10, 20}, {-100, 20}};

static void testCompoundEvents() {
    // Classic API: Collider2D events.
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        box2d::PolyShape bar;
        bar.setupCompound(world, kNotchedBar, 400, 300);
        bar.setStatic();
        box2d::RectBody box;
        box.setup(world, 400, 300 - 20 - 10 + 5, 220, 20);

        int barEnter = 0, barStay = 0, barExit = 0, boxEnter = 0, boxExit = 0;
        auto* bc = bar.getCollider();
        auto* xc = box.getCollider();
        EventListener l1 = bc->onCollisionEnter.listen([&](box2d::CollisionEvent&) { ++barEnter; });
        EventListener l2 = bc->onCollisionStay.listen([&](box2d::CollisionEvent&) { ++barStay; });
        EventListener l3 = bc->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++barExit; });
        EventListener l4 = xc->onCollisionEnter.listen([&](box2d::CollisionEvent&) { ++boxEnter; });
        EventListener l5 = xc->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++boxExit; });

        world.getWorld()->Step(1.0f / 60.0f, 8, 3);
        int touching = touchingContacts(world, bar.getBody(), box.getBody());
        printf("  (classic: %d fixtures, %d touching contacts)\n", fixtureCount(bar.getBody()), touching);
        check("Collider2D: the box touches several of the bar's fixtures", touching >= 2);
        check("Collider2D: one Enter on each side", barEnter == 1 && boxEnter == 1);
        world.getCollisionManager()->update();
        world.getCollisionManager()->update();
        check("Collider2D: one Stay per update", barStay == 2);

        box.setPhysicsPosition(400, -1000);
        world.getWorld()->Step(1.0f / 60.0f, 8, 3);
        check("Collider2D: separated", touchingContacts(world, bar.getBody(), box.getBody()) == 0);
        check("Collider2D: one Exit on each side", barExit == 1 && boxExit == 1);
    }

    // Mod API: RigidBody2D events.
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        shared_ptr<Node> barNode, boxNode;
        auto* bar = attach(world, barNode, box2d::Shape2D::compound(kNotchedBar));
        bar->setBodyType(box2d::BodyType::Static);
        boxNode = make_shared<Node>();
        boxNode->setPos(400, 300 - 20 - 10 + 5);
        auto* box = boxNode->addMod<box2d::RigidBody2D>(world, box2d::Shape2D::box(220, 20));

        int began = 0, stay = 0, ended = 0, boxBegan = 0, boxEnded = 0;
        EventListener l1 = bar->onCollisionBegan.listen([&](box2d::Contact2D&) { ++began; });
        EventListener l2 = bar->onCollisionStay.listen([&](box2d::Contact2D&) { ++stay; });
        EventListener l3 = bar->onCollisionEnded.listen([&](box2d::Contact2D&) { ++ended; });
        EventListener l4 = box->onCollisionBegan.listen([&](box2d::Contact2D&) { ++boxBegan; });
        EventListener l5 = box->onCollisionEnded.listen([&](box2d::Contact2D&) { ++boxEnded; });

        world.getWorld()->Step(1.0f / 60.0f, 8, 3);
        int touching = touchingContacts(world, bar->getBody(), box->getBody());
        printf("  (Mod: %d fixtures, %d touching contacts)\n", fixtureCount(bar->getBody()), touching);
        check("RigidBody2D: the box touches several of the bar's fixtures", touching >= 2);
        check("RigidBody2D: one Began on each side", began == 1 && boxBegan == 1);
        world.getCollisionManager()->update();
        world.getCollisionManager()->update();
        check("RigidBody2D: one Stay per update", stay == 2);

        box->getBody()->SetTransform(box2d::World::toBox2d(400, -1000), 0);
        world.getWorld()->Step(1.0f / 60.0f, 8, 3);
        check("RigidBody2D: separated", touchingContacts(world, bar->getBody(), box->getBody()) == 0);
        check("RigidBody2D: one Ended on each side", ended == 1 && boxEnded == 1);
    }
}

// ---------------------------------------------------------------------------
// reducedConvexHull(): heap-based reduction vs the O(h^2) scan it replaced
// ---------------------------------------------------------------------------

// The reduction as it was before the heap: rescan every vertex, drop the first
// one with the smallest area. Kept verbatim as the reference.
static vector<Vec2> referenceReducedConvexHull(const vector<Vec2>& points, size_t maxPoints) {
    if (maxPoints < 3) maxPoints = 3;

    vector<Vec2> pts = points;
    sort(pts.begin(), pts.end(), [](const Vec2& a, const Vec2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    pts.erase(unique(pts.begin(), pts.end(), [](const Vec2& a, const Vec2& b) {
        return a.x == b.x && a.y == b.y;
    }), pts.end());
    if (pts.size() < 3) return pts;

    auto cross = [](const Vec2& o, const Vec2& a, const Vec2& b) {
        return (double(a.x) - o.x) * (double(b.y) - o.y) - (double(a.y) - o.y) * (double(b.x) - o.x);
    };

    vector<Vec2> hull(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0) --k;
        hull[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, lower = k + 1; i > 0; --i) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0.0) --k;
        hull[k++] = pts[i - 1];
    }
    hull.resize(k - 1);

    while (hull.size() > maxPoints) {
        const size_t n = hull.size();
        size_t best = 0;
        double bestArea = numeric_limits<double>::max();
        for (size_t i = 0; i < n; ++i) {
            double a = abs(cross(hull[(i + n - 1) % n], hull[i], hull[(i + 1) % n]));
            if (a < bestArea) {
                bestArea = a;
                best = i;
            }
        }
        hull.erase(hull.begin() + static_cast<ptrdiff_t>(best));
    }
    return hull;
}

// Strictly convex outline of `n` (even) points with integer coordinates, exact
// in float: n/2 shortest primitive edge vectors of the upper half-plane and
// their negations, walked in angle order. No point is collinear with its
// neighbours, so all of them are on the hull (a float circle this dense is
// not: rounding flattens it).
static vector<Vec2> denseConvexOutline(size_t n) {
    vector<pair<int, int>> dirs;
    for (int k = 1; dirs.size() < n / 2; ++k) {
        // Directions with max(|a|, |b|) == k, angle in [0, TAU / 2).
        for (int a = -k; a <= k; ++a) {
            for (int b = 0; b <= k; ++b) {
                if (max(abs(a), b) != k || (b == 0 && a < 0)) continue;
                int x = abs(a), y = b;
                while (y) { int t = x % y; x = y; y = t; }
                if (x == 1) dirs.push_back({a, b});
            }
        }
    }
    dirs.resize(n / 2);
    sort(dirs.begin(), dirs.end(), [](const pair<int, int>& u, const pair<int, int>& v) {
        return u.first * v.second - u.second * v.first > 0;
    });
    vector<Vec2> pts;
    long long x = 0, y = 0;
    for (int side = 1; side >= -1; side -= 2) {
        for (const auto& d : dirs) {
            pts.push_back(Vec2(float(x), float(y)));
            x += side * d.first;
            y += side * d.second;
        }
    }
    return pts;
}

static void testReducedConvexHull() {
    // Random inputs, many with equal areas (lattice points, regular polygons)
    // so the tie-break is exercised, at several target sizes.
    mt19937 rng(342);
    uniform_real_distribution<float> coord(-500.0f, 500.0f);
    uniform_int_distribution<int> lattice(-6, 6);
    const size_t targets[] = {3, 4, 5, 8, 12};
    int cases = 0, mismatches = 0;
    auto compare = [&](const vector<Vec2>& in) {
        for (size_t m : targets) {
            ++cases;
            if (box2d::detail::reducedConvexHull(in, m) != referenceReducedConvexHull(in, m)) ++mismatches;
        }
    };
    for (int t = 0; t < 200; ++t) {
        vector<Vec2> in(3 + rng() % 200);
        for (auto& p : in) p = Vec2(coord(rng), coord(rng));
        compare(in);
    }
    for (int t = 0; t < 200; ++t) {
        vector<Vec2> in(3 + rng() % 120);
        for (auto& p : in) p = Vec2(float(lattice(rng)), float(lattice(rng)));
        compare(in);
    }
    for (int n = 9; n <= 64; ++n) {
        compare(circlePoints(n, 100));
        vector<Vec2> square;  // integer outline of a square: collinear sides
        for (int i = 0; i < n; ++i) {
            square.push_back(Vec2(float(i), 0.0f));
            square.push_back(Vec2(float(i), float(n)));
            square.push_back(Vec2(0.0f, float(i)));
            square.push_back(Vec2(float(n), float(i)));
        }
        compare(square);
        vector<Vec2> octagon;  // lattice octagon: its corners tie on area
        for (int i = -n; i <= n; ++i) {
            for (int j = -n; j <= n; ++j) {
                if (abs(i) + abs(j) <= n + n / 2) octagon.push_back(Vec2(float(i), float(j)));
            }
        }
        compare(octagon);
    }
    for (size_t n : {100, 1000, 3000}) compare(denseConvexOutline(n));
    check("reducedConvexHull matches the O(h^2) reference on " + to_string(cases) + " inputs",
          mismatches == 0);

    // 50,000 points, all on the hull: the O(h^2) scan took seconds here.
    vector<Vec2> dense = denseConvexOutline(50000);
    check("reducedConvexHull 50,000-point outline: every point is on the hull",
          box2d::detail::reducedConvexHull(dense, dense.size()).size() == dense.size());
    auto t0 = chrono::steady_clock::now();
    vector<Vec2> reduced = box2d::detail::reducedConvexHull(dense);
    double sec = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    printf("  reducedConvexHull 50,000-point outline: %.3f s\n", sec);
    check("reducedConvexHull 50,000-point outline: 8 points", reduced.size() == 8);
    check("reducedConvexHull 50,000-point outline: under 1 s", sec < 1.0);
}

int main() {
    box2d::World world;
    world.setup(0, 0);
    world.setAutoUpdate(false);

    testPolyShapeRefuses(world);
    testPolyShapeValid(world);
    testSetupConvex(world);
    testRigidBody2D(world);
    testCompoundShapes(world);
    testCompoundFilters(world);
    testCompoundHole();
    testCompoundEvents();
    testReducedConvexHull();

    if (g_fail) {
        printf("\n%d check(s) FAILED\n", g_fail);
        return 1;
    }
    printf("\nAll checks passed\n");
    return 0;
}
