#pragma once

#include "TrussC.h"
#include <memory>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstdint>

// =============================================================================
// trussc namespace
// =============================================================================
namespace trussc {

// Forward declaration
class Node;
using NodePtr = std::shared_ptr<Node>;
using NodeWeakPtr = std::weak_ptr<Node>;

// Hover state cache (updated once per frame)
namespace internal {
    inline Node* hoveredNode = nullptr;      // Currently hovered node
    inline Node* prevHoveredNode = nullptr;  // Previously hovered node
}

// =============================================================================
// Node - Scene graph base class
// All nodes inherit from this class
// =============================================================================

class Node : public std::enable_shared_from_this<Node> {
public:
    using Ptr = std::shared_ptr<Node>;
    using WeakPtr = std::weak_ptr<Node>;

    virtual ~Node() = default;

    // -------------------------------------------------------------------------
    // Lifecycle (overridable)
    // -------------------------------------------------------------------------

    virtual void setup() {}
    virtual void update() {}
    virtual void draw() {}
    virtual void cleanup() {}

    // -------------------------------------------------------------------------
    // Tree operations
    // -------------------------------------------------------------------------

    // Add child node
    // keepGlobalPosition: if true, preserves child's global position
    void addChild(Ptr child, bool keepGlobalPosition = false) {
        if (!child || child.get() == this) return;

        // If preserving global position, record position before move
        float globalX = 0, globalY = 0;
        if (keepGlobalPosition) {
            child->localToGlobal(0, 0, globalX, globalY);
        }

        // Remove from existing parent
        if (auto oldParent = child->parent_.lock()) {
            oldParent->removeChild(child);
        }

        child->parent_ = weak_from_this();
        children_.push_back(child);

        // If preserving global position, recalculate local coordinates relative to new parent
        if (keepGlobalPosition) {
            float localX, localY;
            globalToLocal(globalX, globalY, localX, localY);
            child->x = localX;
            child->y = localY;
        }
    }

    // Remove child node
    void removeChild(Ptr child) {
        if (!child) return;

        auto it = std::find(children_.begin(), children_.end(), child);
        if (it != children_.end()) {
            (*it)->parent_.reset();
            children_.erase(it);
        }
    }

    // Remove all child nodes
    void removeAllChildren() {
        for (auto& child : children_) {
            child->parent_.reset();
        }
        children_.clear();
    }

    // Get parent node
    Ptr getParent() const {
        return parent_.lock();
    }

    // Get list of child nodes
    const std::vector<Ptr>& getChildren() const {
        return children_;
    }

    // Get number of child nodes
    size_t getChildCount() const {
        return children_.size();
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    bool isActive = true;    // false: update/draw are skipped
    bool isVisible = true;   // false: only draw is skipped

    // Event enabling (only nodes that called enableEvents() are hit test targets)
    void enableEvents() { eventsEnabled_ = true; }
    void disableEvents() { eventsEnabled_ = false; }
    bool isEventsEnabled() const { return eventsEnabled_; }

    // Whether mouse is over this node (auto-updated each frame, O(1))
    bool isMouseOver() const { return internal::hoveredNode == this; }

    // -------------------------------------------------------------------------
    // Transform
    // -------------------------------------------------------------------------

    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;      // Radians
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    // Set rotation in degrees
    void setRotationDeg(float degrees) {
        rotation = degrees * PI / 180.0f;
    }

    // Get rotation in degrees
    float getRotationDeg() const {
        return rotation * 180.0f / PI;
    }

    // -------------------------------------------------------------------------
    // Coordinate transformation
    // -------------------------------------------------------------------------

    // Get local transform matrix for this node
    Mat4 getLocalMatrix() const {
        Mat4 mat = Mat4::translate(x, y, 0.0f);
        if (rotation != 0.0f) {
            mat = mat * Mat4::rotateZ(rotation);
        }
        if (scaleX != 1.0f || scaleY != 1.0f) {
            mat = mat * Mat4::scale(scaleX, scaleY, 1.0f);
        }
        return mat;
    }

    // Get global transform matrix for this node (includes parent transforms)
    Mat4 getGlobalMatrix() const {
        Mat4 local = getLocalMatrix();
        if (auto p = parent_.lock()) {
            return p->getGlobalMatrix() * local;
        }
        return local;
    }

    // Get inverse of global transform matrix
    Mat4 getGlobalMatrixInverse() const {
        return getGlobalMatrix().inverted();
    }

    // Convert global coordinates to this node's local coordinates
    void globalToLocal(float globalX, float globalY, float& localX, float& localY) const {
        // First convert to parent's local coordinates
        float parentLocalX = globalX;
        float parentLocalY = globalY;
        if (auto p = parent_.lock()) {
            p->globalToLocal(globalX, globalY, parentLocalX, parentLocalY);
        }

        // Convert from parent's local to this node's local coordinates
        // Inverse of translate
        float dx = parentLocalX - x;
        float dy = parentLocalY - y;

        // Inverse of rotate
        float cosR = std::cos(-rotation);
        float sinR = std::sin(-rotation);
        float rx = dx * cosR - dy * sinR;
        float ry = dx * sinR + dy * cosR;

        // Inverse of scale
        localX = (scaleX != 0.0f) ? rx / scaleX : rx;
        localY = (scaleY != 0.0f) ? ry / scaleY : ry;
    }

    // Convert local coordinates to global coordinates
    void localToGlobal(float localX, float localY, float& globalX, float& globalY) const {
        // Apply scale
        float sx = localX * scaleX;
        float sy = localY * scaleY;

        // Apply rotate
        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);
        float rx = sx * cosR - sy * sinR;
        float ry = sx * sinR + sy * cosR;

        // Apply translate
        float tx = rx + x;
        float ty = ry + y;

        // If parent exists, apply parent's local-to-global transform
        if (auto p = parent_.lock()) {
            p->localToGlobal(tx, ty, globalX, globalY);
        } else {
            globalX = tx;
            globalY = ty;
        }
    }

    // -------------------------------------------------------------------------
    // Mouse coordinates (in local coordinate system)
    // -------------------------------------------------------------------------

    // Mouse X in this node's local coordinate system
    float getMouseX() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalMouseX(), trussc::getGlobalMouseY(), lx, ly);
        return lx;
    }

    // Mouse Y in this node's local coordinate system
    float getMouseY() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalMouseX(), trussc::getGlobalMouseY(), lx, ly);
        return ly;
    }

    // Previous frame mouse X (in local coordinate system)
    float getPMouseX() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalPMouseX(), trussc::getGlobalPMouseY(), lx, ly);
        return lx;
    }

    // Previous frame mouse Y (in local coordinate system)
    float getPMouseY() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalPMouseX(), trussc::getGlobalPMouseY(), lx, ly);
        return ly;
    }

    // -------------------------------------------------------------------------
    // Recursive update/draw (called automatically by framework)
    // -------------------------------------------------------------------------

    // Recursively update self and child nodes
    void updateTree() {
        if (!isActive) return;

        processTimers();
        update();  // User code

        // Automatically update child nodes
        for (auto& child : children_) {
            child->updateTree();
        }
    }

    // Recursively draw self and child nodes
    virtual void drawTree() {
        if (!isActive) return;

        pushMatrix();

        // Apply transforms
        translate(x, y);
        if (rotation != 0.0f) {
            rotate(rotation);
        }
        if (scaleX != 1.0f || scaleY != 1.0f) {
            scale(scaleX, scaleY);
        }

        // User drawing
        if (isVisible) {
            draw();
        }

        // Automatically draw child nodes
        for (auto& child : children_) {
            child->drawTree();
        }

        popMatrix();
    }

    // -------------------------------------------------------------------------
    // Ray-based Hit Test (for event dispatch)
    // -------------------------------------------------------------------------

    // Hit test result
    struct HitResult {
        Ptr node;           // Hit node
        float distance;     // Distance from ray origin
        Vec3 localPoint;    // Hit position in local coordinates

        bool hit() const { return node != nullptr; }
    };

    // Hit test entire tree with global ray, return frontmost node
    // Traversed in reverse draw order (later drawn = higher priority)
    HitResult findHitNode(const Ray& globalRay) {
        return findHitNodeRecursive(globalRay, getGlobalMatrixInverse());
    }

    // Dispatch mouse event to tree (for 2D mode)
    // screenX, screenY: screen coordinates
    // return: node that handled event (nullptr if not handled)
    Ptr dispatchMousePress(float screenX, float screenY, int button) {
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            Vec2 local(result.localPoint.x, result.localPoint.y);
            if (result.node->onMousePress(local, button)) {
                return result.node;
            }
        }

        return nullptr;
    }

    Ptr dispatchMouseRelease(float screenX, float screenY, int button) {
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            Vec2 local(result.localPoint.x, result.localPoint.y);
            if (result.node->onMouseRelease(local, button)) {
                return result.node;
            }
        }

        return nullptr;
    }

    Ptr dispatchMouseMove(float screenX, float screenY) {
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            Vec2 local(result.localPoint.x, result.localPoint.y);
            if (result.node->onMouseMove(local)) {
                return result.node;
            }
        }

        return nullptr;
    }

    // -------------------------------------------------------------------------
    // Key event dispatch (broadcast to all active nodes)
    // -------------------------------------------------------------------------

    // Dispatch key press to all nodes
    bool dispatchKeyPress(int key) {
        return dispatchKeyPressRecursive(key);
    }

    // Dispatch key release to all nodes
    bool dispatchKeyRelease(int key) {
        return dispatchKeyReleaseRecursive(key);
    }

    // -------------------------------------------------------------------------
    // Update hover state (call once per frame)
    // -------------------------------------------------------------------------

    void updateHoverState(float screenX, float screenY) {
        // Save previous frame's hovered node
        internal::prevHoveredNode = internal::hoveredNode;

        // Search for new hovered node
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);
        internal::hoveredNode = result.hit() ? result.node.get() : nullptr;

        // Fire Enter/Leave events
        if (internal::prevHoveredNode != internal::hoveredNode) {
            if (internal::prevHoveredNode) {
                internal::prevHoveredNode->onMouseLeave();
            }
            if (internal::hoveredNode) {
                internal::hoveredNode->onMouseEnter();
            }
        }
    }

private:
    // Recursive dispatch of key events
    bool dispatchKeyPressRecursive(int key) {
        if (!isActive) return false;

        // Process self
        if (onKeyPress(key)) {
            return true;  // Consumed
        }

        // Dispatch to child nodes
        for (auto& child : children_) {
            if (child->dispatchKeyPressRecursive(key)) {
                return true;
            }
        }

        return false;
    }

    bool dispatchKeyReleaseRecursive(int key) {
        if (!isActive) return false;

        if (onKeyRelease(key)) {
            return true;
        }

        for (auto& child : children_) {
            if (child->dispatchKeyReleaseRecursive(key)) {
                return true;
            }
        }

        return false;
    }


    // Recursive hit test (traversed in reverse draw order)
    HitResult findHitNodeRecursive(const Ray& globalRay, const Mat4& parentInverseMatrix) {
        if (!isActive) return HitResult{};

        // Calculate inverse matrix for this node
        Mat4 localInverse = getLocalMatrix().inverted();
        Mat4 globalInverse = localInverse * parentInverseMatrix;

        // Convert global ray to local ray
        Ray localRay = globalRay.transformed(globalInverse);

        HitResult bestResult{};

        // Traverse child nodes from back (reverse draw order)
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            HitResult childResult = (*it)->findHitNodeRecursive(globalRay, globalInverse);
            if (childResult.hit()) {
                // Use child's result (later in draw order = front)
                bestResult = childResult;
                break;  // Prioritize first hit (last in draw order)
            }
        }

        // If no child hit, check self
        if (!bestResult.hit()) {
            float distance;
            if (hitTest(localRay, distance)) {
                bestResult.node = std::dynamic_pointer_cast<Node>(shared_from_this());
                bestResult.distance = distance;
                bestResult.localPoint = localRay.at(distance);
            }
        }

        return bestResult;
    }

protected:

    // -------------------------------------------------------------------------
    // Events (overridable)
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // Hit Test (Ray-based - unified 2D/3D approach)
    // -------------------------------------------------------------------------

    // Ray-based hit test (in local space)
    // localRay: Ray already transformed to this node's local coordinate system
    // outDistance: Distance from ray origin (t value) if hit
    // return: true if hit
    virtual bool hitTest(const Ray& localRay, float& outDistance) {
        (void)localRay;
        (void)outDistance;
        return false;
    }

    // 2D hit test - Return true to receive events on this node
    virtual bool hitTest(Vec2 local) {
        (void)local;
        return false;
    }

    // Mouse events (delivered in local coordinates)
    // Return true to consume the event (prevents propagation to parent)
    virtual bool onMousePress(Vec2 local, int button) {
        (void)local;
        (void)button;
        return false;
    }

    virtual bool onMouseRelease(Vec2 local, int button) {
        (void)local;
        (void)button;
        return false;
    }

    virtual bool onMouseMove(Vec2 local) {
        (void)local;
        return false;
    }

    virtual bool onMouseDrag(Vec2 local, int button) {
        (void)local;
        (void)button;
        return false;
    }

    virtual bool onMouseScroll(Vec2 local, Vec2 scroll) {
        (void)local;
        (void)scroll;
        return false;
    }

    // Key events (broadcast to all nodes)
    virtual bool onKeyPress(int key) {
        (void)key;
        return false;
    }

    virtual bool onKeyRelease(int key) {
        (void)key;
        return false;
    }

    // Mouse Enter/Leave (called when hover state changes)
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}

    // -------------------------------------------------------------------------
    // Timers
    // -------------------------------------------------------------------------

    // Execute callback once after specified delay in seconds
    uint64_t callAfter(double delay, std::function<void()> callback) {
        uint64_t id = nextTimerId_++;
        double triggerTime = getElapsedTime() + delay;
        timers_.push_back({id, triggerTime, 0.0, callback, false});
        return id;
    }

    // Execute callback repeatedly at specified interval
    uint64_t callEvery(double interval, std::function<void()> callback) {
        uint64_t id = nextTimerId_++;
        double triggerTime = getElapsedTime() + interval;
        timers_.push_back({id, triggerTime, interval, callback, true});
        return id;
    }

    // Cancel timer
    void cancelTimer(uint64_t id) {
        timers_.erase(
            std::remove_if(timers_.begin(), timers_.end(),
                [id](const Timer& t) { return t.id == id; }),
            timers_.end()
        );
    }

    // Cancel all timers
    void cancelAllTimers() {
        timers_.clear();
    }

protected:
    WeakPtr parent_;
    std::vector<Ptr> children_;
    bool eventsEnabled_ = false;  // Enabled via enableEvents()

    // Timer structure
    struct Timer {
        uint64_t id;
        double triggerTime;
        double interval;
        std::function<void()> callback;
        bool repeating;
    };

    std::vector<Timer> timers_;
    inline static uint64_t nextTimerId_ = 1;

    // Process timers (called within updateRecursive)
    void processTimers() {
        double currentTime = getElapsedTime();
        std::vector<Timer> toRemove;

        for (auto& timer : timers_) {
            if (currentTime >= timer.triggerTime) {
                timer.callback();

                if (timer.repeating) {
                    timer.triggerTime = currentTime + timer.interval;
                } else {
                    toRemove.push_back(timer);
                }
            }
        }

        // Remove completed non-repeating timers
        for (const auto& t : toRemove) {
            cancelTimer(t.id);
        }
    }
};

} // namespace trussc
