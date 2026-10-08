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
//   - when the hull leaves points inside it (#500), setup() and
//     Shape2D::polygon() each log one warning with the counts, pointing to the
//     exact-shape call, only the first time in the process; every point kept
//     (also in a crossing order, or with a collinear middle point) gives none;
//   - setup() and Shape2D::polygon() with a Path use every point of every
//     subpath;
//   - setupSimplified() / Shape2D::simplified() take any number of points and make one
//     fixture of at most 8 points whose mass is close to the outline's; the
//     reduction is O(h log h) and picks the same points as the plain
//     O(h^2) scan it replaced.
//
// Compound bodies (#427). setupCompound() / Shape2D::compound() keep any
// outline exactly, one fixture per triangle of Path::buildFillTriangles():
//   - a 20-point circle gives many fixtures and the 20-gon's mass; a convex
//     outline of at most 8 points gives exactly one fixture; a pentagram's
//     doubly covered center weighs twice; an L whose edge runs straight back
//     is triangulated, not made its hull; the fill kept for drawing is
//     Path::buildFillTriangles()' own;
//   - concave notches and holes stay empty: a ball in the hole falls to the
//     hole's floor, a ball above the solid part lands on it;
//   - slivers are skipped with one warning; nothing usable gives no body;
//   - Collider2D filter and trigger settings reach every fixture;
//   - a compound touching a box with several fixtures at once gives exactly
//     one Enter / Began, one Stay per update, and one Exit / Ended;
//   - a sensor box moved across the seam between two fixtures gets no Exit /
//     Ended, whichever contact Box2D updates first;
//   - Stay listeners may destroy bodies of the next pair or of their own
//     (the other pairs still get their Stay);
//   - listeners of a deferred Exit / Ended or of Stay may free the other body
//     of their pair (a RigidBody2D's hears nothing more; a classic Body
//     destroyed from Stay gets its Exit at once), and a body destroyed between
//     Step() and update() gets no Exit / Ended, its partner one with no
//     other, and a new body at its address none;
//   - the offset/inertia check runs once on the whole body: a rounded
//     rectangle, an off-center 128-gon and a 4096-gon keep every triangle
//     with no warning; a tiny outline far from the origin is refused as a
//     whole, and a tiny convex ring as one polygon; bodies just inside the
//     limit keep a positive inertia at densities 0.001 to 1000;
//   - every fixture of a polygon or compound body has the requested density
//     (PolyShape and RigidBody2D), and setDensity() / setBodyType() (or
//     setStatic() / setDynamic()) afterwards give exactly the mass data of a
//     body created that way.
// =============================================================================

#include <tcxBox2d.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
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

    // More than 8 points: the warning points to setupSimplified().
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, circlePoints(9, 40), 400, 300);
        check("PolyShape::setup 9 points: warning names setupSimplified()",
              w.lastContains("setupSimplified()"));
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
        // The hull warning is once per process and already fired in
        // testHullDropWarning().
        check("PolyShape::setup notched: no second hull warning", w.count == 0);
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

static void testSetupSimplified(box2d::World& world) {
    const float r = 50;
    // 20-point circle, vector and Path overloads.
    for (int usePath = 0; usePath < 2; ++usePath) {
        string tag = usePath ? "Path" : "vector";
        WarningCapture w;
        box2d::PolyShape poly;
        if (usePath) poly.setupSimplified(world, Path(circlePoints(20, r)), 400, 300);
        else         poly.setupSimplified(world, circlePoints(20, r), 400, 300);
        check("setupSimplified 20-point circle (" + tag + "): created", poly.isCreated());
        check("setupSimplified 20-point circle (" + tag + "): one fixture", fixtureCount(poly.getBody()) == 1);
        check("setupSimplified 20-point circle (" + tag + "): at most 8 vertices",
              poly.getNumVertices() >= 3 && poly.getNumVertices() <= 8 &&
              polygonVertexCount(poly.getBody()) == poly.getNumVertices());
        float ratio = poly.getMass() / discMass(r);
        check("setupSimplified 20-point circle (" + tag + "): mass within 15% of the disc",
              ratio > 0.85f && ratio <= 1.0f);
        check("setupSimplified 20-point circle (" + tag + "): no warning", w.count == 0);
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
        poly.setupSimplified(world, pts, 400, 300);
        const auto& v = poly.getVertices();
        check("setupSimplified 24-point diamond: at most 8 vertices", poly.isCreated() && v.size() <= 8);
        check("setupSimplified 24-point diamond: keeps the 4 tips",
              hasPoint(v, -100, 0) && hasPoint(v, 0, -10) && hasPoint(v, 100, 0) && hasPoint(v, 0, 10));
    }

    // Degenerate hulls fail like setup().
    {
        vector<Vec2> line;
        for (int i = 0; i < 12; ++i) line.push_back(Vec2(i * 5.0f, i * 2.0f));
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupSimplified(world, line, 400, 300);
        check("setupSimplified 12 collinear points: no body", !poly.isCreated());
        check("setupSimplified 12 collinear points: one warning", w.count == 1);
        check("setupSimplified 12 collinear points: warning says they collapsed",
              w.lastContains("got 12 points: fewer than 3 distinct, non-collinear points") &&
              w.lastContains("drops duplicate and collinear points"));
    }
    {
        vector<Vec2> same(50, Vec2(7, 3));
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupSimplified(world, same, 400, 300);
        check("setupSimplified 50 copies of one point: no body", !poly.isCreated());
        check("setupSimplified 50 copies of one point: one warning", w.count == 1);
        check("setupSimplified 50 copies of one point: warning says they collapsed",
              w.lastContains("got 50 points: fewer than 3 distinct, non-collinear points") &&
              w.lastContains("drops duplicate and collinear points"));
    }
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupSimplified(world, {{0, 0}, {10, 0}}, 400, 300);
        check("setupSimplified 2 points: no body", !poly.isCreated());
        check("setupSimplified 2 points: one warning", w.count == 1);
    }
}

// ---------------------------------------------------------------------------
// Mod API: RigidBody2D with Shape2D::polygon() / Shape2D::simplified()
// ---------------------------------------------------------------------------
static box2d::RigidBody2D* attach(box2d::World& world, shared_ptr<Node>& node, const box2d::Shape2D& shape) {
    node = make_shared<Node>();
    node->setPos(400, 300);
    return node->addMod<box2d::RigidBody2D>(world, shape);
}

// ---------------------------------------------------------------------------
// Hull warning (#500) and Shape2D::polygon(const Path&)
// ---------------------------------------------------------------------------
// An L (6 points, its inner corner off the hull) plus one point inside it:
// the hull keeps 5 of the 7 points.
static vector<Vec2> lWithInteriorPoint() {
    return {{-20, -20}, {20, -20}, {20, 0}, {0, 0}, {0, 20}, {-20, 20}, {-10, -10}};
}

// Runs before any other test builds a polygon with points inside its hull:
// each call's warning comes once per process, so the "no warning" checks
// here only mean something before it has fired.
static void testHullDropWarning(box2d::World& world) {
    // Every point on the outline: no warning, also in a crossing order, with
    // a collinear middle point, or with the first point repeated at the end.
    vector<pair<string, vector<Vec2>>> kept;
    kept.push_back({"convex pentagon", circlePoints(5, 40)});
    for (const auto& in : crossingInputs()) kept.push_back({in.name, in.pts});
    kept.push_back({"collinear middle point", {{-20, -20}, {0, -20}, {20, -20}, {20, 20}, {-20, 20}}});
    kept.push_back({"closing point repeated", {{-20, -20}, {20, -20}, {20, 20}, {-20, 20}, {-20, -20}}});
    for (const auto& in : kept) {
        {
            WarningCapture w;
            box2d::PolyShape poly;
            poly.setup(world, in.second, 400, 300);
            check("hull warning: setup() " + in.first + ": created, no warning",
                  poly.isCreated() && w.count == 0);
        }
        {
            WarningCapture w;
            shared_ptr<Node> node;
            auto* rb = attach(world, node, box2d::Shape2D::polygon(in.second));
            check("hull warning: Shape2D::polygon() " + in.first + ": created, no warning",
                  rb->getBody() && w.count == 0);
        }
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::simplified(circlePoints(20, 50)));
        check("hull warning: Shape2D::simplified() 20 points: no warning", rb->getBody() && w.count == 0);
    }

    // Points inside the hull: one warning naming the counts and setupCompound().
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setup(world, lWithInteriorPoint(), 400, 300);
        check("hull warning: setup() L + interior point: created as the 5-point hull",
              poly.isCreated() && poly.getNumVertices() == 5);
        check("hull warning: setup() L + interior point: one warning", w.count == 1);
        check("hull warning: setup() L + interior point: warning names counts and setupCompound()",
              w.lastContains("tcxBox2d: PolyShape::setup() dropped 2 of 7 points inside the convex hull;"
                             " use setupCompound() to keep the exact shape."));
    }
    // Once per process: neither the same call again nor the Path overload
    // (which goes through setup()) warns again.
    {
        WarningCapture w;
        box2d::PolyShape a, b;
        a.setup(world, lWithInteriorPoint(), 400, 300);
        b.setup(world, Path(lWithInteriorPoint()), 400, 300);
        check("hull warning: setup() again (vector and Path): created, no warning",
              a.isCreated() && b.isCreated() && w.count == 0);
    }

    // Shape2D::polygon() has its own gate: it warns once even though setup()
    // already did.
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(lWithInteriorPoint()));
        check("hull warning: Shape2D::polygon() L + interior point: created as the 5-point hull",
              rb->getBody() && rb->shape().verts.size() == 5);
        check("hull warning: Shape2D::polygon() L + interior point: one warning", w.count == 1);
        check("hull warning: Shape2D::polygon() L + interior point: warning names counts and compound()",
              w.lastContains("tcxBox2d: Shape2D::polygon() dropped 2 of 7 points inside the convex hull;"
                             " use Shape2D::compound() to keep the exact shape."));
    }
    {
        WarningCapture w;
        shared_ptr<Node> a, b;
        auto* ra = attach(world, a, box2d::Shape2D::polygon(lWithInteriorPoint()));
        auto* rb = attach(world, b, box2d::Shape2D::polygon(Path(lWithInteriorPoint())));
        check("hull warning: Shape2D::polygon() again (vector and Path): created, no warning",
              ra->getBody() && rb->getBody() && w.count == 0);
    }
}

static void testPolygonPath(box2d::World& world) {
    // A convex Path keeps its points and order, like the vector overload.
    {
        vector<Vec2> penta = circlePoints(5, 40);
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(Path(penta)));
        check("Shape2D::polygon(Path) pentagon: one fixture, 5 vertices",
              fixtureCount(rb->getBody()) == 1 && polygonVertexCount(rb->getBody()) == 5);
        check("Shape2D::polygon(Path) pentagon: shape() keeps the points", rb->shape().verts == penta);
        check("Shape2D::polygon(Path) pentagon: no warning", w.count == 0);
    }
    // Every point of every subpath counts: two 2-point subpaths make a square.
    {
        Path p;
        p.moveTo(-20, -20); p.lineTo(20, -20);
        p.moveTo(20, 20);   p.lineTo(-20, 20);
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(p));
        const auto& v = rb->shape().verts;
        check("Shape2D::polygon(Path) two subpaths: the square of all 4 points",
              rb->getBody() && polygonVertexCount(rb->getBody()) == 4 && v.size() == 4 &&
              hasPoint(v, -20, -20) && hasPoint(v, 20, -20) && hasPoint(v, 20, 20) && hasPoint(v, -20, 20));

        box2d::PolyShape poly;
        poly.setup(world, p, 400, 300);
        check("PolyShape::setup(Path) two subpaths: the same square",
              poly.isCreated() && poly.getNumVertices() == 4);
    }
    // More than 8 points in total is refused, pointing to simplified().
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::polygon(Path(circlePoints(20, 40))));
        check("Shape2D::polygon(Path) 20 points: no body", rb->getBody() == nullptr);
        check("Shape2D::polygon(Path) 20 points: one warning naming Shape2D::simplified()",
              w.count == 1 && w.lastContains("Shape2D::simplified()"));
    }
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
        check("RigidBody2D polygon 9 points: warning names Shape2D::simplified()",
              w.lastContains("Shape2D::simplified()"));
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        attach(world, node, box2d::Shape2D::polygon({{0, 0}, {10, 0}}));
        check("RigidBody2D polygon 2 points: warning says Shape2D::simplified() may have dropped points",
              w.lastContains("fewer than 3 distinct, non-collinear points") &&
              w.lastContains("Shape2D::simplified() may have dropped duplicate and collinear points"));
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
        auto* rb = attach(world, node, box2d::Shape2D::simplified(circlePoints(20, 50)));
        check("RigidBody2D Shape2D::simplified 20 points: one fixture", fixtureCount(rb->getBody()) == 1);
        check("RigidBody2D Shape2D::simplified 20 points: at most 8 vertices",
              rb->shape().verts.size() <= 8 && polygonVertexCount(rb->getBody()) <= 8);
        float ratio = rb->getBody() ? rb->getBody()->GetMass() / discMass(50) : 0.0f;
        check("RigidBody2D Shape2D::simplified 20 points: mass within 15% of the disc",
              ratio > 0.85f && ratio <= 1.0f);
        check("RigidBody2D Shape2D::simplified 20 points: no warning", w.count == 0);
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::simplified(Path(circlePoints(20, 50))));
        check("RigidBody2D Shape2D::simplified Path: created", rb->getBody() != nullptr);
    }
    {
        vector<Vec2> line;
        for (int i = 0; i < 12; ++i) line.push_back(Vec2(i * 5.0f, 0));
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::simplified(line));
        check("RigidBody2D Shape2D::simplified collinear: no body", rb->getBody() == nullptr);
        check("RigidBody2D Shape2D::simplified collinear: one warning", w.count == 1);
        check("RigidBody2D Shape2D::simplified collinear: warning says they collapsed",
              w.lastContains("fewer than 3 distinct, non-collinear points") &&
              w.lastContains("Shape2D::simplified() may have dropped duplicate and collinear points"));
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::simplified(vector<Vec2>(50, Vec2(7, 3))));
        check("RigidBody2D Shape2D::simplified 50 copies of one point: no body", rb->getBody() == nullptr);
        check("RigidBody2D Shape2D::simplified 50 copies of one point: one warning", w.count == 1);
        check("RigidBody2D Shape2D::simplified 50 copies of one point: warning says they collapsed",
              w.lastContains("fewer than 3 distinct, non-collinear points") &&
              w.lastContains("Shape2D::simplified() may have dropped duplicate and collinear points"));
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
        // The center pentagon has winding 2: it is covered twice and weighs
        // twice (documented). Star: 10 triangles center-tip-notch; the notch
        // radius is r = R cos(TAU/5) / cos(TAU/10).
        const float R = 40, r = R * cos(TAU / 5) / cos(TAU / 10);
        const float starArea = 5 * R * r * sin(TAU / 10);
        const float centerArea = 2.5f * r * r * sin(TAU / 5);
        check("setupCompound pentagram: the doubly covered center weighs twice",
              abs(poly.getMass() / areaMass(starArea + centerArea) - 1.0f) < 0.01f);
    }

    // The fill kept for drawing is Path::buildFillTriangles()' own.
    {
        Path ring = squareRing(200, 80);
        box2d::detail::CompoundShapes shapes;
        bool made = box2d::detail::makeCompoundShapes(ring, shapes);
        const auto tris = ring.buildFillTriangles();
        bool same = made && shapes.fill.size() == tris.size();
        for (size_t i = 0; same && i < tris.size(); ++i) {
            same = shapes.fill[i].x == tris[i][0] && shapes.fill[i].y == tris[i][1];
        }
        check("setupCompound: the fill kept for drawing is Path's fill", same);
        Path pentagon(circlePoints(5, 40));
        pentagon.close();
        box2d::detail::CompoundShapes convex;
        box2d::detail::makeCompoundShapes(pentagon, convex);
        check("setupCompound convex 5 points: a fill is kept too", convex.fill.size() == 9);
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

    // An L whose first edge runs out and straight back (a 180-degree turn with
    // a zero cross product) turns once around in total like a convex ring,
    // but is not convex: it is triangulated, not made its hull.
    {
        const vector<Vec2> ell = {{0, 0}, {10, 0}, {5, 0}, {5, 5}, {10, 5}, {10, -5}, {0, -5}};
        box2d::PolyShape poly;
        poly.setupCompound(world, ell, 400, 300);
        check("setupCompound L with a reversing edge: created", poly.isCreated());
        check("setupCompound L with a reversing edge: mass = area (75) x density, not the hull's",
              abs(poly.getMass() / areaMass(75) - 1.0f) < 0.01f);
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
        const vector<Vec2> ell = {{0, 0}, {10, 0}, {5, 0}, {5, 5}, {10, 5}, {10, -5}, {0, -5}};
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(ell));
        float mass = rb->getBody() ? rb->getBody()->GetMass() : 0.0f;
        check("RigidBody2D compound L with a reversing edge: mass = area (75) x density, not the hull's",
              abs(mass / areaMass(75) - 1.0f) < 0.01f);
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

        // An Exit from inside a step is dispatched after it (update()).
        box.setPhysicsPosition(400, -1000);
        world.getWorld()->Step(1.0f / 60.0f, 8, 3);
        world.getCollisionManager()->update();
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
        EventListener l1 = bar->onCollisionEnter.listen([&](box2d::Contact2D&) { ++began; });
        EventListener l2 = bar->onCollisionStay.listen([&](box2d::Contact2D&) { ++stay; });
        EventListener l3 = bar->onCollisionExit.listen([&](box2d::Contact2D&) { ++ended; });
        EventListener l4 = box->onCollisionEnter.listen([&](box2d::Contact2D&) { ++boxBegan; });
        EventListener l5 = box->onCollisionExit.listen([&](box2d::Contact2D&) { ++boxEnded; });

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
        world.getCollisionManager()->update();
        check("RigidBody2D: separated", touchingContacts(world, bar->getBody(), box->getBody()) == 0);
        check("RigidBody2D: one Ended on each side", ended == 1 && boxEnded == 1);
    }
}

// All contacts between two bodies, touching or not.
static int contactsBetween(box2d::World& world, const b2Body* a, const b2Body* b) {
    int n = 0;
    for (b2Contact* c = world.getWorld()->GetContactList(); c; c = c->GetNext()) {
        const b2Body* ba = c->GetFixtureA()->GetBody();
        const b2Body* bb = c->GetFixtureB()->GetBody();
        if ((ba == a && bb == b) || (ba == b && bb == a)) ++n;
    }
    return n;
}

// Fixtures of `body` that a `size` px square centered on `spot` overlaps.
static int fixturesOverlapping(const b2Body* body, Vec2 spot, float size) {
    b2PolygonShape square;
    square.SetAsBox(box2d::World::toBox2d(size * 0.5f), box2d::World::toBox2d(size * 0.5f));
    b2Transform xf(box2d::World::toBox2d(spot.x, spot.y), b2Rot(0.0f));
    int n = 0;
    for (const b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext()) {
        if (b2TestOverlap(f->GetShape(), 0, &square, 0, body->GetTransform(), xf)) ++n;
    }
    return n;
}

// Two triangle fixtures of `body` that share an edge, and a point `inset` px
// inside each of them from the middle of that edge (world pixels), where a
// `size` px square overlaps that fixture only. The longest such edge.
static bool seamSpots(const b2Body* body, float inset, float size, Vec2& spot1, Vec2& spot2) {
    float best = 0.0f;
    for (const b2Fixture* f1 = body->GetFixtureList(); f1; f1 = f1->GetNext()) {
        for (const b2Fixture* f2 = f1->GetNext(); f2; f2 = f2->GetNext()) {
            auto* p1 = static_cast<const b2PolygonShape*>(f1->GetShape());
            auto* p2 = static_cast<const b2PolygonShape*>(f2->GetShape());
            if (p1->m_count != 3 || p2->m_count != 3) continue;
            for (int i = 0; i < 3; ++i) {
                b2Vec2 a = p1->m_vertices[i], b = p1->m_vertices[(i + 1) % 3];
                for (int j = 0; j < 3; ++j) {
                    b2Vec2 c = p2->m_vertices[j], d = p2->m_vertices[(j + 1) % 3];
                    if (!(b2DistanceSquared(a, d) < 1e-10f && b2DistanceSquared(b, c) < 1e-10f)) continue;
                    float len = b2Distance(a, b);
                    if (len <= best) continue;
                    b2Vec2 mid = 0.5f * (a + b);
                    b2Vec2 n(-(b.y - a.y) / len, (b.x - a.x) / len);
                    b2Vec2 o1 = p1->m_vertices[(i + 2) % 3];
                    if (b2Dot(n, o1 - mid) < 0) n = -n;
                    float d2 = box2d::World::toBox2d(inset);
                    Vec2 s1 = box2d::World::toPixels(body->GetWorldPoint(mid + d2 * n));
                    Vec2 s2 = box2d::World::toPixels(body->GetWorldPoint(mid - d2 * n));
                    if (fixturesOverlapping(body, s1, size) != 1 ||
                        fixturesOverlapping(body, s2, size) != 1) continue;
                    best = len;
                    spot1 = s1;
                    spot2 = s2;
                }
            }
        }
    }
    return best > 0.0f;
}

// A small sensor box slides back and forth across the seam between two
// fixtures of a compound, touching one of them at a time. Both contacts live
// on (their AABBs keep overlapping), so in one of the two directions Box2D
// ends the old contact before it begins the new one within the same step.
// The body pair touched before and after every step: no Exit / Ended.
static void testCompoundHandover() {
    const int crossings = 6;

    // Classic API: Collider2D events.
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        box2d::PolyShape bar;
        bar.setupCompound(world, kNotchedBar, 400, 300);
        bar.setStatic();
        Vec2 s1, s2;
        bool found = seamSpots(bar.getBody(), 4, 2, s1, s2);

        box2d::RectBody box;
        box.setup(world, s1.x, s1.y, 2, 2);
        box.setSensor(true);
        box.getBody()->SetSleepingAllowed(false);

        int barEnter = 0, barExit = 0, boxEnter = 0, boxExit = 0;
        auto* bc = bar.getCollider();
        auto* xc = box.getCollider();
        EventListener l1 = bc->onCollisionEnter.listen([&](box2d::CollisionEvent&) { ++barEnter; });
        EventListener l2 = bc->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++barExit; });
        EventListener l3 = xc->onCollisionEnter.listen([&](box2d::CollisionEvent&) { ++boxEnter; });
        EventListener l4 = xc->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++boxExit; });

        step(world, 1);
        bool oneAtATime = found;
        for (int i = 0; i < crossings; ++i) {
            const Vec2& s = (i % 2 == 0) ? s2 : s1;
            box.setPhysicsPosition(s.x, s.y);
            step(world, 1);
            if (touchingContacts(world, bar.getBody(), box.getBody()) != 1 ||
                contactsBetween(world, bar.getBody(), box.getBody()) < 2) oneAtATime = false;
        }
        check("Collider2D hand-over: one fixture touched at a time, both contacts kept", oneAtATime);
        check("Collider2D hand-over: one Enter, no Exit across the seam",
              barEnter == 1 && boxEnter == 1 && barExit == 0 && boxExit == 0);

        box.setPhysicsPosition(400, -1000);
        step(world, 1);
        check("Collider2D hand-over: one Exit when it leaves", barExit == 1 && boxExit == 1);
    }

    // Mod API: RigidBody2D events (trigger, as the box is a sensor). The
    // world is static: RigidBody2D keeps its contact routing per World
    // address, and a new world at a destroyed one's address would reuse it.
    {
        static box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        shared_ptr<Node> barNode, boxNode;
        auto* bar = attach(world, barNode, box2d::Shape2D::compound(kNotchedBar));
        bar->setBodyType(box2d::BodyType::Static);
        Vec2 s1, s2;
        bool found = bar->getBody() && seamSpots(bar->getBody(), 4, 2, s1, s2);

        boxNode = make_shared<Node>();
        boxNode->setPos(s1.x, s1.y);
        auto* box = boxNode->addMod<box2d::RigidBody2D>(world, box2d::Shape2D::box(2, 2));
        box->setTrigger(true);
        box->getBody()->SetSleepingAllowed(false);

        int began = 0, ended = 0, boxBegan = 0, boxEnded = 0;
        EventListener l1 = bar->onTriggerEnter.listen([&](box2d::Contact2D&) { ++began; });
        EventListener l2 = bar->onTriggerExit.listen([&](box2d::Contact2D&) { ++ended; });
        EventListener l3 = box->onTriggerEnter.listen([&](box2d::Contact2D&) { ++boxBegan; });
        EventListener l4 = box->onTriggerExit.listen([&](box2d::Contact2D&) { ++boxEnded; });

        step(world, 1);
        bool oneAtATime = found;
        for (int i = 0; i < crossings; ++i) {
            const Vec2& s = (i % 2 == 0) ? s2 : s1;
            box->getBody()->SetTransform(box2d::World::toBox2d(s.x, s.y), 0);
            step(world, 1);
            if (touchingContacts(world, bar->getBody(), box->getBody()) != 1 ||
                contactsBetween(world, bar->getBody(), box->getBody()) < 2) oneAtATime = false;
        }
        check("RigidBody2D hand-over: one fixture touched at a time, both contacts kept", oneAtATime);
        check("RigidBody2D hand-over: one Began, no Ended across the seam",
              began == 1 && boxBegan == 1 && ended == 0 && boxEnded == 0);

        box->getBody()->SetTransform(box2d::World::toBox2d(400, -1000), 0);
        step(world, 1);
        check("RigidBody2D hand-over: one Ended when it leaves", ended == 1 && boxEnded == 1);
    }
}

// Stay listeners that destroy bodies. Destroying a body ends its contacts at
// once (EndContact), while CollisionManager::update() is still walking its
// pairs: the next pairs must still get their Stay, the ended ones no more.
static void testStayListenerDestroys() {
    // Classic API: the bar's first Stay destroys the other box, whose pair
    // comes later in the list (the last one).
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        box2d::PolyShape bar;
        bar.setupCompound(world, kNotchedBar, 400, 300);
        bar.setStatic();
        box2d::RectBody box1, box2;
        box1.setup(world, 330, 283, 10, 10);   // 3 px into the bar's top (y 280)
        box2.setup(world, 470, 283, 10, 10);
        box1.setSensor(true);
        box2.setSensor(true);
        step(world, 1);
        bool setupOk = touchingContacts(world, bar.getBody(), box1.getBody()) > 0 &&
                       touchingContacts(world, bar.getBody(), box2.getBody()) > 0;

        box2d::RectBody* destroyed = nullptr;
        int barStay = 0, stay1 = 0, stay2 = 0, exit1 = 0, exit2 = 0;
        EventListener l1 = bar.getCollider()->onCollisionStay.listen([&](box2d::CollisionEvent& e) {
            ++barStay;
            if (destroyed) return;
            destroyed = (e.other == &box1) ? &box2 : &box1;
            destroyed->destroy();
        });
        EventListener l2 = box1.getCollider()->onCollisionStay.listen([&](box2d::CollisionEvent&) { ++stay1; });
        EventListener l3 = box2.getCollider()->onCollisionStay.listen([&](box2d::CollisionEvent&) { ++stay2; });
        EventListener l4 = box1.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++exit1; });
        EventListener l5 = box2.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++exit2; });

        world.getCollisionManager()->update();
        const bool gone1 = destroyed == &box1;
        check("Stay destroys the next pair's body: both boxes touched the bar", setupOk && destroyed);
        check("Stay destroys the next pair's body: one bar Stay, for the survivor", barStay == 1);
        check("Stay destroys the next pair's body: survivor Stay once, no Exit",
              (gone1 ? stay2 : stay1) == 1 && (gone1 ? exit2 : exit1) == 0);
        check("Stay destroys the next pair's body: the destroyed box gets Exit, no Stay",
              (gone1 ? stay1 : stay2) == 0 && (gone1 ? exit1 : exit2) == 1);

        world.getCollisionManager()->update();
        check("Stay destroys the next pair's body: later updates go on", barStay == 2);
    }

    // Classic API: the bar's first Stay destroys the box it is about (the
    // pair being dispatched). That box hears no Stay after it, and the other
    // pair, next in the list, still gets its Stay.
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        box2d::PolyShape bar;
        bar.setupCompound(world, kNotchedBar, 400, 300);
        bar.setStatic();
        box2d::RectBody box1, box2;
        box1.setup(world, 330, 283, 10, 10);
        box2.setup(world, 470, 283, 10, 10);
        box1.setSensor(true);
        box2.setSensor(true);
        step(world, 1);

        box2d::RectBody* destroyed = nullptr;
        int barStay = 0, staysAfter = 0, stay1 = 0, stay2 = 0, exit1 = 0, exit2 = 0;
        EventListener l1 = bar.getCollider()->onCollisionStay.listen([&](box2d::CollisionEvent& e) {
            ++barStay;
            if (destroyed) return;
            destroyed = (e.other == &box1) ? &box1 : &box2;
            destroyed->destroy();
        });
        EventListener l2 = box1.getCollider()->onCollisionStay.listen([&](box2d::CollisionEvent&) {
            ++stay1;
            if (destroyed == &box1) ++staysAfter;
        });
        EventListener l3 = box2.getCollider()->onCollisionStay.listen([&](box2d::CollisionEvent&) {
            ++stay2;
            if (destroyed == &box2) ++staysAfter;
        });
        EventListener l4 = box1.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++exit1; });
        EventListener l5 = box2.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++exit2; });

        world.getCollisionManager()->update();
        const bool gone1 = destroyed == &box1;
        check("Stay destroys its own pair's body: no Stay after it, one Exit",
              destroyed && staysAfter == 0 && (gone1 ? exit1 : exit2) == 1);
        check("Stay destroys its own pair's body: the next pair still gets its Stay",
              barStay == 2 && (gone1 ? stay2 : stay1) == 1 && (gone1 ? exit2 : exit1) == 0);
    }

    // Mod API: the bar's first Stay drops the other box's node (its
    // RigidBody2D destroys the body), whose body pair comes later. A static
    // world, as above.
    {
        static box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        shared_ptr<Node> barNode, node1, node2;
        auto* bar = attach(world, barNode, box2d::Shape2D::compound(kNotchedBar));
        bar->setBodyType(box2d::BodyType::Static);
        node1 = make_shared<Node>();
        node1->setPos(330, 283);
        auto* rb1 = node1->addMod<box2d::RigidBody2D>(world, box2d::Shape2D::box(10, 10));
        rb1->setTrigger(true);
        node2 = make_shared<Node>();
        node2->setPos(470, 283);
        auto* rb2 = node2->addMod<box2d::RigidBody2D>(world, box2d::Shape2D::box(10, 10));
        rb2->setTrigger(true);
        step(world, 1);

        int barStay = 0, barEnded = 0;
        bool dropped = false;
        EventListener l1 = bar->onTriggerStay.listen([&](box2d::Contact2D& c) {
            ++barStay;
            if (dropped) return;
            dropped = true;
            if (c.other == rb1) node2.reset(); else node1.reset();
        });
        // The dropped RigidBody2D unregisters before its body goes, so the
        // bar can't tell it was a trigger: count either kind of Ended.
        EventListener l2 = bar->onTriggerExit.listen([&](box2d::Contact2D&) { ++barEnded; });
        EventListener l3 = bar->onCollisionExit.listen([&](box2d::Contact2D&) { ++barEnded; });

        world.getCollisionManager()->update();
        check("RigidBody2D Stay drops the next pair's node: one bar Stay, one Ended",
              dropped && barStay == 1 && barEnded == 1);
        world.getCollisionManager()->update();
        check("RigidBody2D Stay drops the next pair's node: later updates go on", barStay == 2);
    }
}

// ---------------------------------------------------------------------------
// Event lifetimes: listeners that free bodies, and bodies destroyed before a
// deferred Exit / Ended fires (#427). Run under ASan too: without the guards
// these are use-after-frees, which a plain build may not notice.
// ---------------------------------------------------------------------------

// One step that ends the contacts, with the Exits left pending (no update()).
static void stepOnly(box2d::World& world) {
    world.getWorld()->Step(1.0f / 60.0f, 8, 3);
}

// A static 200 x 20 platform at (400, 300), and balls of radius 10 sunk 3 px
// into its top at the given x. Balls are sensors, so nothing pushes them out.
static const float kBallY = 300 - 10 - 10 + 3;

static void testClassicEventLifetimes() {
    // An Exit listener frees the other body: the pair's second notify must
    // not reach it.
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        auto platform = make_unique<box2d::RectBody>();
        platform->setup(world, 400, 300, 200, 20);
        platform->setStatic();
        auto ball = make_unique<box2d::CircleBody>();
        ball->setup(world, 400, kBallY, 10);
        ball->setSensor(true);
        step(world, 1);
        const bool touching = touchingContacts(world, platform->getBody(), ball->getBody()) > 0;

        int platformExit = 0, ballExit = 0;
        EventListener l1 = platform->getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) {
            ++platformExit;
            ball.reset();
        });
        EventListener l2 = ball->getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) {
            ++ballExit;
            platform.reset();
        });

        ball->setPhysicsPosition(400, -1000);
        step(world, 1);
        check("Exit listener frees the other body: the first side gets its Exit, the freed one none",
              touching && platformExit + ballExit == 1 && (!platform || !ball));
    }

    // Two balls leave in the same step; the first ball's Exit listener
    // frees the platform. The second ball still gets its Exit, with no other.
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        auto platform = make_unique<box2d::RectBody>();
        platform->setup(world, 400, 300, 200, 20);
        platform->setStatic();
        box2d::CircleBody ball1, ball2;
        ball1.setup(world, 340, kBallY, 10);
        ball2.setup(world, 460, kBallY, 10);
        ball1.setSensor(true);
        ball2.setSensor(true);
        step(world, 1);
        const bool touching = touchingContacts(world, platform->getBody(), ball1.getBody()) > 0 &&
                              touchingContacts(world, platform->getBody(), ball2.getBody()) > 0;

        int platformExit = 0, exit1 = 0, exit2 = 0, secondOtherNull = 0;
        EventListener l1 = platform->getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) {
            ++platformExit;
        });
        auto onExit = [&](int& count) {
            return [&count, &platform, &secondOtherNull](box2d::CollisionEvent& e) {
                ++count;
                if (platform) platform.reset();
                else if (e.other == nullptr) ++secondOtherNull;
            };
        };
        EventListener l2 = ball1.getCollider()->onCollisionExit.listen(onExit(exit1));
        EventListener l3 = ball2.getCollider()->onCollisionExit.listen(onExit(exit2));

        ball1.setPhysicsPosition(340, -1000);
        ball2.setPhysicsPosition(460, -1000);
        step(world, 1);
        check("two balls leave, the first frees the platform: one Exit each, the second with no other",
              touching && !platform && exit1 == 1 && exit2 == 1 && secondOtherNull == 1);
        check("two balls leave, the first frees the platform: at most one platform Exit",
              platformExit <= 1);
    }

    // Manual stepping: Step(), destroy the ball, make a new one (Box2D's
    // allocator hands out the freed body again), then update(). The platform
    // gets its Exit with no other; nothing reaches the freed ball or the new one.
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        box2d::RectBody platform;
        platform.setup(world, 400, 300, 200, 20);
        platform.setStatic();
        auto ball = make_unique<box2d::CircleBody>();
        ball->setup(world, 400, kBallY, 10);
        ball->setSensor(true);
        step(world, 1);

        int platformExit = 0, platformOtherNull = 0, oldBallExit = 0, newBallExit = 0;
        EventListener l1 = platform.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent& e) {
            ++platformExit;
            if (e.other == nullptr) ++platformOtherNull;
        });
        EventListener l2 = ball->getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) {
            ++oldBallExit;
        });

        ball->setPhysicsPosition(400, -1000);
        stepOnly(world);
        const b2Body* oldBody = ball->getBody();
        ball.reset();
        box2d::CircleBody newBall;
        newBall.setup(world, 100, -1000, 10);
        EventListener l3 = newBall.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) {
            ++newBallExit;
        });
        printf("  (classic manual step: new body %s the freed one's address)\n",
               newBall.getBody() == oldBody ? "reuses" : "does not reuse");
        world.getCollisionManager()->update();
        check("manual Step, destroy, update(): the platform's Exit has no other",
              platformExit == 1 && platformOtherNull == 1);
        check("manual Step, destroy, update(): nothing reaches the freed ball or a new one",
              oldBallExit == 0 && newBallExit == 0);
    }
}

// Counts a RigidBody2D's events, collision and trigger alike (a side whose
// other body is gone can't tell it was a trigger). `hook` runs after the
// count, with the phase (0 began, 1 stay, 2 ended).
struct ModCounts {
    int began = 0, stay = 0, ended = 0, otherNull = 0;
};

static vector<EventListener> countEvents(box2d::RigidBody2D* rb, ModCounts& n,
                                         function<void(int, box2d::Contact2D&)> hook = nullptr) {
    auto on = [&n, hook](int phase) {
        return [&n, hook, phase](box2d::Contact2D& c) {
            (phase == 0 ? n.began : phase == 1 ? n.stay : n.ended)++;
            if (!c.other) ++n.otherNull;
            if (hook) hook(phase, c);
        };
    };
    vector<EventListener> ls;
    ls.push_back(rb->onCollisionEnter.listen(on(0)));
    ls.push_back(rb->onCollisionStay.listen(on(1)));
    ls.push_back(rb->onCollisionExit.listen(on(2)));
    ls.push_back(rb->onTriggerEnter.listen(on(0)));
    ls.push_back(rb->onTriggerStay.listen(on(1)));
    ls.push_back(rb->onTriggerExit.listen(on(2)));
    return ls;
}

static box2d::RigidBody2D* addModBody(box2d::World& world, shared_ptr<Node>& node, float x, float y,
                                      const box2d::Shape2D& shape) {
    node = make_shared<Node>();
    node->setPos(x, y);
    return node->addMod<box2d::RigidBody2D>(world, shape);
}

static box2d::RigidBody2D* addPlatform(box2d::World& world, shared_ptr<Node>& node) {
    auto* rb = addModBody(world, node, 400, 300, box2d::Shape2D::box(200, 20));
    rb->setBodyType(box2d::BodyType::Static);
    return rb;
}

static box2d::RigidBody2D* addBall(box2d::World& world, shared_ptr<Node>& node, float x) {
    auto* rb = addModBody(world, node, x, kBallY, box2d::Shape2D::circle(10));
    rb->setTrigger(true);
    return rb;
}

// Each Mod case gets its own world: the contact router is kept per World*.
static void testModEventLifetimes() {
    // A deferred Ended listener drops the other node: the second side must
    // not be notified.
    {
        static box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        shared_ptr<Node> platformNode, ballNode;
        auto* platform = addPlatform(world, platformNode);
        auto* ball = addBall(world, ballNode, 400);
        step(world, 1);
        const bool touching = touchingContacts(world, platform->getBody(), ball->getBody()) > 0;

        ModCounts p, b;
        b2Body* ballBody = ball->getBody();
        auto l1 = countEvents(platform, p, [&](int phase, box2d::Contact2D&) {
            if (phase == 2) ballNode.reset();
        });
        auto l2 = countEvents(ball, b, [&](int phase, box2d::Contact2D&) {
            if (phase == 2) platformNode.reset();
        });

        ballBody->SetTransform(box2d::World::toBox2d(400, -1000), 0);
        step(world, 1);
        check("RigidBody2D Ended listener drops the other node: one Ended, the dropped side none",
              touching && p.ended + b.ended == 1 && (!platformNode || !ballNode));
    }

    // Two balls leave in the same step; the first ball's Ended listener
    // drops the platform. The second ball still gets its Ended, with no other.
    {
        static box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        shared_ptr<Node> platformNode, node1, node2;
        auto* platform = addPlatform(world, platformNode);
        auto* ball1 = addBall(world, node1, 340);
        auto* ball2 = addBall(world, node2, 460);
        step(world, 1);
        const bool touching = touchingContacts(world, platform->getBody(), ball1->getBody()) > 0 &&
                              touchingContacts(world, platform->getBody(), ball2->getBody()) > 0;

        ModCounts p, b1, b2;
        auto l1 = countEvents(platform, p);
        auto drop = [&](int phase, box2d::Contact2D&) {
            if (phase == 2 && platformNode) platformNode.reset();
        };
        auto l2 = countEvents(ball1, b1, drop);
        auto l3 = countEvents(ball2, b2, drop);

        ball1->getBody()->SetTransform(box2d::World::toBox2d(340, -1000), 0);
        ball2->getBody()->SetTransform(box2d::World::toBox2d(460, -1000), 0);
        step(world, 1);
        check("RigidBody2D two balls leave, the first drops the platform: one Ended each, the second with no other",
              touching && !platformNode && b1.ended == 1 && b2.ended == 1 &&
              b1.otherNull + b2.otherNull == 1);
        check("RigidBody2D two balls leave, the first drops the platform: at most one platform Ended",
              p.ended <= 1);
    }

    // Manual stepping: Step(), drop the ball's node, add a new ball (it gets
    // the freed b2Body's address), then update(). The platform's Ended has
    // no other; the new ball hears nothing.
    {
        static box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        shared_ptr<Node> platformNode, ballNode, newNode;
        auto* platform = addPlatform(world, platformNode);
        auto* ball = addBall(world, ballNode, 400);
        step(world, 1);

        ModCounts p, fresh;
        auto l1 = countEvents(platform, p);
        ball->getBody()->SetTransform(box2d::World::toBox2d(400, -1000), 0);
        stepOnly(world);
        const b2Body* oldBody = ball->getBody();
        ballNode.reset();
        auto* newBall = addModBody(world, newNode, 100, -1000, box2d::Shape2D::circle(10));
        auto l2 = countEvents(newBall, fresh);
        const bool reused = newBall->getBody() == oldBody;
        printf("  (Mod manual step: new body %s the freed one's address)\n", reused ? "reuses" : "does not reuse");
        world.getCollisionManager()->update();
        check("RigidBody2D manual Step, drop, update(): the platform's Ended has no other",
              p.ended == 1 && p.otherNull == 1);
        check("RigidBody2D manual Step, drop, update(): the new body at the freed address hears nothing",
              reused && fresh.began + fresh.stay + fresh.ended == 0);
    }

    // A Stay listener drops the other node of the pair being dispatched; a
    // second run also adds a new ball in its place (same b2Body address).
    for (bool replace : {false, true}) {
        static box2d::World worlds[2];
        box2d::World& world = worlds[replace ? 1 : 0];
        world.setup(0, 0);
        world.setAutoUpdate(false);

        shared_ptr<Node> platformNode, ballNode, newNode;
        auto* platform = addPlatform(world, platformNode);
        auto* ball = addBall(world, ballNode, 400);
        step(world, 1);

        ModCounts p, b, fresh;
        vector<EventListener> freshL;
        bool reused = false;
        auto dropOther = [&](shared_ptr<Node>* other) {
            return [&, other](int phase, box2d::Contact2D&) {
                if (phase != 1 || !platformNode || !ballNode) return;
                const b2Body* gone = (other == &ballNode) ? ball->getBody() : platform->getBody();
                other->reset();
                if (!replace) return;
                auto* rb = addModBody(world, newNode, 400, kBallY, box2d::Shape2D::circle(10));
                rb->setTrigger(true);
                reused = rb->getBody() == gone;
                freshL = countEvents(rb, fresh);
            };
        };
        auto l1 = countEvents(platform, p, dropOther(&ballNode));
        auto l2 = countEvents(ball, b, dropOther(&platformNode));

        world.getCollisionManager()->update();
        const string tag = replace ? " (and adds a new ball there)" : "";
        check("RigidBody2D Stay drops the other node of its pair" + tag + ": one Stay, the dropped side none",
              p.stay + b.stay == 1 && (!platformNode || !ballNode));
        if (replace) {
            check("RigidBody2D Stay drops the other node of its pair" + tag + ": the new ball hears nothing",
                  reused && fresh.began + fresh.stay + fresh.ended == 0);
        }
    }
}

// Outside-step Exits (#473): every wrapper finishes the Box2D operation
// before a listener destroys a third body. Both contact-list orders are used.
static void testExitListenerDestroysThirdBody() {
    const vector<string> operations = {"destroy", "disable", "static", "dynamic", "kinematic"};
    for (const auto& op : operations) {
        for (bool reverse : {false, true}) {
            box2d::World world;
            world.setup(0, 0);
            world.setAutoUpdate(false);
            box2d::RectBody a, b, c;
            a.setup(world, 400, 300, 200, 20);
            auto makeB = [&] { b.setup(world, 340, kBallY, 20, 20); b.setSensor(true); };
            auto makeC = [&] { c.setup(world, 460, kBallY, 20, 20); c.setSensor(true); };
            if (reverse) { makeC(); makeB(); } else { makeB(); makeC(); }
            if (op == "dynamic") b.setKinematic();
            step(world, 1);
            const bool touching = touchingContacts(world, a.getBody(), b.getBody()) > 0 &&
                                  touchingContacts(world, a.getBody(), c.getBody()) > 0;
            int ab = 0, ac = 0, bx = 0, cx = 0, ended = 0;
            bool completed = true;
            EventListener l1 = a.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent& e) {
                if (e.other == &b) {
                    ++ab;
                    // IsLocked() alone cannot detect DestroyBody/SetType's
                    // contact loop: inspect their completed state too.
                    completed = completed && !world.getWorld()->IsLocked();
                    if (op == "destroy") completed = completed && !b.isCreated();
                    else if (op == "disable") completed = completed && !b.isBodyEnabled();
                    else completed = completed && contactsBetween(world, a.getBody(), b.getBody()) == 0;
                    c.destroy();
                } else if (e.other == &c) ++ac;
            });
            EventListener l2 = b.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++bx; });
            EventListener l3 = c.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++cx; });
            EventListener l4 = world.getCollisionManager()->contactEnded.listen([&](box2d::WorldContact&) { ++ended; });
            if (op == "destroy") b.destroy();
            else if (op == "disable") b.setEnabled(false);
            else if (op == "static") b.setStatic();
            else if (op == "dynamic") b.setDynamic();
            else b.setKinematic();
            const string label = "outside-step " + op + (reverse ? " reverse" : "");
            check(label + ": listener sees completed Box2D operation, destroys C", touching && completed && !c.isCreated());
            check(label + ": each Exit once before wrapper returns", ab == 1 && ac == 1 && bx == 1 && cx == 1 && ended == 2);
            world.getCollisionManager()->update();
            check(label + ": update does not repeat Exits", ab == 1 && ac == 1 && bx == 1 && cx == 1 && ended == 2);
        }
    }

    // Also put B and C in contact: C's destruction used to free a contact
    // that B's DestroyBody/SetEnabled/SetType loop was still walking.
    for (const auto& op : operations) {
        for (bool reverse : {false, true}) {
            box2d::World world;
            world.setup(0, 0);
            world.setAutoUpdate(false);
            box2d::RectBody a, b, c;
            a.setup(world, 400, 300, 200, 20);
            auto makeB = [&] { b.setup(world, 397, kBallY, 20, 20); b.setSensor(true); };
            auto makeC = [&] { c.setup(world, 403, kBallY, 20, 20); c.setSensor(true); };
            if (reverse) { makeC(); makeB(); } else { makeB(); makeC(); }
            if (op == "dynamic") b.setKinematic();
            step(world, 1);
            const bool touching = touchingContacts(world, a.getBody(), b.getBody()) > 0 &&
                                  touchingContacts(world, a.getBody(), c.getBody()) > 0 &&
                                  touchingContacts(world, b.getBody(), c.getBody()) > 0;
            int ab = 0, ac = 0, ba = 0, bc = 0, ca = 0, cb = 0, ended = 0;
            EventListener la = a.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent& e) {
                if (e.other == &b) { ++ab; c.destroy(); } else if (e.other == &c) ++ac;
            });
            EventListener lb = b.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent& e) {
                if (e.other == &a) ++ba; else ++bc; // C may already have been forgotten
            });
            EventListener lc = c.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent& e) {
                if (e.other == &a) ++ca; else ++cb;
            });
            EventListener lw = world.getCollisionManager()->contactEnded.listen([&](box2d::WorldContact&) { ++ended; });
            if (op == "destroy") b.destroy();
            else if (op == "disable") b.setEnabled(false);
            else if (op == "static") b.setStatic();
            else if (op == "dynamic") b.setDynamic();
            else b.setKinematic();
            const string label = "shared-contact " + op + (reverse ? " reverse" : "");
            check(label + ": three pairs, each surviving side Exit once",
                  touching && !c.isCreated() && ab == 1 && ac == 1 && ba == 1 && bc == 1 && ca == 1 && cb <= 1 && ended == 3);
            // C's B Exit can already have fired before the listener drops C;
            // otherwise forget() suppresses that pending notification.
            const int before = ab + ac + ba + bc + ca + cb;
            world.getCollisionManager()->update();
            check(label + ": no repeated Exit", ab + ac + ba + bc + ca + cb == before && ended == 3);
        }
    }

    const vector<string> modOperations = {"teardown", "static", "dynamic", "kinematic"};
    // Distinct stable addresses for the Mod layer's per-World router.
    static box2d::World worlds[8];
    size_t index = 0;
    for (const auto& op : modOperations) {
        for (bool reverse : {false, true}) {
            auto& world = worlds[index++];
            world.setup(0, 0);
            world.setAutoUpdate(false);
            shared_ptr<Node> an, bn, cn;
            auto* a = addModBody(world, an, 400, 300, box2d::Shape2D::box(200, 20));
            box2d::RigidBody2D* b = nullptr;
            box2d::RigidBody2D* c = nullptr;
            auto makeB = [&] { b = addBall(world, bn, 340); };
            auto makeC = [&] { c = addBall(world, cn, 460); };
            if (reverse) { makeC(); makeB(); } else { makeB(); makeC(); }
            if (op == "dynamic") b->setBodyType(box2d::BodyType::Kinematic);
            step(world, 1);
            ModCounts na, nb, nc;
            bool completed = true;
            auto la = countEvents(a, na, [&](int phase, box2d::Contact2D&) {
                if (phase != 2 || !cn) return;
                completed = completed && !world.getWorld()->IsLocked();
                if (op == "teardown") completed = completed && world.getBodyCount() == 2;
                else completed = completed && contactsBetween(world, a->getBody(), b->getBody()) == 0;
                cn.reset();
            });
            auto lb = countEvents(b, nb);
            auto lc = countEvents(c, nc);
            int ended = 0;
            EventListener lw = world.getCollisionManager()->contactEnded.listen([&](box2d::WorldContact&) { ++ended; });
            if (op == "teardown") bn.reset();
            else b->setBodyType(op == "static" ? box2d::BodyType::Static
                              : op == "dynamic" ? box2d::BodyType::Dynamic : box2d::BodyType::Kinematic);
            const string label = "outside-step Mod " + op + (reverse ? " reverse" : "");
            check(label + ": completed operation, listener drops C", completed && !cn);
            check(label + ": each live side Exit once before return", na.ended == 2 && nb.ended == (op == "teardown" ? 0 : 1) && nc.ended == 0 && ended == 2);
            world.getCollisionManager()->update();
            check(label + ": update does not repeat Exits", na.ended == 2 && nb.ended == (op == "teardown" ? 0 : 1) && nc.ended == 0 && ended == 2);
        }
    }

    for (const string op : {"disable", "type"}) {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);
        box2d::RectBody a, b, c;
        a.setup(world, 400, 300, 200, 20);
        a.setStatic();
        b.setup(world, 340, kBallY, 20, 20);
        b.setSensor(true);
        c.setup(world, 460, kBallY, 20, 20);
        c.setSensor(true);
        step(world, 1);
        int ax = 0, bx = 0, cx = 0;
        EventListener la = a.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++ax; c.destroy(); });
        EventListener lb = b.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++bx; });
        EventListener lc = c.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++cx; });
        if (op == "disable") b.getBody()->SetEnabled(false);
        else if (op == "type") b.getBody()->SetType(b2_staticBody);
        check("raw " + op + ": no Exit inside Box2D call", ax == 0 && bx == 0 && cx == 0);
        world.getCollisionManager()->update();
        check("raw " + op + ": next update flushes exactly once", ax == 2 && bx == 1 && cx == 1);
        world.getCollisionManager()->update();
        check("raw " + op + ": no repeated Exit", ax == 2 && bx == 1 && cx == 1);
    }
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);
        box2d::RectBody a, c;
        a.setup(world, 400, 300, 200, 20);
        a.setStatic();
        c.setup(world, 460, kBallY, 20, 20);
        c.setSensor(true);
        b2BodyDef def;
        def.type = b2_dynamicBody;
        def.position = box2d::World::toBox2d(340, kBallY);
        b2Body* raw = world.getWorld()->CreateBody(&def);
        b2PolygonShape shape;
        shape.SetAsBox(box2d::World::toBox2d(10), box2d::World::toBox2d(10));
        b2FixtureDef fixture;
        fixture.shape = &shape;
        fixture.isSensor = true;
        fixture.density = 1;
        raw->CreateFixture(&fixture);
        step(world, 1);
        int ended = 0, ax = 0, cx = 0;
        EventListener lw = world.getCollisionManager()->contactEnded.listen([&](box2d::WorldContact&) {
            ++ended;
            if (c.isCreated()) c.destroy();
        });
        EventListener la = a.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++ax; });
        EventListener lc = c.getCollider()->onCollisionExit.listen([&](box2d::CollisionEvent&) { ++cx; });
        world.getWorld()->DestroyBody(raw);
        check("raw DestroyBody: no Exit inside Box2D call", ended == 0 && ax == 0 && cx == 0);
        world.getCollisionManager()->update();
        check("raw DestroyBody: next update can destroy C, each Exit once", ended == 2 && ax == 1 && cx == 1 && !c.isCreated());
        world.getCollisionManager()->update();
        check("raw DestroyBody: no repeated Exit", ended == 2 && ax == 1 && cx == 1);
    }

}

// The World's own DestroyBody calls: createBounds() replacing the walls
// (the new walls get the freed b2Body's address) and clear(). The world-level
// Ended deferred from the step must not name the freed walls or bodies.
static void testWorldEventLifetimes() {
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);
        world.createBounds(0, 0, 800, 600);

        box2d::CircleBody ball;
        ball.setup(world, 400, 600 - 10 + 3, 10);   // 3 px into the floor
        ball.setSensor(true);
        step(world, 1);

        int ended = 0;
        box2d::WorldContact last;
        EventListener l = world.getCollisionManager()->contactEnded.listen([&](box2d::WorldContact& c) {
            ++ended;
            last = c;
        });
        ball.setPhysicsPosition(400, 300);
        stepOnly(world);
        const b2Body* oldWalls = nullptr;
        for (b2Body* b = world.getWorld()->GetBodyList(); b; b = b->GetNext()) {
            if (b != ball.getBody()) oldWalls = b;
        }
        world.createBounds(0, 0, 800, 600);
        bool reused = false;
        for (b2Body* b = world.getWorld()->GetBodyList(); b; b = b->GetNext()) {
            if (b == oldWalls) reused = true;
        }
        world.getCollisionManager()->update();
        const bool ballAndNull = (last.a == ball.getBody() && !last.b) || (last.b == ball.getBody() && !last.a);
        check("createBounds() between Step() and update(): one Ended, naming the ball and no walls",
              reused && ended == 1 && ballAndNull);
    }
    {
        box2d::World world;
        world.setup(0, 0);
        world.setAutoUpdate(false);

        auto addDisc = [&](float x, float y) {
            b2BodyDef bd;
            bd.type = b2_dynamicBody;
            bd.position = box2d::World::toBox2d(x, y);
            b2Body* body = world.getWorld()->CreateBody(&bd);
            b2CircleShape c;
            c.m_radius = box2d::World::toBox2d(10);
            b2FixtureDef fd;
            fd.shape = &c;
            fd.isSensor = true;
            body->CreateFixture(&fd);
            return body;
        };
        b2Body* a = addDisc(400, 300);
        addDisc(415, 300);
        step(world, 1);

        int ended = 0;
        EventListener l = world.getCollisionManager()->contactEnded.listen([&](box2d::WorldContact&) { ++ended; });
        a->SetTransform(box2d::World::toBox2d(400, -1000), 0);
        stepOnly(world);
        world.clear();
        world.getCollisionManager()->update();
        check("clear() between Step() and update(): no Ended for the freed bodies", ended == 0);
    }
}

// ---------------------------------------------------------------------------
// Compound bodies: the inertia check runs once on the whole body (#427)
// ---------------------------------------------------------------------------

// A w x h rectangle centered on the origin whose corners are quarter circles
// of radius r, `segments` segments each (segments + 1 points per corner).
static vector<Vec2> roundedRectPoints(float w, float h, float r, int segments) {
    const float cx[4] = {w * 0.5f - r, -w * 0.5f + r, -w * 0.5f + r, w * 0.5f - r};
    const float cy[4] = {h * 0.5f - r, h * 0.5f - r, -h * 0.5f + r, -h * 0.5f + r};
    vector<Vec2> pts;
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= segments; ++i) {
            float a = QUARTER_TAU * c + QUARTER_TAU * i / segments;
            pts.push_back(Vec2(cx[c] + cos(a) * r, cy[c] + sin(a) * r));
        }
    }
    return pts;
}

static vector<Vec2> shifted(vector<Vec2> pts, float dx, float dy) {
    for (auto& p : pts) p += Vec2(dx, dy);
    return pts;
}

// True when `body` keeps a positive rotational inertia about its centroid at
// every density in `densities`. b2Body::ResetMassData() sets 1 / I there, so
// an impulse turns it the right way only if I > 0 (in Debug, I <= 0 asserts
// instead).
static bool inertiaPositive(b2Body* body, const vector<float>& densities) {
    if (!body) return false;
    bool ok = true;
    for (float d : densities) {
        for (b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext()) f->SetDensity(d);
        body->ResetMassData();
        body->SetAngularVelocity(0.0f);
        body->ApplyAngularImpulse(1.0f, true);
        float spin = body->GetAngularVelocity();
        if (!(isfinite(spin) && spin > 0.0f)) ok = false;
    }
    body->SetAngularVelocity(0.0f);
    return ok;
}

static void testCompoundOffset(box2d::World& world) {
    // Ordinary outlines have tiny ear triangles far (for their size) from the
    // body origin. Box2D only needs the whole body's inertia, so they are kept.
    struct Case {
        const char* name;
        vector<Vec2> pts;
        size_t triangles;
        float area;   // analytic
    };
    const vector<Case> cases = {
        {"600x40 rounded rectangle, 4 px corners", roundedRectPoints(600, 40, 4, 8), 34,
         600.0f * 40.0f - (4.0f - TAU * 0.5f) * 16.0f},
        {"radius-40 128-gon 1000 px off the origin", shifted(circlePoints(128, 40), 1000, 0), 126,
         TAU * 0.5f * 40.0f * 40.0f},
        {"radius-300 4096-gon", circlePoints(4096, 300), 4094, TAU * 0.5f * 300.0f * 300.0f},
    };
    for (const auto& c : cases) {
        const string name = string("setupCompound ") + c.name;
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, c.pts, 400, 300);
        check(name + ": created", poly.isCreated());
        check(name + ": no warning", w.count == 0);
        check(name + ": one fixture per triangle (" + to_string(c.triangles) + ")",
              fixtureCount(poly.getBody()) == int(c.triangles));
        check(name + ": mass = analytic area x density (0.5%)",
              abs(poly.getMass() / areaMass(c.area) - 1.0f) < 0.005f);
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(roundedRectPoints(600, 40, 4, 8)));
        check("RigidBody2D compound 600x40 rounded rectangle: 34 fixtures, no warning",
              fixtureCount(rb->getBody()) == 34 && w.count == 0);
    }

    // A small concave outline (3 triangles) is fine at the origin; 2000 px
    // away its whole body is too small for the offset: refused as a whole.
    const vector<Vec2> notch = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0, 0}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    {
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, notch, 400, 300);
        check("setupCompound 1 px notch at the origin: 3 fixtures, no warning",
              fixtureCount(poly.getBody()) == 3 && w.count == 0);
    }
    {
        int bodiesBefore = world.getBodyCount();
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, shifted(notch, 2000, 2000), 400, 300);
        check("setupCompound 1 px notch 2000 px off: no body", !poly.isCreated());
        check("setupCompound 1 px notch 2000 px off: no Box2D body", world.getBodyCount() == bodiesBefore);
        check("setupCompound 1 px notch 2000 px off: one warning, says too small",
              w.count == 1 && w.lastContains("too small for its distance"));
        check("setupCompound 1 px notch 2000 px off: no vertices", poly.getVertices().empty());
    }
    {
        WarningCapture w;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(shifted(notch, 2000, 2000)));
        check("RigidBody2D compound 1 px notch 2000 px off: no body, warning says too small",
              rb->getBody() == nullptr && w.count == 1 && w.lastContains("too small for its distance"));
    }

    // A small convex ring far from the origin is refused as one polygon for
    // its offset, not triangulated.
    {
        const vector<Vec2> tiny = {{600, 600}, {600.5f, 600}, {600, 600.5f}};
        WarningCapture w;
        box2d::PolyShape poly;
        poly.setupCompound(world, tiny, 0, 0);
        check("setupCompound tiny far triangle: no body", !poly.isCreated());
        check("setupCompound tiny far triangle: warning says too small",
              w.count == 1 && w.lastContains("too small for its distance") && !w.lastContains("no area"));

        Path path(tiny);
        path.close();
        box2d::detail::CompoundShapes shapes;
        bool made = box2d::detail::makeCompoundShapes(path, shapes);
        check("makeCompoundShapes tiny far triangle: refused as one polygon, not triangulated",
              !made && shapes.error == box2d::detail::PolygonError::TooSmallForOffset
                    && shapes.triangles == 0 && shapes.shapes.empty());

        WarningCapture w2;
        shared_ptr<Node> node;
        auto* rb = attach(world, node, box2d::Shape2D::compound(tiny));
        check("RigidBody2D compound tiny far triangle: no body, warning says too small",
              rb->getBody() == nullptr && w2.count == 1 && w2.lastContains("too small for its distance"));
    }

    // The combined check keeps its margin: just inside the distance where it
    // starts refusing, every density keeps the inertia positive. (With a
    // fixed 16-epsilon margin the 128-gon and larger fail here: Box2D's float
    // sum over many fixtures rounds differently at other densities.)
    {
        vector<float> densities = {0.001f, 0.0037f, 0.01f, 0.05f, 0.3f, 1.0f, 2.5f, 7.1f, 33.0f, 100.0f, 420.0f, 1000.0f};
        mt19937 rng(427);
        uniform_real_distribution<float> exponent(-3.0f, 3.0f);
        for (int i = 0; i < 20; ++i) densities.push_back(pow(10.0f, exponent(rng)));

        struct Outline {
            const char* name;
            vector<Vec2> pts;
        };
        const vector<Outline> outlines = {
            {"1 px notch", notch},
            {"600x40 rounded rectangle", roundedRectPoints(600, 40, 4, 8)},
            {"radius-40 128-gon", circlePoints(128, 40)},
            {"radius-300 1024-gon", circlePoints(1024, 300)},
        };
        for (const auto& o : outlines) {
            auto made = [&](double dist, box2d::detail::CompoundShapes& out) {
                Path path(shifted(o.pts, float(dist * 0.6), float(dist * 0.8)));
                path.close();
                return box2d::detail::makeCompoundShapes(path, out);
            };
            // Bisect the distance where the check starts refusing.
            double lo = 0.0, hi = 1.0;
            box2d::detail::CompoundShapes out;
            while (made(hi, out)) hi *= 2.0;
            for (int i = 0; i < 40; ++i) {
                double mid = (lo + hi) * 0.5;
                (made(mid, out) ? lo : hi) = mid;
            }
            // Build the accepted bodies just inside through setupCompound()
            // (in Debug, Box2D asserts while creating one if any step of it
            // loses the inertia), then try every density on them.
            int tried = 0, bad = 0;
            for (int k = 0; k < 20; ++k) {
                const double dist = lo * (1.0 - k * 1e-3);
                if (!made(dist, out)) continue;
                ++tried;
                box2d::PolyShape poly;
                poly.setupCompound(world, shifted(o.pts, float(dist * 0.6), float(dist * 0.8)), 0, 0);
                if (!inertiaPositive(poly.getBody(), densities)) ++bad;
            }
            {
                // RigidBody2D creates its fixtures its own way.
                shared_ptr<Node> node = make_shared<Node>();
                auto* rb = node->addMod<box2d::RigidBody2D>(
                    world, box2d::Shape2D::compound(shifted(o.pts, float(lo * 0.6), float(lo * 0.8))));
                if (!inertiaPositive(rb->getBody(), densities)) ++bad;
            }
            printf("  %s: refused from %.0f px, %d accepted bodies just inside\n", o.name, hi, tried);
            check(string("combined inertia check, ") + o.name + ": positive at every density just inside",
                  tried > 0 && bad == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Density and body type after creation (#427). Polygon and compound fixtures
// are added at density 0 and get their density once all are in: every
// fixture must end up with it, and changing the density or the body type
// later must give the mass data of a body made that way directly.
// ---------------------------------------------------------------------------

// True when `body` has fixtures and every one of them has density `d`.
static bool everyFixtureDensity(const b2Body* body, float d) {
    if (!body || !body->GetFixtureList()) return false;
    for (const b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext()) {
        if (f->GetDensity() != d) return false;
    }
    return true;
}

struct MassProps {
    float mass = 0.0f;
    float inertia = 0.0f;   // about the body origin
    b2Vec2 center = b2Vec2_zero;
};

static MassProps massProps(const b2Body* body) {
    MassProps m;
    if (!body) return m;
    m.mass = body->GetMass();
    m.inertia = body->GetInertia();
    m.center = body->GetLocalCenter();
    return m;
}

// The same fixtures at the same density go through the same float sums in
// the same order, so the mass data must match exactly.
static bool sameMass(const MassProps& got, const MassProps& want) {
    return want.mass > 0.0f && got.mass == want.mass && got.inertia == want.inertia && got.center == want.center;
}

// `pts` moved `dist` px from the origin along (0.6, 0.8).
static vector<Vec2> offsetBy(const vector<Vec2>& pts, double dist) {
    return shifted(pts, float(dist * 0.6), float(dist * 0.8));
}

// The largest distance (bisected) at which `accepted` still takes `pts`.
static double acceptLimit(const vector<Vec2>& pts, const function<bool(const vector<Vec2>&)>& accepted) {
    double lo = 0.0, hi = 1.0;
    while (hi < 1e9 && accepted(offsetBy(pts, hi))) hi *= 2.0;
    for (int i = 0; i < 40; ++i) {
        double mid = (lo + hi) * 0.5;
        (accepted(offsetBy(pts, mid)) ? lo : hi) = mid;
    }
    return lo;
}

static void testDensityAndType(box2d::World& world) {
    // Outlines just inside the offset limit: a mass reset over only some of
    // the fixtures, or at a density the check did not cover, would lose the
    // inertia there (a Box2D assert in Debug).
    const vector<Vec2> gon = circlePoints(128, 40);
    const vector<Vec2> hex = circlePoints(6, 2);
    const double gonLimit = acceptLimit(gon, [](const vector<Vec2>& p) {
        Path path(p);
        path.close();
        box2d::detail::CompoundShapes out;
        return box2d::detail::makeCompoundShapes(path, out);
    });
    const double hexLimit = acceptLimit(hex, [](const vector<Vec2>& p) {
        b2PolygonShape shape;
        vector<Vec2> hull;
        return box2d::detail::makePolygonShape(p, shape, hull) == box2d::detail::PolygonError::None;
    });

    struct Kind {
        string name;
        vector<Vec2> pts;
        bool compound;
    };
    const vector<Kind> kinds = {
        {"compound 128-gon near the offset limit", offsetBy(gon, gonLimit), true},
        {"polygon hexagon near the offset limit", offsetBy(hex, hexLimit), false},
    };
    auto modBody = [&](const Kind& k, shared_ptr<Node>& node, box2d::BodyType type, float d) {
        node = make_shared<Node>();
        node->setPos(400, 300);
        auto shape = k.compound ? box2d::Shape2D::compound(k.pts) : box2d::Shape2D::polygon(k.pts);
        return node->addMod<box2d::RigidBody2D>(world, shape, type, d);
    };
    auto classicBody = [&](const Kind& k, box2d::PolyShape& poly) {
        if (k.compound) poly.setupCompound(world, k.pts, 400, 300);
        else poly.setup(world, k.pts, 400, 300);
    };
    const vector<float> densities = {0.001f, 2.5f, 1000.0f};

    for (const auto& k : kinds) {
        // (a) Every fixture has the requested density once the body exists.
        {
            WarningCapture w;
            box2d::PolyShape poly;
            classicBody(k, poly);
            const int want = k.compound ? 126 : 1;
            check("PolyShape " + k.name + ": created, no warning, " + to_string(want) + " fixture(s)",
                  poly.isCreated() && w.count == 0 && fixtureCount(poly.getBody()) == want);
            check("PolyShape " + k.name + ": every fixture density 1",
                  everyFixtureDensity(poly.getBody(), 1.0f));
        }
        {
            shared_ptr<Node> node;
            auto* rb = modBody(k, node, box2d::BodyType::Dynamic, 1.0f);
            check("RigidBody2D " + k.name + ": every fixture density 1 (default)",
                  everyFixtureDensity(rb->getBody(), 1.0f));
            shared_ptr<Node> node2;
            auto* rb2 = modBody(k, node2, box2d::BodyType::Dynamic, 2.5f);
            check("RigidBody2D " + k.name + ": every fixture density 2.5 (dynamic)",
                  everyFixtureDensity(rb2->getBody(), 2.5f));
            shared_ptr<Node> node3;
            auto* rb3 = modBody(k, node3, box2d::BodyType::Static, 2.5f);
            check("RigidBody2D " + k.name + ": every fixture density 2.5 (static)",
                  everyFixtureDensity(rb3->getBody(), 2.5f));
        }

        // (b) Density and body type changed afterwards give the mass data of
        // a body created that way.
        bool staticThenDynamic = true, densityLater = true, bothLater = true;
        bool classicDensity = true, classicStaticDensityDynamic = true;
        for (float d : densities) {
            shared_ptr<Node> directNode;
            const MassProps direct = massProps(modBody(k, directNode, box2d::BodyType::Dynamic, d)->getBody());
            {
                // Created static at d, then made dynamic.
                shared_ptr<Node> node;
                auto* rb = modBody(k, node, box2d::BodyType::Static, d);
                const bool wasStatic = rb->getBody() && rb->getBody()->GetMass() == 0.0f;
                rb->setBodyType(box2d::BodyType::Dynamic);
                if (!wasStatic || !sameMass(massProps(rb->getBody()), direct)) staticThenDynamic = false;
            }
            {
                // Created dynamic at density 1, then given d.
                shared_ptr<Node> node;
                auto* rb = modBody(k, node, box2d::BodyType::Dynamic, 1.0f);
                rb->setDensity(d);
                if (!everyFixtureDensity(rb->getBody(), d) || !sameMass(massProps(rb->getBody()), direct)) {
                    densityLater = false;
                }
            }
            {
                // Created static at density 1, given d, then made dynamic.
                shared_ptr<Node> node;
                auto* rb = modBody(k, node, box2d::BodyType::Static, 1.0f);
                rb->setDensity(d);
                rb->setBodyType(box2d::BodyType::Dynamic);
                if (!sameMass(massProps(rb->getBody()), direct)) bothLater = false;
            }
            {
                box2d::PolyShape poly;
                classicBody(k, poly);
                poly.setDensity(d);
                if (!everyFixtureDensity(poly.getBody(), d) || !sameMass(massProps(poly.getBody()), direct)) {
                    classicDensity = false;
                }
            }
            {
                box2d::PolyShape poly;
                classicBody(k, poly);
                poly.setStatic();
                poly.setDensity(d);
                poly.setDynamic();
                if (!sameMass(massProps(poly.getBody()), direct)) classicStaticDensityDynamic = false;
            }
        }
        check("RigidBody2D " + k.name + ": static, then setBodyType(Dynamic) = created dynamic",
              staticThenDynamic);
        check("RigidBody2D " + k.name + ": setDensity() later = created at that density", densityLater);
        check("RigidBody2D " + k.name + ": static, setDensity(), setBodyType(Dynamic) = created so",
              bothLater);
        check("PolyShape " + k.name + ": setDensity() = RigidBody2D created at that density", classicDensity);
        check("PolyShape " + k.name + ": setStatic(), setDensity(), setDynamic() = created so",
              classicStaticDensityDynamic);
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

// The old event names (Began / Ended) are deprecated aliases of the new ones
// (#496): the same Event objects, so a listener on either name sees the same
// notifications.
static void testDeprecatedEventAliases(box2d::World& world) {
    auto node = make_shared<Node>();
    auto* rb = node->addMod<box2d::RigidBody2D>(world, box2d::Shape2D::box(4, 4));
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    check("deprecated aliases: onCollisionBegan is onCollisionEnter", &rb->onCollisionBegan == &rb->onCollisionEnter);
    check("deprecated aliases: onCollisionEnded is onCollisionExit", &rb->onCollisionEnded == &rb->onCollisionExit);
    check("deprecated aliases: onTriggerBegan is onTriggerEnter", &rb->onTriggerBegan == &rb->onTriggerEnter);
    check("deprecated aliases: onTriggerEnded is onTriggerExit", &rb->onTriggerEnded == &rb->onTriggerExit);
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif
}

int main() {
    box2d::World world;
    world.setup(0, 0);
    world.setAutoUpdate(false);

    testPolyShapeRefuses(world);
    testHullDropWarning(world);
    testPolyShapeValid(world);
    testSetupSimplified(world);
    testRigidBody2D(world);
    testPolygonPath(world);
    testCompoundShapes(world);
    testCompoundFilters(world);
    testCompoundHole();
    testCompoundEvents();
    testCompoundHandover();
    testStayListenerDestroys();
    testClassicEventLifetimes();
    testModEventLifetimes();
    testWorldEventLifetimes();
    testExitListenerDestroysThirdBody();
    testCompoundOffset(world);
    testDensityAndType(world);
    testReducedConvexHull();
    testDeprecatedEventAliases(world);

    if (g_fail) {
        printf("\n%d check(s) FAILED\n", g_fail);
        return 1;
    }
    printf("\nAll checks passed\n");
    return 0;
}
