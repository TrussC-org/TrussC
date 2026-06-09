#pragma once

#include "TrussC.h"
#include "tc/types/tcMod.h"
#include "tc/utils/tcAsyncScheduler.h"
#include "tc/utils/tcTypeName.h"
#include "tc/utils/tcReflect.h"
#include <memory>
#include <string>
#include <atomic>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstdint>
#include <typeindex>
#include <unordered_map>

// =============================================================================
// trussc namespace
// =============================================================================
namespace trussc {

// Forward declaration
class Node;
class Mod;
using NodePtr = std::shared_ptr<Node>;
using NodeWeakPtr = std::weak_ptr<Node>;

// Hover state cache (updated once per frame)
namespace internal {
    inline Node* hoveredNode = nullptr;      // Currently hovered node
    inline Node* prevHoveredNode = nullptr;  // Previously hovered node
    inline Node* grabbedNode = nullptr;      // Node grabbed by mouse press
    inline int grabbedButton = -1;           // Mouse button that caused the grab
}

// =============================================================================
// Node - Scene graph base class
// All nodes inherit from this class
// =============================================================================

class Node : public std::enable_shared_from_this<Node> {
    friend class App;  // Allow App to call dispatch methods
    friend class Mod;  // Allow Mod to access owner_

public:
    using Ptr = std::shared_ptr<Node>;
    using WeakPtr = std::weak_ptr<Node>;

    Node() : instanceId_(nextInstanceId_++) { internal::nodeCount++; }
    virtual ~Node() {
        cancelAllAsyncTimers();  // stop + await any in-flight async callbacks
        for (auto& [t, m] : mods_) m->onDestroy();  // mod cleanup on node destruction
        internal::nodeCount--;
    }

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

        // Catch accidental addChild() in constructor where weak_from_this() is empty
        assert(!weak_from_this().expired() &&
            "addChild() called before shared_ptr is ready — move to setup()");

        // If preserving global position, record position before move
        Vec3 gpos;
        if (keepGlobalPosition) {
            gpos = child->localToGlobal(Vec3(0, 0, 0));
        }

        // Remove from existing parent
        if (auto oldParent = child->parent_.lock()) {
            oldParent->removeChild(child);
        }

        child->parent_ = weak_from_this();
        children_.push_back(child);

        // If preserving global position, recalculate local coordinates relative to new parent
        if (keepGlobalPosition) {
            Vec3 lpos = globalToLocal(gpos);
            child->setPos(lpos.x, lpos.y, child->getZ());
        }

        // Notify callback
        onChildAdded(child);
    }

    // Insert child node at specific index
    void insertChild(size_t index, Ptr child, bool keepGlobalPosition = false) {
        if (!child || child.get() == this) return;

        // Catch accidental insertChild() in constructor where weak_from_this() is empty
        assert(!weak_from_this().expired() &&
            "insertChild() called before shared_ptr is ready — move to setup()");

        // If preserving global position, record position before move
        Vec3 gpos;
        if (keepGlobalPosition) {
            gpos = child->localToGlobal(Vec3(0, 0, 0));
        }

        // Remove from existing parent
        if (auto oldParent = child->parent_.lock()) {
            oldParent->removeChild(child);
        }

        child->parent_ = weak_from_this();

        // Clamp index and insert
        if (index >= children_.size()) {
            children_.push_back(child);
        } else {
            children_.insert(children_.begin() + index, child);
        }

        // If preserving global position, recalculate local coordinates
        if (keepGlobalPosition) {
            Vec3 lpos = globalToLocal(gpos);
            child->setPos(lpos.x, lpos.y, child->getZ());
        }

        onChildAdded(child);
    }

    // Remove child node
    void removeChild(Ptr child) {
        if (!child) return;

        auto it = std::find(children_.begin(), children_.end(), child);
        if (it != children_.end()) {
            // Mutate first, then notify. onChildRemoved overrides may call
            // addChild / removeChild on this node; firing the callback after
            // the erase keeps children_ consistent and avoids invalidating
            // the local iterator before we use it.
            children_.erase(it);
            child->parent_.reset();
            onChildRemoved(child);
        }
    }

    // Remove all child nodes
    void removeAllChildren() {
        // Mutate first (single vector move), then notify. onChildRemoved
        // overrides may call addChild on this node; firing the callbacks
        // after the move lets them see an empty children_.
        auto cleared = std::move(children_);
        children_.clear();   // moved-from vector is "valid but unspecified"
        for (auto& child : cleared) {
            child->parent_.reset();
            onChildRemoved(child);
        }
    }

    // Callback when child is added (overridable)
    virtual void onChildAdded(Ptr child) { (void)child; }

    // Callback when child is removed (overridable)
    virtual void onChildRemoved(Ptr child) { (void)child; }

    // Get parent node
    Ptr getParent() const {
        return parent_.lock();
    }

    // Get list of child nodes (returns a copy — safe to iterate while modifying the tree)
    std::vector<Ptr> getChildren() const {
        return children_;
    }

    // Get number of child nodes
    size_t getChildCount() const {
        return children_.size();
    }

    // Index of the given child among this node's children (-1 if not a child)
    int indexOfChild(const Node* child) const {
        for (size_t i = 0; i < children_.size(); ++i) {
            if (children_[i].get() == child) return static_cast<int>(i);
        }
        return -1;
    }

    // This node's index among its parent's children (-1 if it has no parent).
    // A sibling-order counterpart to getParent().
    int getChildIndex() const {
        auto p = getParent();
        return p ? p->indexOfChild(this) : -1;
    }

    // Reorder this node within its parent's child list.
    //
    // Children are drawn in vector order: the first child is drawn first
    // (visually behind), the last child is drawn last (visually in front).
    // moveToFront() puts this node at the end so it draws on top of its
    // siblings; moveToBack() puts it at the beginning so it draws underneath.
    //
    // Both use std::rotate, so they don't change the vector's size and don't
    // trigger reallocation. No-op if the node has no parent or is already at
    // the requested position.
    void moveToFront() {
        auto p = getParent();
        if (!p) return;
        auto self = shared_from_this();
        auto& sib = p->children_;
        auto it = std::find(sib.begin(), sib.end(), self);
        if (it == sib.end() || it + 1 == sib.end()) return;
        std::rotate(it, it + 1, sib.end());
    }

    void moveToBack() {
        auto p = getParent();
        if (!p) return;
        auto self = shared_from_this();
        auto& sib = p->children_;
        auto it = std::find(sib.begin(), sib.end(), self);
        if (it == sib.end() || it == sib.begin()) return;
        std::rotate(sib.begin(), it, it + 1);
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    // Active state (false: update/draw are skipped)
    bool isActive() const { return isActive_; }
    void setActive(bool active) {
        if (isActive_ != active) {
            isActive_ = active;
            onActiveChanged(active);
        }
    }

    // Visible state (false: only draw is skipped)
    bool isVisible() const { return isVisible_; }
    void setVisible(bool visible) {
        if (isVisible_ != visible) {
            isVisible_ = visible;
            onVisibleChanged(visible);
        }
    }

    // Deprecated aliases — will be removed in v1.0.0
    [[deprecated("Use isActive() instead. Will be removed in v1.0.0")]]
    bool getActive() const { return isActive_; }
    [[deprecated("Use isVisible() instead. Will be removed in v1.0.0")]]
    bool getVisible() const { return isVisible_; }
    [[deprecated("Use setActive() instead. Will be removed in v1.0.0")]]
    void setIsActive(bool active) { setActive(active); }
    [[deprecated("Use setVisible() instead. Will be removed in v1.0.0")]]
    void setIsVisible(bool visible) { setVisible(visible); }

    // Destroy node (marks as dead, removed from tree on next update cycle)
    // Safe to call during update() — actual removal is deferred
    void destroy() {
        if (dead_) return;
        dead_ = true;
    }
    bool isDead() const { return dead_; }

    // Event enabling (only nodes that called enableEvents() are hit test targets)
    void enableEvents() { eventsEnabled_ = true; }
    void disableEvents() { eventsEnabled_ = false; }
    bool isEventsEnabled() const { return eventsEnabled_; }

    // Whether mouse is over this node (auto-updated each frame, O(1))
    bool isMouseOver() const { return internal::hoveredNode == this; }

    // -------------------------------------------------------------------------
    // Identity / Name
    // -------------------------------------------------------------------------
    //
    // Distinct notions:
    //   - getName()       : optional instance name, set by the user (may be empty)
    //   - getTypeName()   : the C++ class name from RTTI (always available, free)
    //   - getDisplayName(): short type name, with the instance name in parens
    //                       if set ("RectNode" or "RectNode (play)") — uses the
    //                       unqualified class name (no "trussc::") for readable
    //                       trees; getTypeName() keeps the full qualified name.
    //   - getInstanceId() : per-process unique id, fixed at construction.

    // Optional instance name. Empty unless setName() was called.
    Node& setName(const std::string& name) { name_ = name; return *this; }
    const std::string& getName() const { return name_; }
    bool hasName() const { return !name_.empty(); }

    // C++ class name (dynamic / most-derived type) via RTTI. Cached per type,
    // so this is cheap to call even for many nodes. E.g. "trussc::RectNode".
    const std::string& getTypeName() const { return typeName(typeid(*this)); }

    // Type-anchored label for trees / inspectors / logs. Always non-empty.
    // Uses the short (unqualified) type name: "RectNode" when unnamed,
    // "RectNode (play)" when named. For the full "trussc::RectNode", use
    // getTypeName().
    std::string getDisplayName() const {
        const std::string& type = shortTypeName(typeid(*this));
        return hasName() ? type + " (" + name_ + ")" : type;
    }

    // Per-process unique id, assigned once at construction and never changed
    // (stable across reparenting). Read-only — there is no setter.
    uint64_t getInstanceId() const { return instanceId_; }

    // -------------------------------------------------------------------------
    // Reflection (TC_REFLECT)
    // -------------------------------------------------------------------------
    // Exposes a curated set of editable properties (not the raw private fields).
    // Transform values go through their setters so the matrix cache / change
    // events stay correct. Subclasses extend via `using Super = Node;` + their
    // own TC_REFLECT block.
    TC_REFLECT_ROOT(Node)
        TC_PROPERTY(pos,      getPos,    setPos)
        TC_PROPERTY(rotation, getRotDeg, setRotDeg)
        TC_PROPERTY(scale,    getScale,  setScale)
        TC_PROPERTY(visible,  isVisible, setVisible)
        TC_PROPERTY(active,   isActive,  setActive)
    TC_REFLECT_END

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
    Vec3 globalToLocal(const Vec3& global) const {
        return getGlobalMatrixInverse() * global;
    }

    [[deprecated("Use globalToLocal(Vec3) instead. Will be removed in v1.0.0")]]
    void globalToLocal(float globalX, float globalY, float& localX, float& localY) const {
        Vec3 local = globalToLocal(Vec3(globalX, globalY, 0.0f));
        localX = local.x;
        localY = local.y;
    }

    // Get global position (origin of this node in global space)
    Vec3 getGlobalPos() const {
        return localToGlobal(Vec3(0, 0, 0));
    }

    // Convert local coordinates to global coordinates
    Vec3 localToGlobal(const Vec3& local) const {
        return getGlobalMatrix() * local;
    }

    [[deprecated("Use localToGlobal(Vec3) instead. Will be removed in v1.0.0")]]
    void localToGlobal(float localX, float localY, float& globalX, float& globalY) const {
        Vec3 global = localToGlobal(Vec3(localX, localY, 0.0f));
        globalX = global.x;
        globalY = global.y;
    }

    // -------------------------------------------------------------------------
    // Mouse coordinates (in local coordinate system)
    // -------------------------------------------------------------------------

    // Mouse X in this node's local coordinate system
    float getMouseX() const {
        return globalToLocal(Vec3(trussc::getGlobalMouseX(), trussc::getGlobalMouseY(), 0)).x;
    }

    // Mouse Y in this node's local coordinate system
    float getMouseY() const {
        return globalToLocal(Vec3(trussc::getGlobalMouseX(), trussc::getGlobalMouseY(), 0)).y;
    }

    // Previous frame mouse X (in local coordinate system)
    float getPMouseX() const {
        return globalToLocal(Vec3(trussc::getGlobalPMouseX(), trussc::getGlobalPMouseY(), 0)).x;
    }

    // Previous frame mouse Y (in local coordinate system)
    float getPMouseY() const {
        return globalToLocal(Vec3(trussc::getGlobalPMouseX(), trussc::getGlobalPMouseY(), 0)).y;
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

    // -------------------------------------------------------------------------
    // Mod system - attach behaviors to nodes
    // -------------------------------------------------------------------------

    // Get a mod by type (returns nullptr if not found)
    template<typename T>
    T* getMod() {
        auto it = mods_.find(std::type_index(typeid(T)));
        if (it != mods_.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    // Whether a mod of type T is attached
    template<typename T>
    bool hasMod() const {
        return mods_.count(std::type_index(typeid(T))) > 0;
    }

    // Add a mod to this node (returns pointer for chaining)
    template<typename T, typename... Args>
    T* addMod(Args&&... args) {
        auto mod = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = mod.get();
        mod->owner_ = this;
        mods_[std::type_index(typeid(T))] = std::move(mod);
        // setup() runs after owner_ is set and the mod is in mods_, so it can
        // safely call getMod / addChild / etc. on its owner.
        ptr->setup();
        return ptr;
    }

    // Remove a mod by type. onDestroy() is called, then the mod is freed. If a
    // mod removes itself (or another) during a dispatch/update, destruction is
    // deferred until iteration finishes so the in-flight call can't dangle.
    // (A mod can remove itself without naming its type via Mod::removeSelf().)
    template<typename T>
    void removeMod() {
        removeModByType(std::type_index(typeid(T)));
    }

private:
    // -------------------------------------------------------------------------
    // Mod dispatch helpers
    // -------------------------------------------------------------------------

    // Remove a mod by runtime type (shared by removeMod<T>() and
    // Mod::removeSelf()). Deferred-safe during iteration; calls onDestroy().
    void removeModByType(std::type_index key) {
        auto it = mods_.find(key);
        if (it == mods_.end()) return;
        if (modDispatchDepth_ > 0) {
            modsPendingDestroy_.push_back(std::move(it->second));
            mods_.erase(it);  // gone from lookups now; destroyed after iteration
        } else {
            it->second->onDestroy();
            mods_.erase(it);
        }
    }

    // Visit each attached mod. Iterates a snapshot of type indices (so a
    // handler may add mods — picked up next round — or remove other mods).
    // A mod that removes itself stays alive until this returns: removeMod()
    // defers destruction while modDispatchDepth_ > 0, and we sweep the
    // pending list (calling onDestroy) once the outermost visit finishes.
    template<typename F>
    void forEachMod(F&& f) {
        ++modDispatchDepth_;
        std::vector<std::type_index> types;
        types.reserve(mods_.size());
        for (auto& [t, m] : mods_) types.push_back(t);
        for (auto& t : types) {
            auto it = mods_.find(t);
            if (it != mods_.end()) f(it->second.get());
        }
        if (--modDispatchDepth_ == 0 && !modsPendingDestroy_.empty()) {
            auto pending = std::move(modsPendingDestroy_);
            modsPendingDestroy_.clear();
            for (auto& m : pending) m->onDestroy();
            // pending mods are freed here, after all in-flight calls returned
        }
    }

    // Fire an input event to this node's own handler AND its mods; consumed if
    // either consumes. Mouse events go to the hit/grabbed node, keys broadcast.
    bool fireMousePress(const MouseEventArgs& e) {
        bool consumed = onMousePress(e);
        forEachMod([&](Mod* m) { if (m->onMousePress(e)) consumed = true; });
        return consumed;
    }
    bool fireMouseRelease(const MouseEventArgs& e) {
        bool consumed = onMouseRelease(e);
        forEachMod([&](Mod* m) { if (m->onMouseRelease(e)) consumed = true; });
        return consumed;
    }
    bool fireMouseMove(const MouseMoveEventArgs& e) {
        bool consumed = onMouseMove(e);
        forEachMod([&](Mod* m) { if (m->onMouseMove(e)) consumed = true; });
        return consumed;
    }
    bool fireMouseDrag(const MouseDragEventArgs& e) {
        bool consumed = onMouseDrag(e);
        forEachMod([&](Mod* m) { if (m->onMouseDrag(e)) consumed = true; });
        return consumed;
    }
    bool fireMouseScroll(const ScrollEventArgs& e) {
        bool consumed = onMouseScroll(e);
        forEachMod([&](Mod* m) { if (m->onMouseScroll(e)) consumed = true; });
        return consumed;
    }
    bool fireKeyPress(const KeyEventArgs& e) {
        bool consumed = onKeyPress(e);
        forEachMod([&](Mod* m) { if (m->onKeyPress(e)) consumed = true; });
        return consumed;
    }
    bool fireKeyRelease(const KeyEventArgs& e) {
        bool consumed = onKeyRelease(e);
        forEachMod([&](Mod* m) { if (m->onKeyRelease(e)) consumed = true; });
        return consumed;
    }
    void fireMouseEnter() { onMouseEnter(); forEachMod([](Mod* m) { m->onMouseEnter(); }); }
    void fireMouseLeave() { onMouseLeave(); forEachMod([](Mod* m) { m->onMouseLeave(); }); }

    // -------------------------------------------------------------------------
    // Recursive update/draw (called by App via friend access)
    // -------------------------------------------------------------------------

    // Recursively update self and child nodes
    void updateTree() {
        if (!isActive_) return;

        // Remove dead children before processing
        sweepDeadChildren();

        // Call setup() once on first update/draw
        if (!setupCalled_) {
            setupCalled_ = true;
            setup();
        }

        // Mod early update (before Node::update). forEachMod snapshots types
        // and defers self-removal, so a mod may add/remove mods safely here.
        forEachMod([](Mod* m) { m->earlyUpdate(); });

        processTimers();
        update();  // User code

        // Mod update (after Node::update).
        forEachMod([](Mod* m) { m->update(); });

        // Iterate over a snapshot — a child's update() may add, remove, or
        // reorder siblings (via addChild / removeChild / moveToFront / etc.),
        // and we want those edits to take effect on the *next* frame rather
        // than corrupt the in-flight iteration. The snapshot is a shallow
        // shared_ptr copy so it's cheap and keeps the targets alive.
        auto childrenSnapshot = children_;
        for (auto& child : childrenSnapshot) {
            if (!child->isDead()) child->updateTree();
        }
    }

    // Remove dead children and call cleanup on their subtrees.
    //
    // Two-phase: collect dead children, mutate children_, THEN fire user
    // callbacks (cleanupTree / onChildRemoved). User code inside those
    // callbacks may call addChild / removeChild on this node — running them
    // after the erase guarantees a consistent children_ during dispatch.
    void sweepDeadChildren() {
        std::vector<Ptr> dead;
        for (auto& c : children_) {
            if (c->isDead()) dead.push_back(c);
        }
        if (dead.empty()) return;

        children_.erase(
            std::remove_if(children_.begin(), children_.end(),
                [](const Ptr& c) { return c->isDead(); }),
            children_.end());

        for (auto& c : dead) {
            c->cleanupTree();
            onChildRemoved(c);
            c->parent_.reset();
        }
    }

    // Recursively call cleanup() on this node and all descendants
    void cleanupTree() {
        // Cleanup children first (depth-first, like destructors). Snapshot
        // because a child's cleanup() may reach back and mutate this node's
        // children_ (uncommon but legal — e.g. a child that unregisters
        // siblings from its parent on destruction).
        auto snapshot = children_;
        for (auto& child : snapshot) {
            child->cleanupTree();
        }

        // Clear global references to this node (prevent dangling pointers)
        if (internal::hoveredNode == this) internal::hoveredNode = nullptr;
        if (internal::prevHoveredNode == this) internal::prevHoveredNode = nullptr;
        if (internal::grabbedNode == this) {
            internal::grabbedNode = nullptr;
            internal::grabbedButton = -1;
        }

        dead_ = true;
        cleanup();
    }

    // Recursively draw self and child nodes
    void drawTree() {
        if (!isActive_) return;

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

        // Begin draw hook (for clipping, etc.)
        beginDraw();

        // User drawing, then mod draw (mods draw in the node's local space,
        // after the node's own draw()).
        if (isVisible_) {
            resetStyle();
            draw();
            forEachMod([](Mod* m) { m->draw(); });
        }

        // Draw child nodes (overridable for clipping, etc.)
        drawChildren();

        // End draw hook
        resetStyle();
        endDraw();

        popMatrix();
    }

    // -------------------------------------------------------------------------
    // Event dispatch (called by App only via friend access)
    // -------------------------------------------------------------------------

    // Press/release: lean args carry no movement, so only pos is localized.
    MouseEventArgs localizeMouse(const MouseEventArgs& s) {
        MouseEventArgs a = s;
        Vec3 lp = globalToLocal(Vec3(s.globalPos.x, s.globalPos.y, 0));
        a.pos = Vec2(lp.x, lp.y);
        a.syncLegacy();
        return a;
    }

    // Move/drag carrier: localize pos AND the movement delta into this node's
    // local space. globalPos / globalDelta / button / modifiers are preserved.
    MouseEventRaw localizeMouse(const MouseEventRaw& s) {
        MouseEventRaw a = s;
        Vec3 lp = globalToLocal(Vec3(s.globalPos.x, s.globalPos.y, 0));
        Vec3 lpPrev = globalToLocal(Vec3(s.globalPos.x - s.globalDelta.x,
                                         s.globalPos.y - s.globalDelta.y, 0));
        a.pos = Vec2(lp.x, lp.y);
        a.delta = Vec2(lp.x - lpPrev.x, lp.y - lpPrev.y);
        return a;
    }

    ScrollEventArgs localizeScroll(const ScrollEventArgs& s) {
        ScrollEventArgs a = s;
        Vec3 lp = globalToLocal(Vec3(s.globalPos.x, s.globalPos.y, 0));
        a.pos = Vec2(lp.x, lp.y);
        a.syncLegacy();
        return a;
    }

    // Dispatch mouse event to tree (for 2D mode). `e` is in screen space
    // (pos == globalPos); each node receives a copy localized to its space.
    // return: node that handled event (nullptr if not handled)
    Ptr dispatchMousePress(const MouseEventArgs& e) {
        Ray globalRay = Ray::fromScreenPoint2D(e.globalPos.x, e.globalPos.y);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            MouseEventArgs local = result.node->localizeMouse(e);
            if (result.node->fireMousePress(local)) {
                // Set grabbed node for drag tracking
                internal::grabbedNode = result.node.get();
                internal::grabbedButton = e.button;
                return result.node;
            }
        }

        return nullptr;
    }

    Ptr dispatchMouseRelease(const MouseEventArgs& e) {
        // Send release to grabbed node if it exists
        if (internal::grabbedNode && internal::grabbedButton == e.button) {
            MouseEventArgs local = internal::grabbedNode->localizeMouse(e);
            internal::grabbedNode->fireMouseRelease(local);

            Ptr result = std::dynamic_pointer_cast<Node>(
                internal::grabbedNode->shared_from_this());

            // Clear grabbed state
            internal::grabbedNode = nullptr;
            internal::grabbedButton = -1;

            return result;
        }

        // Fallback: send to hit node
        Ray globalRay = Ray::fromScreenPoint2D(e.globalPos.x, e.globalPos.y);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            MouseEventArgs local = result.node->localizeMouse(e);
            if (result.node->fireMouseRelease(local)) {
                return result.node;
            }
        }

        return nullptr;
    }

    Ptr dispatchMouseMove(const MouseEventRaw& e) {
        // Send drag event to grabbed node
        if (internal::grabbedNode) {
            MouseEventRaw local = internal::grabbedNode->localizeMouse(e);
            local.button = internal::grabbedButton;
            internal::grabbedNode->fireMouseDrag(toDragArgs(local));
        }

        // Also send move event to hit node (for hover, etc.)
        Ray globalRay = Ray::fromScreenPoint2D(e.globalPos.x, e.globalPos.y);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            MouseEventRaw local = result.node->localizeMouse(e);
            if (result.node->fireMouseMove(toMoveArgs(local))) {
                return result.node;
            }
        }

        return nullptr;
    }

    Ptr dispatchMouseScroll(const ScrollEventArgs& e) {
        Ray globalRay = Ray::fromScreenPoint2D(e.globalPos.x, e.globalPos.y);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            // Bubble up from hit node to ancestors until consumed
            Node* current = result.node.get();
            while (current) {
                ScrollEventArgs local = current->localizeScroll(e);
                if (current->fireMouseScroll(local)) {
                    // Event consumed
                    return std::dynamic_pointer_cast<Node>(
                        current->shared_from_this());
                }

                // Bubble up to parent
                current = current->getParent().get();
            }
        }

        return nullptr;
    }

    // Dispatch key press to all nodes
    bool dispatchKeyPress(const KeyEventArgs& e) {
        return dispatchKeyPressRecursive(e);
    }

    // Dispatch key release to all nodes
    bool dispatchKeyRelease(const KeyEventArgs& e) {
        return dispatchKeyReleaseRecursive(e);
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
                internal::prevHoveredNode->fireMouseLeave();
            }
            if (internal::hoveredNode) {
                internal::hoveredNode->fireMouseEnter();
            }
        }
    }

    // Recursive dispatch of key events
    bool dispatchKeyPressRecursive(const KeyEventArgs& e) {
        if (!isActive_) return false;

        // Process self
        if (fireKeyPress(e)) {
            return true;  // Consumed
        }

        // Snapshot — handlers may mutate the tree.
        auto childrenSnapshot = children_;
        for (auto& child : childrenSnapshot) {
            if (child->isDead()) continue;
            if (child->dispatchKeyPressRecursive(e)) {
                return true;
            }
        }

        return false;
    }

    bool dispatchKeyReleaseRecursive(const KeyEventArgs& e) {
        if (!isActive_) return false;

        if (fireKeyRelease(e)) {
            return true;
        }

        auto childrenSnapshot = children_;
        for (auto& child : childrenSnapshot) {
            if (child->isDead()) continue;
            if (child->dispatchKeyReleaseRecursive(e)) {
                return true;
            }
        }

        return false;
    }

protected:
    // Recursive hit test (traversed in reverse draw order)
    // Protected so that RectNode can override for clipping-aware hit test
    virtual HitResult findHitNodeRecursive(const Ray& globalRay, const Mat4& parentInverseMatrix) {
        if (!isActive_ || !isVisible_) return HitResult{};

        // Calculate inverse matrix for this node
        Mat4 localInverse = getLocalMatrix().inverted();
        Mat4 globalInverse = localInverse * parentInverseMatrix;

        // Convert global ray to local ray
        Ray localRay = globalRay.transformed(globalInverse);

        HitResult bestResult{};

        // Traverse child nodes from back (reverse draw order). Snapshot in
        // case a hitTest() override mutates the tree — hitTest is contractually
        // a pure geometric predicate, but the snapshot is cheap insurance.
        auto snapshot = children_;
        for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
            HitResult childResult = (*it)->findHitNodeRecursive(globalRay, globalInverse);
            if (childResult.hit()) {
                // Use child's result (later in draw order = front)
                bestResult = childResult;
                break;  // Prioritize first hit (last in draw order)
            }
        }

        // If no child hit, check self: the node's own hitTest OR any mod's
        // hitTest (mouse picking). First true wins (mods short-circuit once hit).
        if (!bestResult.hit()) {
            float distance;
            bool hit = hitTest(localRay, distance);
            if (!hit) {
                forEachMod([&](Mod* m) { if (!hit && m->hitTest(localRay, distance)) hit = true; });
            }
            if (hit) {
                bestResult.node = std::dynamic_pointer_cast<Node>(shared_from_this());
                bestResult.distance = distance;
                bestResult.localPoint = localRay.at(distance);
            }
        }

        return bestResult;
    }

protected:

    // -------------------------------------------------------------------------
    // Draw hooks (for clipping, etc.)
    // -------------------------------------------------------------------------

    // Called before draw() and drawChildren()
    virtual void beginDraw() {}

    // Called after draw() and drawChildren()
    virtual void endDraw() {}

    // -------------------------------------------------------------------------
    // Draw children (overridable)
    // -------------------------------------------------------------------------

    virtual void drawChildren() {
        // Snapshot — see updateTree() for rationale.
        auto childrenSnapshot = children_;
        for (auto& child : childrenSnapshot) {
            if (!child->isDead()) child->drawTree();
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

    // Mouse events. `e` is localized to this node (e.pos in local space,
    // e.globalPos in screen space). Return true to consume (stops propagation).
    //
    // Each has a rich form (canonical, carries globalPos / delta / modifiers)
    // and a simple form (convenience, oF-style local pos + int button). The
    // default rich impl forwards to the simple one — override either.
    virtual bool onMousePress(const MouseEventArgs& e) { return onMousePress(e.pos, e.button); }
    virtual bool onMousePress(Vec2 local, int button) { (void)local; (void)button; return false; }

    virtual bool onMouseRelease(const MouseEventArgs& e) { return onMouseRelease(e.pos, e.button); }
    virtual bool onMouseRelease(Vec2 local, int button) { (void)local; (void)button; return false; }

    virtual bool onMouseMove(const MouseMoveEventArgs& e) { return onMouseMove(e.pos); }
    virtual bool onMouseMove(Vec2 local) { (void)local; return false; }

    virtual bool onMouseDrag(const MouseDragEventArgs& e) { return onMouseDrag(e.pos, e.button); }
    virtual bool onMouseDrag(Vec2 local, int button) { (void)local; (void)button; return false; }

    virtual bool onMouseScroll(const ScrollEventArgs& e) { return onMouseScroll(e.pos, e.scroll); }
    virtual bool onMouseScroll(Vec2 local, Vec2 scroll) { (void)local; (void)scroll; return false; }

    // Key events (broadcast to all nodes). The rich form (canonical) carries
    // modifiers + isRepeat; the simple int form is a convenience the default
    // rich impl forwards to — override either.
    virtual bool onKeyPress(const KeyEventArgs& e) { return onKeyPress(e.key); }
    virtual bool onKeyPress(int key) { (void)key; return false; }

    virtual bool onKeyRelease(const KeyEventArgs& e) { return onKeyRelease(e.key); }
    virtual bool onKeyRelease(int key) { (void)key; return false; }

    // Mouse Enter/Leave (called when hover state changes)
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}

    // State change callbacks
    virtual void onActiveChanged(bool active) { (void)active; }
    virtual void onVisibleChanged(bool visible) { (void)visible; }

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

    // -------------------------------------------------------------------------
    // Async timers (off-thread, precise)
    // -------------------------------------------------------------------------
    // Like callAfter / callEvery, but fired by a background scheduler thread at
    // precise times instead of the frame-quantized update loop - use these when
    // timing jitter matters (sequencer clocks, LED/MIDI output, ...).
    //
    // The callback runs ON THE SCHEDULER THREAD: guard state shared with
    // update()/draw() behind a mutex, never draw from it (AudioEngine::play is
    // fine). Cancel them before the members the callback touches are destroyed
    // (e.g. in cleanup() / on mode change); ~Node cancels any leftovers and
    // waits for an in-flight callback to finish.
    uint64_t callAfterAsync(double delay, std::function<void()> callback) {
        return AsyncScheduler::get().after(asyncOwner(), delay, std::move(callback));
    }

    uint64_t callEveryAsync(double interval, std::function<void()> callback) {
        return AsyncScheduler::get().every(asyncOwner(), interval, std::move(callback));
    }

    void cancelAsyncTimer(uint64_t id) {
        AsyncScheduler::get().cancel(id);
    }

    void cancelAllAsyncTimers() {
        if (asyncOwner_) AsyncScheduler::get().cancelOwner(asyncOwner_);
    }

private:
    uint64_t asyncOwner_ = 0;   // lazily assigned scheduler owner token
    uint64_t asyncOwner() {
        if (!asyncOwner_) asyncOwner_ = AsyncScheduler::newOwner();
        return asyncOwner_;
    }

    bool setupCalled_ = false;    // Ensures setup() is called only once
    bool dead_ = false;           // Marked for removal by destroy()
    std::string name_;            // Optional instance name (see getName())
    const uint64_t instanceId_;   // Per-process unique id, fixed at construction
    inline static std::atomic<uint64_t> nextInstanceId_{0};  // id source
    WeakPtr parent_;
    std::vector<Ptr> children_;
    bool eventsEnabled_ = false;  // Enabled via enableEvents()
    bool isActive_ = true;        // false: update/draw are skipped
    bool isVisible_ = true;       // false: only draw is skipped

    // Mod system
    std::unordered_map<std::type_index, std::unique_ptr<Mod>> mods_;
    int modDispatchDepth_ = 0;   // >0 while iterating mods (forEachMod)
    std::vector<std::unique_ptr<Mod>> modsPendingDestroy_;  // removed mid-iteration

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
    //
    // Reentrancy-safe: a callback may invoke callAfter / callEvery / cancelTimer
    // / cancelAllTimers on this same node. We snapshot the ready-timer IDs up
    // front, then look each one up by ID before firing, copying out the
    // callback and metadata so vector reallocation during the callback can't
    // dangle the in-flight reference.
    void processTimers() {
        double currentTime = getElapsedTime();

        std::vector<uint64_t> readyIds;
        readyIds.reserve(timers_.size());
        for (const auto& t : timers_) {
            if (currentTime >= t.triggerTime) {
                readyIds.push_back(t.id);
            }
        }

        for (uint64_t id : readyIds) {
            auto it = std::find_if(timers_.begin(), timers_.end(),
                [id](const Timer& t) { return t.id == id; });
            if (it == timers_.end()) continue;  // cancelled by an earlier callback in this batch

            std::function<void()> callback = it->callback;
            bool   repeating = it->repeating;
            double interval  = it->interval;

            if (repeating) {
                it->triggerTime = currentTime + interval;
                callback();  // safe: we already captured what we need from `it`
            } else {
                // Remove before firing so the timer is gone even if the
                // callback throws or registers a new timer that reallocates.
                cancelTimer(id);
                callback();
            }
        }
    }
};

// Mod::removeSelf — defined here now that Node is complete. Uses the mod's
// dynamic type so it removes the right entry without the mod naming its type.
inline void Mod::removeSelf() {
    if (owner_) owner_->removeModByType(std::type_index(typeid(*this)));
}

} // namespace trussc
