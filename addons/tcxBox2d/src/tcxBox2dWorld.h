// =============================================================================
// tcxBox2dWorld.h - Box2D World Management
// =============================================================================

#pragma once

#include <TrussC.h>
#include <box2d/box2d.h>
#include <memory>
#include <vector>

namespace tcx::box2d {

// Forward declarations
class Body;
class CollisionManager;

// =============================================================================
// Box2D World
// =============================================================================
// Main class for managing physics simulation
// =============================================================================
class World {
public:
    // Pixel/meter conversion scale (default: 30px = 1m)
    static float scale;

    World();
    ~World();

    // Non-copyable
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Movable
    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------

    // Initialize world with gravity
    void setup(const tc::Vec2& gravity = tc::Vec2(0, 10));
    void setup(float gravityX, float gravityY);

    // -------------------------------------------------------------------------
    // Simulation
    // -------------------------------------------------------------------------

    // Advance physics simulation. With auto-update ON (default) this is called
    // automatically once per frame via tc::events().update — you usually don't
    // call it yourself. It is framerate-independent: real getDeltaTime() is
    // accumulated and the world is stepped in FIXED sub-steps (see
    // setSimulationRate). Calling it manually more than once in the same frame is
    // a no-op (deduplicated via getFrameCount), so old code that calls update()
    // each frame keeps working without double-stepping.
    void update();

    // Auto-update: step automatically each frame (default ON). Turn OFF to drive
    // the simulation yourself (manual update(), pausing, custom ordering, etc.).
    void setAutoUpdate(bool on);
    bool getAutoUpdate() const { return autoUpdate_; }

    // Simulation parameters
    void setFPS(float fps);               // alias of setSimulationRate
    // Fixed simulation sub-step rate in Hz (default: 60). This is the physics
    // stability knob, decoupled from the display refresh rate: at 1000 the world
    // steps in 1/1000 s increments (~16 sub-steps per 60 fps frame). Higher =
    // more accurate/stable, more CPU.
    void setSimulationRate(float hz);
    float getSimulationRate() const { return timeStep_ > 0 ? 1.0f / timeStep_ : 0.0f; }
    void setVelocityIterations(int n);    // Velocity iterations (default: 8)
    void setPositionIterations(int n);    // Position iterations (default: 3)

    // Gravity
    void setGravity(const tc::Vec2& gravity);
    void setGravity(float x, float y);
    tc::Vec2 getGravity() const;

    // -------------------------------------------------------------------------
    // Bounds (walls at screen edges)
    // -------------------------------------------------------------------------

    // Create walls at screen edges. The four walls are one static body, and
    // collision events count per body pair: a RigidBody2D already touching
    // one wall gets no new onCollisionBegan when it reaches another, and
    // onCollisionEnded only when it leaves the last one.
    void createBounds(float x, float y, float width, float height);
    void createBounds();  // Create with current window size

    // Create ground only (at bottom of screen)
    void createGround(float y, float width);
    void createGround();  // Create with current window size

    // -------------------------------------------------------------------------
    // Body Management
    // -------------------------------------------------------------------------

    // Remove all registered bodies
    void clear();

    // Body count
    int getBodyCount() const;

    // -------------------------------------------------------------------------
    // Point Query (get body at specified position)
    // -------------------------------------------------------------------------
    Body* getBodyAtPoint(const tc::Vec2& point);
    Body* getBodyAtPoint(float x, float y);

    // -------------------------------------------------------------------------
    // Mouse Drag (using b2MouseJoint)
    // -------------------------------------------------------------------------

    // Start dragging (specify body and start position)
    void startDrag(Body* body, const tc::Vec2& target);
    void startDrag(Body* body, float x, float y);

    // Update drag (update mouse position)
    void updateDrag(const tc::Vec2& target);
    void updateDrag(float x, float y);

    // End dragging
    void endDrag();

    // Check if dragging
    bool isDragging() const;

    // Get drag anchor position (connection point on body side)
    tc::Vec2 getDragAnchor() const;

    // -------------------------------------------------------------------------
    // Coordinate Conversion
    // -------------------------------------------------------------------------

    // Pixel coordinates → Box2D coordinates
    static b2Vec2 toBox2d(const tc::Vec2& v);
    static b2Vec2 toBox2d(float x, float y);
    static float toBox2d(float val);

    // Box2D coordinates → Pixel coordinates
    static tc::Vec2 toPixels(const b2Vec2& v);
    static float toPixels(float val);

    // -------------------------------------------------------------------------
    // Direct Access to Box2D
    // -------------------------------------------------------------------------
    b2World* getWorld() { return world_.get(); }
    const b2World* getWorld() const { return world_.get(); }

    // -------------------------------------------------------------------------
    // Collision Manager Access
    // -------------------------------------------------------------------------
    CollisionManager* getCollisionManager() { return collisionManager_.get(); }

    // -------------------------------------------------------------------------
    // Lifetime token (for Mods: detect a world destroyed before its bodies)
    // -------------------------------------------------------------------------
    std::weak_ptr<int> aliveToken() const { return alive_; }

private:
    // (Re)register or drop the per-frame auto-step listener based on autoUpdate_.
    void refreshAutoUpdate();

    std::unique_ptr<b2World> world_;
    std::unique_ptr<CollisionManager> collisionManager_;

    // Simulation parameters (timeStep_ = fixed sub-step size)
    float timeStep_ = 1.0f / 60.0f;
    int velocityIterations_ = 8;
    int positionIterations_ = 3;

    // Auto-update / framerate-independent stepping
    bool autoUpdate_ = true;
    float accumulator_ = 0.0f;
    uint64_t lastStepFrame_ = UINT64_MAX;
    tc::EventListener updateListener_;

    // Lifetime token: expires when this World is destroyed.
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    // Bounds body
    b2Body* groundBody_ = nullptr;

    // Mouse drag
    b2MouseJoint* mouseJoint_ = nullptr;
    b2Body* dragAnchorBody_ = nullptr;  // Static body for joint anchor
};

} // namespace tcx::box2d
