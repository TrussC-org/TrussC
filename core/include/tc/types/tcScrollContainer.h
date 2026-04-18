#pragma once

#include "tcRectNode.h"

namespace trussc {

// =============================================================================
// ScrollContainer - Scrollable container for a single content node
//
// The first child (content) is scrolled when it exceeds container bounds.
// Additional children can be added as overlays (not scrolled).
//
// Usage:
//   auto scroll = make_shared<ScrollContainer>();
//   scroll->setSize(300, 400);
//
//   auto content = make_shared<RectNode>();
//   content->addMod<LayoutMod>(LayoutDirection::Vertical, 10)
//       ->setMainAxis(AxisMode::Content);  // content grows with children
//
//   scroll->setContent(content);
//   // Add items to content...
//
// =============================================================================

class ScrollContainer : public RectNode {
public:
    using Ptr = std::shared_ptr<ScrollContainer>;

    ScrollContainer() {
        enableEvents();
        setClipping(true);  // Clip content to container bounds
    }

    // -------------------------------------------------------------------------
    // Content management
    // -------------------------------------------------------------------------

    // Set the scrollable content (replaces existing content)
    void setContent(Node::Ptr newContent) {
        // Remove existing content if any
        if (content_) {
            removeChild(content_);
        }
        content_ = newContent;
        if (content_) {
            // Insert at front so it's always the first child
            insertChild(0, content_);
        }
        updateScrollBounds();
    }

    // Get the scrollable content
    Node::Ptr getContent() const {
        return content_;
    }

    // Get content as RectNode
    RectNode* getContentRect() const {
        return content_ ? dynamic_cast<RectNode*>(content_.get()) : nullptr;
    }

    // -------------------------------------------------------------------------
    // Scroll position
    // -------------------------------------------------------------------------

    float getScrollX() const { return scrollX_; }
    float getScrollY() const { return scrollY_; }
    Vec2 getScroll() const { return Vec2(scrollX_, scrollY_); }

    void setScrollX(float x) {
        scrollX_ = clampScrollX(x);
        applyScroll();
    }

    void setScrollY(float y) {
        scrollY_ = clampScrollY(y);
        applyScroll();
    }

    void setScroll(float x, float y) {
        scrollX_ = clampScrollX(x);
        scrollY_ = clampScrollY(y);
        applyScroll();
    }

    void setScroll(Vec2 pos) {
        setScroll(pos.x, pos.y);
    }

    // -------------------------------------------------------------------------
    // Scroll bounds
    // -------------------------------------------------------------------------

    float getMaxScrollX() const { return maxScrollX_; }
    float getMaxScrollY() const { return maxScrollY_; }

    // Recalculate scroll bounds based on content size
    void updateScrollBounds() {
        auto* content = getContentRect();
        if (!content) {
            maxScrollX_ = 0;
            maxScrollY_ = 0;
            return;
        }

        maxScrollX_ = std::max(0.0f, content->getWidth() - getWidth());
        maxScrollY_ = std::max(0.0f, content->getHeight() - getHeight());

        // Clamp current scroll position
        scrollX_ = clampScrollX(scrollX_);
        scrollY_ = clampScrollY(scrollY_);
        applyScroll();
    }

    // -------------------------------------------------------------------------
    // Scroll settings
    // -------------------------------------------------------------------------

    bool isHorizontalScrollEnabled() const { return horizontalScroll_; }
    bool isVerticalScrollEnabled() const { return verticalScroll_; }

    void setHorizontalScrollEnabled(bool enabled) { horizontalScroll_ = enabled; }
    void setVerticalScrollEnabled(bool enabled) { verticalScroll_ = enabled; }

    float getScrollSpeed() const { return scrollSpeed_; }
    void setScrollSpeed(float speed) { scrollSpeed_ = speed; }

protected:
    // -------------------------------------------------------------------------
    // Size change callback
    // -------------------------------------------------------------------------

    void onSizeChanged() override {
        updateScrollBounds();
    }

    // -------------------------------------------------------------------------
    // Update - apply accumulated scroll
    // -------------------------------------------------------------------------

    void update() override {
        if (accumulatedScrollX_ != 0 || accumulatedScrollY_ != 0) {
            if (verticalScroll_ && maxScrollY_ > 0) {
                setScrollY(scrollY_ - accumulatedScrollY_ * scrollSpeed_);
            }
            if (horizontalScroll_ && maxScrollX_ > 0) {
                setScrollX(scrollX_ - accumulatedScrollX_ * scrollSpeed_);
            }
            accumulatedScrollX_ = 0;
            accumulatedScrollY_ = 0;
        }
    }

    // -------------------------------------------------------------------------
    // Mouse scroll event - accumulate for processing in update
    // -------------------------------------------------------------------------

    bool onMouseScroll(Vec2 local, Vec2 scroll) override {
        (void)local;

        bool canScroll = false;

        if (verticalScroll_ && maxScrollY_ > 0) {
            accumulatedScrollY_ += scroll.y;
            canScroll = true;
        }

        if (horizontalScroll_ && maxScrollX_ > 0) {
            accumulatedScrollX_ += scroll.x;
            canScroll = true;
        }

        // Consume event if we can scroll, otherwise bubble up
        return canScroll;
    }

private:
    Node::Ptr content_;
    float scrollX_ = 0;
    float scrollY_ = 0;
    float maxScrollX_ = 0;
    float maxScrollY_ = 0;
    bool horizontalScroll_ = false;  // Disabled by default
    bool verticalScroll_ = true;     // Enabled by default
    float scrollSpeed_ = 20.0f;

    // Accumulated scroll delta (processed in update)
    float accumulatedScrollX_ = 0;
    float accumulatedScrollY_ = 0;

    float clampScrollX(float x) const {
        return std::max(0.0f, std::min(maxScrollX_, x));
    }

    float clampScrollY(float y) const {
        return std::max(0.0f, std::min(maxScrollY_, y));
    }

    void applyScroll() {
        auto* content = getContentRect();
        if (content) {
            content->setPos(-scrollX_, -scrollY_);
        }
    }
};

} // namespace trussc
