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
    friend class App;  // Allow App to call dispatch methods

public:
    using Ptr = std::shared_ptr<Node>;
    using WeakPtr = std::weak_ptr<Node>;

    Node() { internal::nodeCount++; }
    virtual ~Node() { internal::nodeCount--; }

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
            child->setPos(localX, localY, child->getZ());
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
    // Transform - Position
    // -------------------------------------------------------------------------

    const Vec3& getPos() const { return position_; }
    float getX() const { return position_.x; }
    float getY() const { return position_.y; }
    float getZ() const { return position_.z; }

    void setPos(const Vec3& pos) {
        if (position_ != pos) {
            position_ = pos;
            notifyLocalMatrixChanged();
        }
    }
    void setPos(float x, float y, float z = 0.0f) {
        setPos(Vec3(x, y, z));
    }
    void setX(float x) { setPos(x, position_.y, position_.z); }
    void setY(float y) { setPos(position_.x, y, position_.z); }
    void setZ(float z) { setPos(position_.x, position_.y, z); }

    // -------------------------------------------------------------------------
    // Transform - Rotation (Quaternion internally, various interfaces)
    // -------------------------------------------------------------------------

    const Quaternion& getQuaternion() const { return rotation_; }
    void setQuaternion(const Quaternion& q) {
        if (rotation_ != q) {
            rotation_ = q;
            notifyLocalMatrixChanged();
        }
    }

    // Euler angles (pitch=X, yaw=Y, roll=Z)
    Vec3 getEuler() const { return rotation_.toEuler(); }
    void setEuler(const Vec3& euler) { setQuaternion(Quaternion::fromEuler(euler)); }
    void setEuler(float pitch, float yaw, float roll) { setEuler(Vec3(pitch, yaw, roll)); }

    // 2D convenience: Z-axis rotation only (radians)
    float getRot() const {
        return rotation_.toEuler().z;
    }
    void setRot(float radians) {
        setQuaternion(Quaternion::fromAxisAngle(Vec3(0, 0, 1), radians));
    }
    float getRotDeg() const { return rad2deg(getRot()); }
    void setRotDeg(float degrees) { setRot(deg2rad(degrees)); }

    // -------------------------------------------------------------------------
    // Transform - Scale
    // -------------------------------------------------------------------------

    const Vec3& getScale() const { return scale_; }
    float getScaleX() const { return scale_.x; }
    float getScaleY() const { return scale_.y; }
    float getScaleZ() const { return scale_.z; }

    void setScale(const Vec3& s) {
        if (scale_ != s) {
            scale_ = s;
            notifyLocalMatrixChanged();
        }
    }
    void setScale(float uniform) { setScale(Vec3(uniform, uniform, uniform)); }
    void setScale(float sx, float sy, float sz = 1.0f) { setScale(Vec3(sx, sy, sz)); }
    void setScaleX(float sx) { setScale(sx, scale_.y, scale_.z); }
    void setScaleY(float sy) { setScale(scale_.x, sy, scale_.z); }
    void setScaleZ(float sz) { setScale(scale_.x, scale_.y, sz); }

    // -------------------------------------------------------------------------
    // Transform change notification
    // -------------------------------------------------------------------------

    Event<void> localMatrixChanged;

    // -------------------------------------------------------------------------
    // Coordinate transformation (Matrix cached)
    // -------------------------------------------------------------------------

    // Get local transform matrix for this node (cached)
    const Mat4& getLocalMatrix() const {
        if (localMatrixDirty_) {
            updateLocalMatrix();
        }
        return localMatrix_;
    }

    // Get global transform matrix for this node (includes parent transforms, cached)
    const Mat4& getGlobalMatrix() const {
        if (globalMatrixDirty_) {
            updateGlobalMatrix();
        }
        return globalMatrix_;
    }

    // Get inverse of global transform matrix
    Mat4 getGlobalMatrixInverse() const {
        return getGlobalMatrix().inverted();
    }

    // Convert global coordinates to this node's local coordinates
    void globalToLocal(float globalX, float globalY, float& localX, float& localY) const {
        // Use inverse matrix for coordinate transformation
        Mat4 inv = getGlobalMatrixInverse();
        Vec3 local = inv * Vec3(globalX, globalY, 0.0f);
        localX = local.x;
        localY = local.y;
    }

    // Convert local coordinates to global coordinates
    void localToGlobal(float localX, float localY, float& globalX, float& globalY) const {
        Vec3 global = getGlobalMatrix() * Vec3(localX, localY, 0.0f);
        globalX = global.x;
        globalY = global.y;
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

private:
    // -------------------------------------------------------------------------
    // Recursive update/draw (called by App via friend access)
    // -------------------------------------------------------------------------

    // Recursively update self and child nodes
    void updateTree() {
        if (!isActive) return;

        // Call setup() once on first update/draw
        if (!setupCalled_) {
            setupCalled_ = true;
            setup();
        }

        processTimers();
        update();  // User code

        // Automatically update child nodes
        for (auto& child : children_) {
            child->updateTree();
        }
    }

    // Recursively draw self and child nodes
    void drawTree() {
        if (!isActive) return;

        // Call setup() once on first update/draw
        if (!setupCalled_) {
            setupCalled_ = true;
            setup();
        }

        pushMatrix();

        // Apply transforms using cached matrix
        translate(position_.x, position_.y, position_.z);
        if (rotation_ != Quaternion::identity()) {
            // Apply rotation via Euler angles for now (sokol uses axis-angle or euler)
            Vec3 euler = rotation_.toEuler();
            if (euler.x != 0.0f) rotateX(euler.x);
            if (euler.y != 0.0f) rotateY(euler.y);
            if (euler.z != 0.0f) rotateZ(euler.z);
        }
        if (scale_.x != 1.0f || scale_.y != 1.0f || scale_.z != 1.0f) {
            scale(scale_.x, scale_.y, scale_.z);
        }

        // User drawing
        if (isVisible) {
            draw();
        }

        // Draw child nodes (overridable for clipping, etc.)
        drawChildren();

        popMatrix();
    }

    // -------------------------------------------------------------------------
    // Event dispatch (called by App only via friend access)
    // -------------------------------------------------------------------------

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

    Ptr dispatchMouseScroll(float screenX, float screenY, Vec2 scroll) {
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            Vec2 local(result.localPoint.x, result.localPoint.y);
            if (result.node->onMouseScroll(local, scroll)) {
                return result.node;
            }
        }

        return nullptr;
    }

    // Dispatch key press to all nodes
    bool dispatchKeyPress(int key) {
        return dispatchKeyPressRecursive(key);
    }

    // Dispatch key release to all nodes
    bool dispatchKeyRelease(int key) {
        return dispatchKeyReleaseRecursive(key);
    }

    // Update hover state (call once per frame)
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
        if (!isActive || !isVisible) return HitResult{};

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
    // Draw children (overridable for clipping, etc.)
    // -------------------------------------------------------------------------

    virtual void drawChildren() {
        for (auto& child : children_) {
            child->drawTree();
        }
    }

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

private:
    bool setupCalled_ = false;    // Ensures setup() is called only once
    WeakPtr parent_;
    std::vector<Ptr> children_;
    bool eventsEnabled_ = false;  // Enabled via enableEvents()

    // -------------------------------------------------------------------------
    // Transform data (private)
    // -------------------------------------------------------------------------
    Vec3 position_;
    Quaternion rotation_;
    Vec3 scale_ = Vec3(1.0f, 1.0f, 1.0f);

    // -------------------------------------------------------------------------
    // Matrix cache
    // -------------------------------------------------------------------------
    mutable Mat4 localMatrix_;
    mutable Mat4 globalMatrix_;
    mutable bool localMatrixDirty_ = true;
    mutable bool globalMatrixDirty_ = true;

    void updateLocalMatrix() const {
        localMatrix_ = Mat4::translate(position_) * rotation_.toMatrix() * Mat4::scale(scale_);
        localMatrixDirty_ = false;
    }

    void updateGlobalMatrix() const {
        if (auto p = parent_.lock()) {
            globalMatrix_ = p->getGlobalMatrix() * getLocalMatrix();
        } else {
            globalMatrix_ = getLocalMatrix();
        }
        globalMatrixDirty_ = false;
    }

    void markMatrixDirty() {
        localMatrixDirty_ = true;
        globalMatrixDirty_ = true;
        // Mark children's global matrix as dirty
        for (auto& child : children_) {
            child->markGlobalMatrixDirty();
        }
    }

    void markGlobalMatrixDirty() {
        globalMatrixDirty_ = true;
        for (auto& child : children_) {
            child->markGlobalMatrixDirty();
        }
    }

    void notifyLocalMatrixChanged() {
        markMatrixDirty();
        onLocalMatrixChanged();
        localMatrixChanged.notify();
    }

protected:
    // Override for custom behavior when local matrix changes
    virtual void onLocalMatrixChanged() {}

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
