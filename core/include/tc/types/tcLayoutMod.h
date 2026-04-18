#pragma once

#include "tcMod.h"
#include "tcRectNode.h"

namespace trussc {

// =============================================================================
// Layout direction
// =============================================================================
enum class LayoutDirection {
    Vertical,    // VStack: top to bottom
    Horizontal   // HStack: left to right
};

// =============================================================================
// Axis sizing mode
// =============================================================================
enum class AxisMode {
    None,      // Don't change size (default)
    Fill,      // Stretch children to fill parent
    Content    // Resize parent to fit children
};

// =============================================================================
// LayoutMod - Automatic layout for child nodes
//
// Automatically arranges child RectNodes in vertical or horizontal stack.
// Only works with RectNode (nodes with size).
//
// Axis modes:
//   - CrossAxis: perpendicular to layout direction
//     VStack: width, HStack: height
//   - MainAxis: along layout direction
//     VStack: height, HStack: width
//
// Usage:
//   auto container = make_shared<RectNode>();
//   container->addMod<LayoutMod>(LayoutDirection::Vertical, 10.0f)
//       ->setCrossAxis(AxisMode::Fill)     // children width = parent width
//       ->setMainAxis(AxisMode::Content);  // parent height = sum of children
//
// =============================================================================

class LayoutMod : public Mod {
public:
    LayoutMod(LayoutDirection direction = LayoutDirection::Vertical, float spacing = 0.0f)
        : direction_(direction), spacing_(spacing) {}

    // -------------------------------------------------------------------------
    // Direction & Spacing
    // -------------------------------------------------------------------------

    LayoutDirection getDirection() const { return direction_; }
    LayoutMod& setDirection(LayoutDirection dir) {
        direction_ = dir;
        updateLayout();
        return *this;
    }

    float getSpacing() const { return spacing_; }
    LayoutMod& setSpacing(float spacing) {
        spacing_ = spacing;
        updateLayout();
        return *this;
    }

    // -------------------------------------------------------------------------
    // Axis modes
    // -------------------------------------------------------------------------

    AxisMode getCrossAxis() const { return crossAxis_; }
    LayoutMod& setCrossAxis(AxisMode mode) {
        crossAxis_ = mode;
        updateLayout();
        return *this;
    }

    AxisMode getMainAxis() const { return mainAxis_; }
    LayoutMod& setMainAxis(AxisMode mode) {
        mainAxis_ = mode;
        updateLayout();
        return *this;
    }

    // -------------------------------------------------------------------------
    // Padding
    // -------------------------------------------------------------------------

    float getPaddingLeft() const { return paddingLeft_; }
    float getPaddingTop() const { return paddingTop_; }
    float getPaddingRight() const { return paddingRight_; }
    float getPaddingBottom() const { return paddingBottom_; }

    LayoutMod& setPadding(float padding) {
        paddingLeft_ = paddingTop_ = paddingRight_ = paddingBottom_ = padding;
        updateLayout();
        return *this;
    }

    LayoutMod& setPadding(float vertical, float horizontal) {
        paddingTop_ = paddingBottom_ = vertical;
        paddingLeft_ = paddingRight_ = horizontal;
        updateLayout();
        return *this;
    }

    LayoutMod& setPadding(float top, float right, float bottom, float left) {
        paddingTop_ = top;
        paddingRight_ = right;
        paddingBottom_ = bottom;
        paddingLeft_ = left;
        updateLayout();
        return *this;
    }

    // -------------------------------------------------------------------------
    // Manual layout trigger
    // -------------------------------------------------------------------------

    void updateLayout() {
        Node* owner = getOwner();
        if (!owner) return;

        auto* rectOwner = dynamic_cast<RectNode*>(owner);
        if (!rectOwner) return;

        const auto& children = owner->getChildren();

        // Calculate available space
        float availableWidth = rectOwner->getWidth() - paddingLeft_ - paddingRight_;
        float availableHeight = rectOwner->getHeight() - paddingTop_ - paddingBottom_;

        // First pass: calculate total size and apply cross-axis Fill
        float totalMainSize = 0;
        float maxCrossSize = 0;
        int activeCount = 0;

        for (const auto& child : children) {
            auto* rectChild = dynamic_cast<RectNode*>(child.get());
            if (!rectChild || !child->isActive()) continue;

            activeCount++;

            if (direction_ == LayoutDirection::Vertical) {
                // VStack: cross = width, main = height
                if (crossAxis_ == AxisMode::Fill) {
                    rectChild->setWidth(availableWidth);
                }
                totalMainSize += rectChild->getHeight();
                maxCrossSize = std::max(maxCrossSize, rectChild->getWidth());
            } else {
                // HStack: cross = height, main = width
                if (crossAxis_ == AxisMode::Fill) {
                    rectChild->setHeight(availableHeight);
                }
                totalMainSize += rectChild->getWidth();
                maxCrossSize = std::max(maxCrossSize, rectChild->getHeight());
            }
        }

        // Add spacing to total
        if (activeCount > 1) {
            totalMainSize += spacing_ * (activeCount - 1);
        }

        // Apply Content mode to parent (resize parent to fit children)
        if (mainAxis_ == AxisMode::Content) {
            if (direction_ == LayoutDirection::Vertical) {
                rectOwner->setHeight(totalMainSize + paddingTop_ + paddingBottom_);
            } else {
                rectOwner->setWidth(totalMainSize + paddingLeft_ + paddingRight_);
            }
        }

        if (crossAxis_ == AxisMode::Content) {
            if (direction_ == LayoutDirection::Vertical) {
                rectOwner->setWidth(maxCrossSize + paddingLeft_ + paddingRight_);
            } else {
                rectOwner->setHeight(maxCrossSize + paddingTop_ + paddingBottom_);
            }
        }

        // Second pass: position children
        float x = paddingLeft_;
        float y = paddingTop_;

        for (const auto& child : children) {
            auto* rectChild = dynamic_cast<RectNode*>(child.get());
            if (!rectChild || !child->isActive()) continue;

            child->setPos(x, y);

            if (direction_ == LayoutDirection::Vertical) {
                y += rectChild->getHeight() + spacing_;
            } else {
                x += rectChild->getWidth() + spacing_;
            }
        }
    }

protected:
    // -------------------------------------------------------------------------
    // Mod lifecycle
    // -------------------------------------------------------------------------

    void setup() override {
        updateLayout();
    }

    void earlyUpdate() override {
        // Could add auto-relayout on change detection here
    }

    // -------------------------------------------------------------------------
    // Exclusivity - only one LayoutMod per node
    // -------------------------------------------------------------------------

    bool isExclusive() const override { return true; }

    // -------------------------------------------------------------------------
    // Restrict to RectNode
    // -------------------------------------------------------------------------

    bool canAttachTo(Node* node) override {
        return dynamic_cast<RectNode*>(node) != nullptr;
    }

private:
    LayoutDirection direction_ = LayoutDirection::Vertical;
    float spacing_ = 0.0f;
    AxisMode crossAxis_ = AxisMode::None;
    AxisMode mainAxis_ = AxisMode::None;
    float paddingLeft_ = 0.0f;
    float paddingTop_ = 0.0f;
    float paddingRight_ = 0.0f;
    float paddingBottom_ = 0.0f;
};

} // namespace trussc
