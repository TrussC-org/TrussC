#pragma once

#include <tcMath.h>
#include <box2d/box2d.h>
#include <memory>
#include <unordered_set>

namespace tc = trussc;

namespace tcx::box2d {

class RigidBody2D;

// Experimental v3 world. Pixel methods use 30 pixels per meter.
// Raw ids use Box2D units (meters); the World owns its id.
// Pixel coordinates match TrussC (y down); no axis flip, so raw Box2D meters
// are y-down too and upstream samples' negative-y gravity becomes positive here.
class World {
public:
    World() = default;
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    // Replaces any previous world. Uses upstream defaults, with one worker by default.
    void setup();
    // One synchronous fixed step of 1/60 second; no-op before setup().
    void update();
    // Positive sub-step count passed directly to b2World_Step (default 4).
    // Non-positive values are ignored.
    void setSubSteps(int count);
    // Pixels per second squared; may also be set before setup().
    void setGravity(const tc::Vec2& gravity);
    // Upstream built-in task system; defaults to one, changeable at runtime.
    void setWorkerCount(int count);
    b2WorldId id() const { return worldId_; }

private:
    friend class RigidBody2D;
    struct Owners {
        b2WorldId worldId = b2_nullWorldId;
        std::unordered_set<RigidBody2D*> bodies;
    };
    std::shared_ptr<Owners> owners_ = std::make_shared<Owners>();
    b2WorldId worldId_ = b2_nullWorldId;
    tc::Vec2 gravity_{0.0f, 300.0f}; // 10 m/s^2 downward in TrussC's y-down pixels
    int subSteps_ = 4;
    int workerCount_ = 1;
};

} // namespace tcx::box2d
