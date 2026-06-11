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

    // -------------------------------------------------------------------------
    // Size settings
    // -------------------------------------------------------------------------

    float getWidth() const { return width_; }
    float getHeight() const { return height_; }
    Vec2 getSize() const { return Vec2(width_, height_); }

    void setWidth(float w) {
        if (width_ != w) {
            width_ = w;
            onSizeChanged();
        }
    }

    void setHeight(float h) {
        if (height_ != h) {
            height_ = h;
            onSizeChanged();
        }
    }

    virtual void setSize(float w, float h) {
        if (width_ != w || height_ != h) {
            width_ = w;
            height_ = h;
            onSizeChanged();
        }
    }

    virtual void setSize(float size) {
        setSize(size, size);
    }

    void setSize(const Vec2& s) {
        setSize(s.x, s.y);
    }

    // Set position and size at once
    void setRect(float x, float y, float w, float h) {
        setPos(x, y);
        setSize(w, h);
    }

    // -------------------------------------------------------------------------
    // Reflection
    // -------------------------------------------------------------------------
    TC_REFLECT(RectNode, Node) {
        TC_VALUE(size, getSize, setSize)
        TC_VALUE(clipping, isClipping, setClipping)
    }

    // -------------------------------------------------------------------------
    // Clipping settings
    // -------------------------------------------------------------------------

    void setClipping(bool enabled) {
        clipping_ = enabled;
    }

    bool isClipping() const {
        return clipping_;
    }

    // -------------------------------------------------------------------------
    // Get rectangle (in local coordinates)
    // -------------------------------------------------------------------------

    // Treat as rectangle starting from origin (0,0)
    // To center at origin, offset by -width/2, -height/2 when drawing
    float getLeft() const { return 0; }
    float getRight() const { return width_; }
    float getTop() const { return 0; }
    float getBottom() const { return height_; }

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
        if (hitPoint.x >= 0 && hitPoint.x <= width_ &&
            hitPoint.y >= 0 && hitPoint.y <= height_) {
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
        return local.x >= 0 && local.x <= width_ &&
               local.y >= 0 && local.y <= height_;
    }

    // -------------------------------------------------------------------------
    // Clipping-aware hit test: when clipping is enabled, reject children
    // outside this node's rectangle (prevents scrolled-out items from
    // receiving events through overlapping siblings)
    // -------------------------------------------------------------------------

    HitResult findHitNodeRecursive(internal::PickRaySource& pick,
                                   const CameraContext* inheritedCtx,
                                   Ray globalRay,
                                   const Mat4& parentInverseMatrix) override {
        if (!isActive() || !isVisible()) return HitResult{};

        if (clipping_) {
            // Pre-check: ray must hit this rect before we check children.
            // Use this node's effective camera context, same as the base does.
            auto [ctx, ray] = resolvePickRay(pick, inheritedCtx, globalRay);
            (void)ctx;
            Mat4 localInverse = getLocalMatrix().inverted();
            Mat4 globalInverse = localInverse * parentInverseMatrix;
            Ray localRay = ray.transformed(globalInverse);

            float t;
            Vec3 hp;
            if (!localRay.intersectZPlane(t, hp) ||
                hp.x < 0 || hp.x > width_ || hp.y < 0 || hp.y > height_) {
                return HitResult{};
            }
        }

        return Node::findHitNodeRecursive(pick, inheritedCtx, globalRay, parentInverseMatrix);
    }

    // -------------------------------------------------------------------------
    // Drawing helpers (for overriding)
    // -------------------------------------------------------------------------

    // Draw rectangle (can be overridden in derived classes)
    void draw() override {
        // By default, draw nothing
        // Derived classes call drawRect(0, 0, width, height) etc.
    }

protected:
    // -------------------------------------------------------------------------
    // Clipping via beginDraw/endDraw hooks
    // -------------------------------------------------------------------------

    void beginDraw() override {
        if (clipping_) {
            // Convert local coordinates (0,0) and (width_, height_) to global
            Vec3 g1 = localToGlobal(Vec3(0, 0, 0));
            Vec3 g2 = localToGlobal(Vec3(width_, height_, 0));
            float gx1 = g1.x, gy1 = g1.y, gx2 = g2.x, gy2 = g2.y;

            // Calculate rectangle in screen coordinates (considering DPI scale)
            float dpi = sapp_dpi_scale();
            float sx = std::min(gx1, gx2) * dpi;
            float sy = std::min(gy1, gy2) * dpi;
            float sw = std::abs(gx2 - gx1) * dpi;
            float sh = std::abs(gy2 - gy1) * dpi;

            pushScissor(sx, sy, sw, sh);
        }
    }

    void endDraw() override {
        if (clipping_) {
            popScissor();
        }
    }

    // -------------------------------------------------------------------------
    // Mouse events (fire events)
    // -------------------------------------------------------------------------

    // These override the rich form and fire the corresponding Event. They also
    // forward to the simple (Vec2,int) virtual so that a subclass which overrode
    // the legacy simple form (pre-rich, oF-style) still gets called.
    bool onMousePress(const MouseEventArgs& e) override {
        MouseEventArgs args = e;  // already localized to this node
        mousePressed.notify(args);
        onMousePress(e.pos, e.button);  // legacy subclass hook
        return true;  // Consume event
    }

    bool onMouseRelease(const MouseEventArgs& e) override {
        MouseEventArgs args = e;
        mouseReleased.notify(args);
        onMouseRelease(e.pos, e.button);  // legacy subclass hook
        return true;
    }

    bool onMouseDrag(const MouseDragEventArgs& e) override {
        MouseDragEventArgs args = e;
        mouseDragged.notify(args);
        onMouseDrag(e.pos, e.button);  // legacy subclass hook
        return true;
    }

    bool onMouseScroll(const ScrollEventArgs& e) override {
        ScrollEventArgs args = e;
        mouseScrolled.notify(args);
        // Return false to allow bubbling to parent (e.g., ScrollContainer).
        // The legacy hook may return true to consume.
        return onMouseScroll(e.pos, e.scroll);
    }

    // Bring the simple-form overloads into scope so the same-named rich
    // overrides above don't hide them (name hiding across overload sets).
    using Node::onMousePress;
    using Node::onMouseRelease;
    using Node::onMouseDrag;
    using Node::onMouseScroll;

    // -------------------------------------------------------------------------
    // Drawing helpers
    // -------------------------------------------------------------------------

    // Helper to draw rectangle with fill
    void drawRectFill() {
        fill();
        drawRect(0, 0, width_, height_);
    }

    // Helper to draw rectangle with stroke
    void drawRectStroke() {
        noFill();
        drawRect(0, 0, width_, height_);
    }

    // Helper to draw rectangle with both fill and stroke
    void drawRectFillAndStroke() {
        fill();
        drawRect(0, 0, width_, height_);
        noFill();
        drawRect(0, 0, width_, height_);
    }

    // Override for custom behavior when size changes
    virtual void onSizeChanged() {}

private:
    float width_ = 100.0f;
    float height_ = 100.0f;
    bool clipping_ = false;
};

// =============================================================================
// RectNodeButton - Simple button (example of RectNode)
// =============================================================================

class RectNodeButton : public RectNode {
public:
    using Ptr = std::shared_ptr<RectNodeButton>;

    // Color settings
    Color normalColor = Color(0.3f, 0.3f, 0.3f);
    Color hoverColor = Color(0.4f, 0.4f, 0.5f);
    Color pressColor = Color(0.2f, 0.2f, 0.3f);

    // Label
    std::string label;

    RectNodeButton() {
        enableEvents();  // Enable events
    }

    // State getters
    bool isPressed() const { return isPressed_; }

    void draw() override {
        // Set color based on state
        if (isPressed_) {
            setColor(pressColor);
        } else if (isMouseOver()) {
            setColor(hoverColor);
        } else {
            setColor(normalColor);
        }

        drawRectFill();

        // Draw label at center
        if (!label.empty()) {
            setColor(1.0f, 1.0f, 1.0f);
            drawBitmapString(label, getWidth() / 2, getHeight() / 2,
                             Direction::Center, Direction::Center);
        }
    }

protected:
    bool onMousePress(const MouseEventArgs& e) override {
        isPressed_ = true;
        return RectNode::onMousePress(e);  // Also fire parent's event
    }

    bool onMouseRelease(const MouseEventArgs& e) override {
        isPressed_ = false;
        return RectNode::onMouseRelease(e);
    }

private:
    bool isPressed_ = false;
};

} // namespace trussc
