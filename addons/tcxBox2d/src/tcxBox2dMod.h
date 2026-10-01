#pragma once

// =============================================================================
// tcxBox2dMod.h - Box2D physics as Node Mods (EXPERIMENTAL).
//
//   auto n = make_shared<Node>();
//   n->setPos(x, y);
//   addChild(n);
//   n->addMod<box2d::RigidBody2D>(box2d::Shape2D::circle(30));
//   n->addMod<box2d::ColliderRenderer2D>()->setColor(Color(1, 0.4f, 0.1f));
//   // → the node now falls under 2D physics AND draws its shape; no custom Node.
//   // The world auto-steps each frame (box2d::defaultWorld() / setup()).
//
// This mirrors the tcxPhysics (3D / Jolt) Mod design, in 2D:
//   - RigidBody2D owns ONE b2Body (shape + body type + physics material).
//   - ColliderRenderer2D is a SEPARATE draw Mod reading the sibling shape.
//   - defaultWorld() removes the need to pass a World around.
//   - a per-world contact router fans world contacts out to per-node events.
//
// The classic API (box2d::World + CircleBody/RectBody/PolyShape) is untouched;
// this is an additive, higher-level path that rides the core Node/Mod system.
//
// Naming: the "2D" suffix avoids clashing with tcxPhysics' tcx::RigidBody /
// tcx::ColliderRenderer when both addons are `using namespace tcx;`'d together.
//
// EXPERIMENTAL: rides the core Node/Mod API; expect churn while that settles.
// Assumes the owner node lives in the X/Y plane (2D), rotated only about Z.
// =============================================================================

#include "tcxBox2dWorld.h"
#include "tcxCollisionManager.h"
#include "tcxBox2dPolygonCheck.h"
#include <TrussC.h>   // tc::Mod, tc::Node, drawing, TC_REFLECT
#include <box2d/box2d.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <cmath>

namespace tcx::box2d {

// =============================================================================
// Shape2D - what a RigidBody2D is made of (pixel units, local/centered).
// =============================================================================
struct Shape2D {
    enum Kind { Circle, Box, Polygon, Compound };
    Kind kind = Circle;
    float radius = 30.0f;                 // circle
    tc::Vec2 size{60.0f, 60.0f};          // box: full width/height
    std::vector<tc::Vec2> verts;          // polygon: local verts (centered), convex, 3..8
    tc::Path path;                        // compound: local outline (centered), any shape

    static Shape2D circle(float r) {
        Shape2D s; s.kind = Circle; s.radius = r; return s;
    }
    static Shape2D box(float w, float h) {
        Shape2D s; s.kind = Box; s.size = tc::Vec2(w, h); return s;
    }
    static Shape2D box(float size) { return box(size, size); }
    // One convex polygon of 3 to 8 points. Concave input becomes its convex
    // hull, as Box2D does: when the hull leaves points inside it (concave
    // input, or interior points), the first such RigidBody2D in the process
    // logs one warning with the counts, pointing to compound(); points on the
    // outline (collinear, duplicates) and a crossing order don't count. Once
    // the body exists, RigidBody2D::shape() (and so
    // ColliderRenderer2D) holds that hull. Convex input that already goes
    // around its outline in order (either winding, any start) is kept as
    // given; otherwise (points dropped, or listed in a crossing order) the
    // hull is in Box2D's order (from the rightmost point). More than 8
    // points, or degenerate points (collinear, nearly coincident, tiny next
    // to their distance from the origin), log a warning when the RigidBody2D
    // is attached and create no body: getBody() stays null and
    // ColliderRenderer2D draws nothing. For more points use simplified()
    // (convex approximation) or compound() (exact shape).
    static Shape2D polygon(const std::vector<tc::Vec2>& v) {
        Shape2D s; s.kind = Polygon; s.verts = v; return s;
    }
    // Same as above with every point of the path (all subpaths together), so
    // the path must have 3 to 8 points in total.
    static Shape2D polygon(const tc::Path& path) {
        return polygon(detail::pathPoints(path));
    }
    // Any number of points (3 or more), approximated by their convex hull
    // reduced to at most 8 points (the vertices that lose the least area are
    // dropped; tips and extents can shrink). Concave parts and holes are
    // filled. Degenerate input behaves like polygon(): a warning and no body.
    static Shape2D simplified(const std::vector<tc::Vec2>& points) {
        Shape2D s; s.kind = Polygon; s.verts = detail::reducedConvexHull(points); return s;
    }
    // Same as above with every point of the path (all subpaths together).
    static Shape2D simplified(const tc::Path& path) {
        return simplified(detail::pathPoints(path));
    }
    // Any outline, kept exactly: concave, with holes (a subpath wound opposite
    // to its enclosing one), any number of points. It is triangulated like
    // Path::drawFill() and each triangle becomes one fixture of the one body
    // (a single convex ring of at most 8 points stays one polygon fixture).
    // Slivers Box2D can't use (collinear or nearly coincident corners, almost
    // no area) are skipped with one warning; if nothing is left, or the whole
    // body is tiny next to its distance from the origin, a warning and no
    // body. Collision events come once per touching body pair, however many
    // fixtures touch. Area covered more than once (a self-overlapping
    // outline, overlapping same-direction subpaths, a hole wound like its
    // outer ring) weighs once per layer: give a simple outline and wind holes
    // opposite.
    static Shape2D compound(const tc::Path& outline) {
        Shape2D s; s.kind = Compound; s.path = outline; return s;
    }
    // Same as above with the points as one closed outline.
    static Shape2D compound(const std::vector<tc::Vec2>& points) {
        tc::Path p(points);
        p.close();
        return compound(p);
    }
    static Shape2D regularPolygon(float radius, int sides) {
        Shape2D s; s.kind = Polygon;
        if (sides < 3) sides = 3;
        if (sides > 8) sides = 8;   // Box2D supports 3..8 vertices
        const float step = tc::TAU / sides;
        for (int i = 0; i < sides; ++i) {
            float a = i * step - tc::QUARTER_TAU;   // start from top
            s.verts.push_back(tc::Vec2(std::cos(a) * radius, std::sin(a) * radius));
        }
        return s;
    }
};

// Dynamic   = simulated (physics drives the node).
// Static    = a fixed obstacle placed at the node's transform when attached.
// Kinematic = YOU drive the node; the body follows (velocity-derived) and pushes
//             dynamics. Good for moving platforms / paddles / doors.
enum class BodyType { Dynamic, Static, Kinematic };
TC_ENUM_LABELS(BodyType, "Dynamic", "Static", "Kinematic")

// Process-wide default world, so addMod<RigidBody2D>(shape) needs no world passed.
// Point it at your own instance with setDefaultWorld(); otherwise the first access
// lazily creates a built-in one. setup() it once (the app usually does
// defaultWorld().setup(gravity)); auto-update steps it each frame.
inline World*& currentDefaultWorld_() { static World* p = nullptr; return p; }
inline void setDefaultWorld(World& world) { currentDefaultWorld_() = &world; }
inline World& defaultWorld() {
    World*& p = currentDefaultWorld_();
    if (!p) { static World builtin; p = &builtin; }
    return *p;
}

class RigidBody2D;  // fwd

// Argument for RigidBody2D collision/trigger events, from the receiving body's
// point of view. `other` is the body it touched, or null if that was a
// non-RigidBody2D body (the bounds/ground, or a raw CircleBody/RectBody/...)
// or if its node went away before this event (an Exit deferred to after the
// physics step, or a listener that ran first dropped it).
struct Contact2D {
    RigidBody2D* other = nullptr;
    tc::Node*    otherNode = nullptr;   // other ? other->getOwner() : nullptr
    tc::Vec2     point;                 // pixels (zero on Exit)
    tc::Vec2     normal;                // world-space (zero on Exit)
};

namespace detail {
    // One router per world: maps b2Body* -> the RigidBody2D that owns it, plus
    // the listeners on that world's CollisionManager contact events. The first
    // RigidBody2D on a world installs the listeners; everyone registers here.
    // Each registration gets its own serial: a body destroyed by a listener
    // and a new one created at the same address are told apart.
    struct ContactRouter2D {
        struct Entry {
            RigidBody2D* rb = nullptr;
            uint64_t serial = 0;   // 0 = not registered
        };
        std::unordered_map<b2Body*, Entry> bodies;
        uint64_t lastSerial = 0;
        tc::EventListener beganL, stayL, endedL;
    };
    inline std::unordered_map<World*, ContactRouter2D>& contactRouters() {
        static std::unordered_map<World*, ContactRouter2D> r;
        return r;
    }
    // phase: 0 = began, 1 = stay, 2 = ended.
    inline void routeContact(World* w, const WorldContact& c, int phase);  // after RigidBody2D
}

// =============================================================================
// RigidBody2D : the physics Mod (one b2Body).
// =============================================================================
class RigidBody2D : public tc::Mod {
    // addMod() calls setup() through a RigidBody2D* (not Mod*), so Node needs
    // access to our protected override.
    friend class trussc::Node;
    friend void detail::routeContact(World* w, const WorldContact& c, int phase);
    friend class ColliderRenderer2D;   // draws fillMesh_

public:
    RigidBody2D(World& world, const Shape2D& shape,
                BodyType type = BodyType::Dynamic, float density = 1.0f)
        : world_(&world), shape_(shape), type_(type), density_(density) {}

    RigidBody2D(const Shape2D& shape,
                BodyType type = BodyType::Dynamic, float density = 1.0f)
        : world_(&defaultWorld()), shape_(shape), type_(type), density_(density) {}

    b2Body* getBody() { return body_; }
    const b2Body* getBody() const { return body_; }
    const Shape2D& shape() const { return shape_; }

    // True while THIS mod is writing the body transform into the node
    // (earlyUpdate sync). Lets app code distinguish "physics moved the node"
    // from "someone edited the node".
    bool isSyncing() const { return syncing_; }

    // --- PHYSICS material (chainable; applied immediately if the body exists) --
    RigidBody2D& setDensity(float d) {
        density_ = d;
        if (body_) { forFixtures([d](b2Fixture* f) { f->SetDensity(d); }); body_->ResetMassData(); }
        return *this;
    }
    RigidBody2D& setFriction(float f) {
        friction_ = f;
        if (body_) forFixtures([f](b2Fixture* fx) { fx->SetFriction(f); });
        return *this;
    }
    RigidBody2D& setRestitution(float r) {
        restitution_ = r;
        if (body_) forFixtures([r](b2Fixture* fx) { fx->SetRestitution(r); });
        return *this;
    }

    // Make this body a trigger (sensor): detects overlaps, never blocks motion.
    RigidBody2D& setTrigger(bool on = true) {
        trigger_ = on;
        if (body_) forFixtures([on](b2Fixture* f) { f->SetSensor(on); });
        return *this;
    }
    bool isTrigger() const { return trigger_; }

    RigidBody2D& setFixedRotation(bool fixed) {
        fixedRotation_ = fixed;
        if (body_) body_->SetFixedRotation(fixed);
        return *this;
    }
    bool isFixedRotation() const { return body_ ? body_->IsFixedRotation() : fixedRotation_; }

    // Switch how the body moves at runtime.
    RigidBody2D& setBodyType(BodyType t) {
        type_ = t;
        if (body_) {
            body_->SetType(t == BodyType::Dynamic ? b2_dynamicBody
                         : t == BodyType::Static  ? b2_staticBody
                                                  : b2_kinematicBody);
        }
        return *this;
    }
    BodyType getBodyType() const { return type_; }

    // Inspector-facing getters (live values when the body exists).
    float getDensity() const     { return (body_ && body_->GetFixtureList()) ? body_->GetFixtureList()->GetDensity()     : density_; }
    float getFriction() const    { return (body_ && body_->GetFixtureList()) ? body_->GetFixtureList()->GetFriction()    : (friction_    >= 0 ? friction_    : 0.3f); }
    float getRestitution() const { return (body_ && body_->GetFixtureList()) ? body_->GetFixtureList()->GetRestitution() : (restitution_ >= 0 ? restitution_ : 0.5f); }

    // Collision events (listen via EventListener; fired on the main thread).
    tc::Event<Contact2D> onCollisionEnter;   // started touching
    tc::Event<Contact2D> onCollisionStay;    // still touching, once per World::update()
    tc::Event<Contact2D> onCollisionExit;    // stopped touching

    // Trigger events — fired instead of the collision ones when EITHER side is a
    // trigger (sensor). `point`/`normal` come from the overlap (zero on Exit).
    tc::Event<Contact2D> onTriggerEnter;
    tc::Event<Contact2D> onTriggerStay;
    tc::Event<Contact2D> onTriggerExit;

    // Old names (Began / Ended), kept as aliases of the events above until
    // v1.0.0. References add nothing to copying: tc::Event already makes
    // RigidBody2D non-copyable and non-movable, and reflection lists values only.
    [[deprecated("use onCollisionEnter")]] tc::Event<Contact2D>& onCollisionBegan = onCollisionEnter;
    [[deprecated("use onCollisionExit")]]  tc::Event<Contact2D>& onCollisionEnded = onCollisionExit;
    [[deprecated("use onTriggerEnter")]]   tc::Event<Contact2D>& onTriggerBegan   = onTriggerEnter;
    [[deprecated("use onTriggerExit")]]    tc::Event<Contact2D>& onTriggerEnded   = onTriggerExit;

    // Reflection: live, editable physics state in inspectors / MCP node tree.
    TC_REFLECT(RigidBody2D, tc::Mod) {
        TC_VALUE(bodyType, getBodyType, setBodyType)
        TC_VALUE(density, getDensity)                 // no setter = read-only
        TC_VALUE(friction, getFriction, setFriction)
        TC_VALUE(restitution, getRestitution, setRestitution)
        TC_VALUE(trigger, isTrigger, setTrigger)
        TC_VALUE(fixedRotation, isFixedRotation, setFixedRotation)
    }

protected:
    void setup() override {
        tc::Node* n = getOwner();
        worldAlive_ = world_->aliveToken();
        b2World* w = world_->getWorld();
        if (!w) {
            tc::logWarning() << "tcxBox2d: RigidBody2D attached before world.setup() — body not created.";
            return;
        }

        // Check polygon points before creating anything (the same check as
        // PolyShape::setup()). Box2D would assert (Debug) or build a 2x2 m box
        // or a hull of the first 8 points (Release) for points it can't use.
        std::vector<b2PolygonShape> polys;
        if (shape_.kind == Shape2D::Polygon) {
            b2PolygonShape poly;
            std::vector<tc::Vec2> hull;
            detail::PolygonError err = detail::makePolygonShape(shape_.verts, poly, hull);
            if (err != detail::PolygonError::None) {
                auto log = tc::logWarning();
                log << "tcxBox2d: RigidBody2D polygon has " << shape_.verts.size() << " points: ";
                if (err == detail::PolygonError::TooFewPoints) {
                    // Shape2D::simplified() may have reduced many points to these
                    // few, so "needs at least 3 points" would mislead. Shape2D
                    // doesn't record which factory made it, so say "may".
                    log << detail::describeCollapsedHull("Shape2D::simplified()", true) << ".";
                } else {
                    log << detail::describePolygonError(err) << ".";
                }
                if (err == detail::PolygonError::TooManyPoints) {
                    log << " Use Shape2D::simplified() for a convex approximation or"
                        << " Shape2D::compound() for the exact shape.";
                }
                log << " Body not created.";
                return;
            }
            // Concave input becomes its hull, as Box2D does: say so once per
            // process, so a scene that builds many bodies doesn't flood the log.
            const size_t dropped = detail::countPointsInsideHull(shape_.verts, hull);
            static tc::OnceGate droppedWarned;
            if (dropped > 0 && droppedWarned.isFirstTime()) {
                tc::logWarning() << "tcxBox2d: Shape2D::polygon() dropped " << dropped << " of "
                                 << shape_.verts.size() << " points inside the convex hull;"
                                 << " use Shape2D::compound() to keep the exact shape.";
            }
            shape_.verts = hull;   // draw what collides
            polys.push_back(poly);
        } else if (shape_.kind == Shape2D::Compound) {
            detail::CompoundShapes shapes;
            if (!detail::makeCompoundShapes(shape_.path, shapes)) {
                if (shapes.error == detail::PolygonError::TooSmallForOffset) {
                    tc::logWarning() << "tcxBox2d: RigidBody2D compound has " << shape_.path.size()
                                     << " points: " << detail::describePolygonError(shapes.error)
                                     << ". Body not created.";
                } else {
                    tc::logWarning() << "tcxBox2d: RigidBody2D compound has " << shape_.path.size()
                                     << " points with no area Box2D can use (" << shapes.triangles
                                     << " triangles, none usable). Body not created.";
                }
                return;
            }
            if (shapes.skipped > 0) {
                tc::logWarning() << "tcxBox2d: RigidBody2D compound skipped " << shapes.skipped
                                 << " of " << shapes.triangles
                                 << " triangles that are slivers Box2D can't use (collinear or nearly"
                                 << " coincident corners, almost no area).";
            }
            polys = std::move(shapes.shapes);
            fillMesh_ = detail::makeFillMesh(shapes.fill);
        }

        // Physics is world-space — create the body at the node's global pose.
        tc::Vec3 wpos = n->getGlobalPos();
        b2BodyDef bd;
        bd.type = (type_ == BodyType::Dynamic) ? b2_dynamicBody
                : (type_ == BodyType::Static)  ? b2_staticBody
                                               : b2_kinematicBody;
        bd.position = World::toBox2d(wpos.x, wpos.y);
        bd.angle = globalRotZ(n);
        bd.fixedRotation = fixedRotation_;
        body_ = w->CreateBody(&bd);

        // Do NOT store anything in the body UserData: World::getBodyAtPoint()
        // casts it to box2d::Body* and we are not one. The router keys on the
        // raw b2Body* instead.
        body_->GetUserData().pointer = 0;

        createFixtures(polys);
        if (trigger_) forFixtures([](b2Fixture* f) { f->SetSensor(true); });

        // Register for contact routing; the first body on this world hooks the
        // CollisionManager's contact events.
        auto& router = detail::contactRouters()[world_];
        if (!router.beganL) {
            if (auto* cm = world_->getCollisionManager()) {
                World* wp = world_;
                router.beganL = cm->contactBegan.listen([wp](WorldContact& c) { detail::routeContact(wp, c, 0); });
                router.stayL  = cm->contactStay.listen ([wp](WorldContact& c) { detail::routeContact(wp, c, 1); });
                router.endedL = cm->contactEnded.listen([wp](WorldContact& c) { detail::routeContact(wp, c, 2); });
            }
        }
        router.bodies[body_] = {this, ++router.lastSerial};
    }

    // Dynamic: physics drives the node — sync BEFORE Node::update() so user code
    // sees the fresh transform.
    void earlyUpdate() override {
        if (type_ != BodyType::Dynamic || !body_) return;
        tc::Node* n = getOwner();
        if (n->isDead()) return;
        syncing_ = true;
        tc::Vec2 px = World::toPixels(body_->GetPosition());
        auto parent = n->getParent();
        if (parent) {
            tc::Vec3 lp = parent->globalToLocal(tc::Vec3(px.x, px.y, 0.0f));
            n->setPos(lp.x, lp.y, n->getZ());
        } else {
            n->setPos(px.x, px.y, n->getZ());
        }
        n->setRot(body_->GetAngle() - parentGlobalRotZ(n));
        syncing_ = false;
    }

    // Kinematic: the node drives the body — sync AFTER Node::update(). Derive the
    // velocity from the delta so the body shoves the dynamics it meets.
    void update() override {
        if (type_ != BodyType::Kinematic || !body_) return;
        tc::Node* n = getOwner();
        if (n->isDead()) return;
        float dt = static_cast<float>(tc::getDeltaTime());
        if (dt <= 0.0f) return;
        tc::Vec3 wp = n->getGlobalPos();
        b2Vec2 target = World::toBox2d(wp.x, wp.y);
        b2Vec2 cur = body_->GetPosition();
        body_->SetLinearVelocity(b2Vec2((target.x - cur.x) / dt, (target.y - cur.y) / dt));
        float dAng = globalRotZ(n) - body_->GetAngle();
        body_->SetAngularVelocity(dAng / dt);
    }

    void onDestroy() override {
        auto it = detail::contactRouters().find(world_);
        if (it != detail::contactRouters().end()) it->second.bodies.erase(body_);
        // Only touch the world if it's still alive — at shutdown it may be
        // destroyed before its bodies' nodes (it frees all bodies itself).
        if (!worldAlive_.expired() && world_ && body_) {
            // A deferred Exit must not name this body once it is freed
            if (auto* cm = world_->getCollisionManager()) cm->forget(body_);
            if (auto* w = world_->getWorld()) w->DestroyBody(body_);
        }
        body_ = nullptr;
    }

    bool isExclusive() const override { return true; }

private:
    // Build a Contact2D (from this body's view) and fire the matching event.
    void fireContact(RigidBody2D* other, const WorldContact& c, int phase) {
        Contact2D col;
        col.other     = other;
        col.otherNode = other ? other->getOwner() : nullptr;
        col.point     = c.point;
        col.normal    = c.normal;
        bool trigger = trigger_ || (other && other->trigger_);
        if (trigger) {
            switch (phase) {
                case 0:  onTriggerEnter.notify(col); break;
                case 1:  onTriggerStay.notify(col);  break;
                default: onTriggerExit.notify(col); break;
            }
        } else {
            switch (phase) {
                case 0:  onCollisionEnter.notify(col); break;
                case 1:  onCollisionStay.notify(col);  break;
                default: onCollisionExit.notify(col); break;
            }
        }
    }

    // polys: the checked polygons, one fixture each, used when shape_ is a
    // Polygon or a Compound.
    void createFixtures(const std::vector<b2PolygonShape>& polys) {
        b2FixtureDef fd;
        fd.density     = density_;
        fd.friction    = (friction_    >= 0.0f) ? friction_    : 0.3f;
        fd.restitution = (restitution_ >= 0.0f) ? restitution_ : 0.5f;
        switch (shape_.kind) {
            case Shape2D::Circle: {
                b2CircleShape c;
                c.m_radius = World::toBox2d(shape_.radius);
                fd.shape = &c;
                body_->CreateFixture(&fd);
                break;
            }
            case Shape2D::Box: {
                b2PolygonShape box;
                box.SetAsBox(World::toBox2d(shape_.size.x * 0.5f),
                             World::toBox2d(shape_.size.y * 0.5f));
                fd.shape = &box;
                body_->CreateFixture(&fd);
                break;
            }
            case Shape2D::Polygon:
            case Shape2D::Compound: {
                // Added at density 0, then given the density at once:
                // CreateFixture() resets the mass data after every fixture
                // that has a density, and a compound body's first few
                // triangles alone can be too small for their offset (an
                // m_I > 0 assert in Debug) even though the whole body passed
                // the check.
                fd.density = 0.0f;
                for (const auto& poly : polys) {
                    fd.shape = &poly;
                    body_->CreateFixture(&fd);
                }
                forFixtures([this](b2Fixture* f) { f->SetDensity(density_); });
                body_->ResetMassData();
                break;
            }
        }
    }

    template<typename F>
    void forFixtures(F&& f) {
        if (!body_) return;
        for (b2Fixture* fx = body_->GetFixtureList(); fx; fx = fx->GetNext()) f(fx);
    }

    // 2D: rotation is a single Z angle. Sum the parent chain (valid for pure-Z
    // rotations, which is the 2D assumption).
    static float parentGlobalRotZ(tc::Node* n) {
        float a = 0.0f;
        for (auto p = n->getParent(); p; p = p->getParent()) a += p->getRot();
        return a;
    }
    static float globalRotZ(tc::Node* n) { return n->getRot() + parentGlobalRotZ(n); }

    World* world_ = nullptr;
    bool syncing_ = false;
    Shape2D shape_;
    BodyType type_ = BodyType::Dynamic;
    float density_ = 1.0f;
    float friction_ = -1.0f;       // <0 = use default at create
    float restitution_ = -1.0f;    // <0 = use default at create
    bool trigger_ = false;
    bool fixedRotation_ = false;
    b2Body* body_ = nullptr;
    tc::Mesh fillMesh_;   // compound fill, triangulated once in setup()
    std::weak_ptr<int> worldAlive_;
};

namespace detail {
// Fan a world contact out to the RigidBody2D(s) involved (main thread). Each
// side hears about the OTHER body; a side that isn't a RigidBody2D is null.
inline void routeContact(World* w, const WorldContact& c, int phase) {
    using Entry = ContactRouter2D::Entry;
    auto find = [w](b2Body* body) -> Entry {
        auto it = contactRouters().find(w);
        if (!body || it == contactRouters().end()) return {};
        auto f = it->second.bodies.find(body);
        return (f != it->second.bodies.end()) ? f->second : Entry{};
    };
    const Entry ea = find(c.a);
    const Entry eb = find(c.b);
    if (ea.rb) ea.rb->fireContact(eb.rb, c, phase);

    // A's listeners may have dropped B's node (and made a new body at the
    // same address) or A's: notify B only while it is still registered.
    if (!eb.rb || find(c.b).serial != eb.serial) return;
    RigidBody2D* ra = (ea.rb && find(c.a).serial == ea.serial) ? ea.rb : nullptr;
    eb.rb->fireContact(ra, c, phase);
}
} // namespace detail

// =============================================================================
// ColliderRenderer2D : draw the sibling RigidBody2D's shape (origin-centered).
// =============================================================================
class ColliderRenderer2D : public tc::Mod {
    friend class trussc::Node;

public:
    ColliderRenderer2D& setColor(const tc::Color& c) { color_ = c; return *this; }
    tc::Color getColor() const { return color_; }
    ColliderRenderer2D& setFilled(bool f) { filled_ = f; return *this; }
    bool isFilled() const { return filled_; }

    TC_REFLECT(ColliderRenderer2D, tc::Mod) {
        TC_VALUE(color, getColor, setColor)
        TC_VALUE(filled, isFilled, setFilled)
    }

protected:
    void draw() override {
        tc::Node* n = getOwner();
        if (!n->hasMod<RigidBody2D>()) return;
        const RigidBody2D* rb = n->getMod<RigidBody2D>();
        const Shape2D& s = rb->shape();

        tc::setColor(color_);
        filled_ ? tc::fill() : tc::noFill();

        switch (s.kind) {
            case Shape2D::Circle:
                tc::drawCircle(0.0f, 0.0f, s.radius);
                if (!filled_) tc::drawLine(0.0f, 0.0f, s.radius, 0.0f);  // rotation indicator
                break;
            case Shape2D::Box:
                tc::drawRect(-s.size.x * 0.5f, -s.size.y * 0.5f, s.size.x, s.size.y);
                break;
            case Shape2D::Polygon:
                // Only a polygon Box2D accepted. Without a body (refused
                // points, or no world yet) verts are the raw points: they
                // may be concave, and nothing collides there (PolyShape
                // draws nothing either).
                if (rb->getBody()) drawPolygon(s.verts);
                break;
            case Shape2D::Compound:
                // Only an outline Box2D accepted (PolyShape draws nothing
                // without a body either). The fill is Path::drawFill()'s,
                // triangulated once when the body was made.
                if (!rb->getBody()) break;
                filled_ ? rb->fillMesh_.draw() : detail::drawPathOutline(s.path);
                break;
        }
    }

    bool isExclusive() const override { return true; }

private:
    void drawPolygon(const std::vector<tc::Vec2>& verts) {
        if (verts.size() < 3) return;
        if (filled_) {
            // Fan from the first vertex: verts is convex (checked by
            // makePolygonShape, since a body exists), but the body origin
            // need not lie inside it.
            tc::Mesh mesh;
            mesh.setMode(tc::PrimitiveMode::TriangleFan);
            for (const auto& v : verts) mesh.addVertex(tc::Vec3(v.x, v.y, 0.0f));
            mesh.draw();
        } else {
            for (size_t i = 0; i < verts.size(); ++i) {
                const auto& a = verts[i];
                const auto& b = verts[(i + 1) % verts.size()];
                tc::drawLine(a.x, a.y, b.x, b.y);
            }
        }
    }

    tc::Color color_{1.0f, 1.0f, 1.0f};
    bool filled_ = false;
};

} // namespace tcx::box2d
