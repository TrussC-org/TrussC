#pragma once

#include "tcNode.h"

namespace trussc {

// =============================================================================
// RectNode - 2D UI node with rectangle
// Supports accurate hit testing via ray-based approach, even with rotation/scale in 3D space
// =============================================================================

class RectNode : public Node {
public:
    using Ptr = std::shared_ptr<RectNode>;
    using WeakPtr = std::weak_ptr<RectNode>;

    // -------------------------------------------------------------------------
    // Events (listeners can be registered externally)
    // -------------------------------------------------------------------------
    Event<MouseEventArgs> mousePressed;
    Event<MouseEventArgs> mouseReleased;
    Event<MouseDragEventArgs> mouseDragged;
    Event<ScrollEventArgs> mouseScrolled;

    // Size (width and height in local coordinates)
    float width = 100.0f;
    float height = 100.0f;

    // -------------------------------------------------------------------------
    // Clipping settings
    // -------------------------------------------------------------------------

    // Enable/disable clipping
    void setClipping(bool enabled) {
        clipping_ = enabled;
    }

    bool isClipping() const {
        return clipping_;
    }

    // -------------------------------------------------------------------------
    // Size settings
    // -------------------------------------------------------------------------

    void setSize(float w, float h) {
        width = w;
        height = h;
    }

    void setSize(float size) {
        width = height = size;
    }

    Vec2 getSize() const {
        return Vec2(width, height);
    }

    // -------------------------------------------------------------------------
    // Get rectangle (in local coordinates)
    // -------------------------------------------------------------------------

    // Treat as rectangle starting from origin (0,0)
    // To center at origin, offset by -width/2, -height/2 when drawing
    float getLeft() const { return 0; }
    float getRight() const { return width; }
    float getTop() const { return 0; }
    float getBottom() const { return height; }

    // -------------------------------------------------------------------------
    // Ray-based Hit Test
    // -------------------------------------------------------------------------

    // Hit test using ray (intersection with Z=0 plane)
    // In local space, this node exists as a width x height rectangle on Z=0 plane
    bool hitTest(const Ray& localRay, float& outDistance) override {
        // No hit if events are not enabled
        if (!isEventsEnabled()) {
            return false;
        }

        // Calculate intersection with Z=0 plane
        float t;
        Vec3 hitPoint;
        if (!localRay.intersectZPlane(t, hitPoint)) {
            return false;
        }

        // Check if intersection point is within rectangle
        if (hitPoint.x >= 0 && hitPoint.x <= width &&
            hitPoint.y >= 0 && hitPoint.y <= height) {
            outDistance = t;
            return true;
        }

        return false;
    }

    // 2D hit test
    bool hitTest(Vec2 local) override {
        if (!isEventsEnabled()) {
            return false;
        }
        return local.x >= 0 && local.x <= width &&
               local.y >= 0 && local.y <= height;
    }

    // -------------------------------------------------------------------------
    // Drawing helpers (for overriding)
    // -------------------------------------------------------------------------

    // Draw rectangle (can be overridden in derived classes)
    void draw() override {
        // By default, draw nothing
        // Derived classes call drawRect(0, 0, width, height) etc.
    }

    // Draw tree with clipping support
    void drawTree() override {
        if (!isActive) return;

        pushMatrix();

        // Apply transformations
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

        // If clipping enabled, set scissor (push to stack)
        if (clipping_) {
            // Convert local coordinates (0,0) and (width, height) to global
            float gx1, gy1, gx2, gy2;
            localToGlobal(0, 0, gx1, gy1);
            localToGlobal(width, height, gx2, gy2);

            // Calculate rectangle in screen coordinates (considering DPI scale)
            float dpi = sapp_dpi_scale();
            float sx = std::min(gx1, gx2) * dpi;
            float sy = std::min(gy1, gy2) * dpi;
            float sw = std::abs(gx2 - gx1) * dpi;
            float sh = std::abs(gy2 - gy1) * dpi;

            pushScissor(sx, sy, sw, sh);
        }

        // Draw child nodes
        for (auto& child : children_) {
            child->drawTree();
        }

        // Restore clipping (pop from stack)
        if (clipping_) {
            popScissor();
        }

        popMatrix();
    }

protected:
    bool clipping_ = false;
    // -------------------------------------------------------------------------
    // Mouse events (fire events)
    // -------------------------------------------------------------------------

    bool onMousePress(Vec2 local, int button) override {
        MouseEventArgs args;
        args.x = local.x;
        args.y = local.y;
        args.button = button;
        mousePressed.notify(args);
        return true;  // Consume event
    }

    bool onMouseRelease(Vec2 local, int button) override {
        MouseEventArgs args;
        args.x = local.x;
        args.y = local.y;
        args.button = button;
        mouseReleased.notify(args);
        return true;
    }

    bool onMouseDrag(Vec2 local, int button) override {
        MouseDragEventArgs args;
        args.x = local.x;
        args.y = local.y;
        args.button = button;
        args.deltaX = local.x - getMouseX();  // Simple delta
        args.deltaY = local.y - getMouseY();
        mouseDragged.notify(args);
        return true;
    }

    bool onMouseScroll(Vec2 local, Vec2 scroll) override {
        (void)local;
        ScrollEventArgs args;
        args.scrollX = scroll.x;
        args.scrollY = scroll.y;
        mouseScrolled.notify(args);
        return true;
    }

    // -------------------------------------------------------------------------
    // Drawing helpers
    // -------------------------------------------------------------------------

    // Helper to draw rectangle with fill
    void drawRectFill() {
        fill();
        noStroke();
        drawRect(0, 0, width, height);
    }

    // Helper to draw rectangle with stroke
    void drawRectStroke() {
        noFill();
        stroke();
        drawRect(0, 0, width, height);
    }

    // Helper to draw rectangle with both fill and stroke
    void drawRectFillStroke() {
        fill();
        stroke();
        drawRect(0, 0, width, height);
    }
};

// =============================================================================
// Button - Simple button (example of RectNode)
// =============================================================================

class Button : public RectNode {
public:
    using Ptr = std::shared_ptr<Button>;

    // State
    bool isHovered = false;
    bool isPressed = false;

    // Color settings
    Color normalColor = Color(0.3f, 0.3f, 0.3f);
    Color hoverColor = Color(0.4f, 0.4f, 0.5f);
    Color pressColor = Color(0.2f, 0.2f, 0.3f);

    // Label
    std::string label;

    Button() {
        enableEvents();  // Enable events
    }

    void draw() override {
        // Set color based on state
        if (isPressed) {
            setColor(pressColor);
        } else if (isHovered) {
            setColor(hoverColor);
        } else {
            setColor(normalColor);
        }

        drawRectFill();

        // Draw label at center
        if (!label.empty()) {
            setColor(1.0f, 1.0f, 1.0f);
            // Simple centering (simplified version without font size consideration)
            float textX = width / 2 - label.length() * 4;
            float textY = height / 2 + 4;
            drawBitmapString(label, textX, textY);
        }
    }

protected:
    bool onMousePress(Vec2 local, int button) override {
        isPressed = true;
        return RectNode::onMousePress(local, button);  // Also fire parent's event
    }

    bool onMouseRelease(Vec2 local, int button) override {
        isPressed = false;
        return RectNode::onMouseRelease(local, button);
    }
};

} // namespace trussc
