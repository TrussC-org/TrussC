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

    // Advance physics simulation by one step
    void update();

    // Simulation parameters
    void setFPS(float fps);               // FPS (default: 60)
    void setVelocityIterations(int n);    // Velocity iterations (default: 8)
    void setPositionIterations(int n);    // Position iterations (default: 3)

    // Gravity
    void setGravity(const tc::Vec2& gravity);
    void setGravity(float x, float y);
    tc::Vec2 getGravity() const;

    // -------------------------------------------------------------------------
    // Bounds (walls at screen edges)
    // -------------------------------------------------------------------------

    // Create walls at screen edges
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

private:
    std::unique_ptr<b2World> world_;

    // Simulation parameters
    float timeStep_ = 1.0f / 60.0f;
    int velocityIterations_ = 8;
    int positionIterations_ = 3;

    // Bounds body
    b2Body* groundBody_ = nullptr;

    // Mouse drag
    b2MouseJoint* mouseJoint_ = nullptr;
    b2Body* dragAnchorBody_ = nullptr;  // Static body for joint anchor
};

} // namespace tcx::box2d
