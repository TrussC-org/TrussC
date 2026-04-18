#pragma once

#include <memory>
#include <vector>
#include <algorithm>

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
