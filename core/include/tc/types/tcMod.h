#pragma once

#include <memory>
#include <vector>
#include <algorithm>
#include "../events/tcEventArgs.h"
#include "../math/tcRay.h"

namespace trussc {

// Forward declaration
class Node;

// =============================================================================
// Mod - Attachable behavior for Node
//
// Lifecycle:
//   1. addMod<T>() creates Mod and calls setup()
//   2. Each frame: earlyUpdate() -> Node::update() -> update() -> draw()
//   3. When Node is destroyed or Mod removed: onDestroy()
//
// Usage:
//   node->addMod<DraggableMod>();
//   node->addMod<LayoutMod>(Layout::VStack, 10.0f);
//
// Exclusive Mods:
//   Override isExclusive() to return true to prevent multiple instances
//   of the same Mod type on a single Node.
// =============================================================================

class Mod {
    friend class Node;

public:
    virtual ~Mod() = default;

    // Get owner node
    Node* getOwner() { return owner_; }
    const Node* getOwner() const { return owner_; }

protected:
    // Remove this mod from its owner (no need to name its own type). Safe to
    // call from inside a mod's own update/draw/event handler — destruction is
    // deferred until the current dispatch finishes. Defined in tcNode.h.
    void removeSelf();

    // -------------------------------------------------------------------------
    // Lifecycle (override in derived classes)
    // -------------------------------------------------------------------------

    // Called once when Mod is attached to Node
    virtual void setup() {}

    // Called every frame BEFORE Node::update()
    // Use for: applying transforms, tweens, physics
    virtual void earlyUpdate() {}

    // Called every frame AFTER Node::update()
    // Use for: reactions to node state changes
    virtual void update() {}

    // Called during draw phase (after Node::draw())
    virtual void draw() {}

    // Called when Mod is removed or Node is destroyed
    virtual void onDestroy() {}

    // -------------------------------------------------------------------------
    // Input events (forwarded from the owner Node)
    // -------------------------------------------------------------------------
    // Same args as Node's handlers. Mouse events reach mods on the hit node;
    // key events broadcast to mods on every node. Return true from a mouse
    // handler to consume the event (counts as the node consuming it).
    virtual bool onMousePress(const MouseEventArgs& e)    { (void)e; return false; }
    virtual bool onMouseRelease(const MouseEventArgs& e)  { (void)e; return false; }
    virtual bool onMouseMove(const MouseMoveEventArgs& e) { (void)e; return false; }
    virtual bool onMouseDrag(const MouseDragEventArgs& e) { (void)e; return false; }
    virtual bool onMouseScroll(const ScrollEventArgs& e)  { (void)e; return false; }
    virtual bool onKeyPress(const KeyEventArgs& e)        { (void)e; return false; }
    virtual bool onKeyRelease(const KeyEventArgs& e)      { (void)e; return false; }
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}

    // -------------------------------------------------------------------------
    // Hit test (mouse / pointer picking)
    // -------------------------------------------------------------------------
    // Screen-space picking only — NOT physics collision (physics colliders are
    // a separate concept in the tcxPhysics addon). Override to define a hit
    // shape in the node's LOCAL space. Checked together with Node::hitTest;
    // if the node's own test OR any mod's returns true, the node is the hit.
    virtual bool hitTest(const Ray& localRay, float& outDistance) {
        (void)localRay; (void)outDistance; return false;
    }

    // -------------------------------------------------------------------------
    // Exclusivity
    // -------------------------------------------------------------------------

    // Override to return true if only one instance of this Mod type
    // should be allowed per Node (e.g., LayoutMod)
    virtual bool isExclusive() const { return false; }

    // -------------------------------------------------------------------------
    // Node attachment restriction (optional)
    // -------------------------------------------------------------------------

    // Override to restrict which Node types this Mod can attach to
    // Return false to reject attachment
    virtual bool canAttachTo(Node* node) {
        (void)node;
        return true;
    }

private:
    Node* owner_ = nullptr;
};

} // namespace trussc
