// =============================================================================
// scrollContainerExample - ScrollContainer + LayoutMod + ScrollBar Demo
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // =========================================================================
    // Vertical scroll demo (left side)
    // =========================================================================
    vScrollContainer_ = make_shared<ScrollContainer>();
    vScrollContainer_->setPos(50, 80);
    vScrollContainer_->setSize(350, 450);
    addChild(vScrollContainer_);

    // Content with VStack layout
    vContent_ = make_shared<RectNode>();
    vContent_->setSize(350, 0);

    vLayout_ = vContent_->addMod<LayoutMod>(LayoutDirection::Vertical, 8.0f);
    vLayout_->setCrossAxis(AxisMode::Fill);
    vLayout_->setMainAxis(AxisMode::Content);
    vLayout_->setPadding(10);

    vScrollContainer_->setContent(vContent_);

    // Vertical scroll bar (position auto-calculated from barWidth)
    vScrollBar_ = make_shared<ScrollBar>(vScrollContainer_.get(), ScrollBar::Vertical);
    vScrollContainer_->addChild(vScrollBar_);

    // Add initial items
    for (int i = 0; i < 15; i++) {
        addItem();
    }

    // =========================================================================
    // Horizontal scroll demo (right side)
    // =========================================================================
    hScrollContainer_ = make_shared<ScrollContainer>();
    hScrollContainer_->setPos(450, 80);
    hScrollContainer_->setSize(450, 120);
    hScrollContainer_->setHorizontalScrollEnabled(true);
    hScrollContainer_->setVerticalScrollEnabled(false);
    addChild(hScrollContainer_);

    // Content with HStack layout
    hContent_ = make_shared<RectNode>();
    hContent_->setSize(0, 120);

    hLayout_ = hContent_->addMod<LayoutMod>(LayoutDirection::Horizontal, 8.0f);
    hLayout_->setCrossAxis(AxisMode::Fill);
    hLayout_->setMainAxis(AxisMode::Content);
    hLayout_->setPadding(10);

    hScrollContainer_->setContent(hContent_);

    // Horizontal scroll bar (position auto-calculated from barWidth)
    hScrollBar_ = make_shared<ScrollBar>(hScrollContainer_.get(), ScrollBar::Horizontal);
    hScrollContainer_->addChild(hScrollBar_);

    // Add initial items
    for (int i = 0; i < 8; i++) {
        addHItem();
    }

    // =========================================================================
    // Both directions scroll demo (bottom right)
    // =========================================================================
    bothScrollContainer_ = make_shared<ScrollContainer>();
    bothScrollContainer_->setPos(450, 300);
    bothScrollContainer_->setSize(300, 230);
    bothScrollContainer_->setHorizontalScrollEnabled(true);
    bothScrollContainer_->setVerticalScrollEnabled(true);
    addChild(bothScrollContainer_);

    // Fixed size crosshatch content (600x600 - larger than container)
    crossHatch_ = make_shared<CrossHatch>(600, 600);
    bothScrollContainer_->setContent(crossHatch_);

    // Vertical scroll bar for both-direction container
    bothVScrollBar_ = make_shared<ScrollBar>(bothScrollContainer_.get(), ScrollBar::Vertical);
    bothScrollContainer_->addChild(bothVScrollBar_);

    // Horizontal scroll bar for both-direction container
    bothHScrollBar_ = make_shared<ScrollBar>(bothScrollContainer_.get(), ScrollBar::Horizontal);
    bothScrollContainer_->addChild(bothHScrollBar_);

    // =========================================================================
    // Log
    // =========================================================================
    logNotice("tcApp") << "=== scrollContainerExample ===";
    logNotice("tcApp") << "Keys:";
    logNotice("tcApp") << "  A/D - Add/Remove vertical items";
    logNotice("tcApp") << "  H/J - Add/Remove horizontal items";
}

void tcApp::update() {
    vScrollContainer_->updateScrollBounds();
    hScrollContainer_->updateScrollBounds();
    bothScrollContainer_->updateScrollBounds();
    vScrollBar_->updateFromContainer();
    hScrollBar_->updateFromContainer();
    bothVScrollBar_->updateFromContainer();
    bothHScrollBar_->updateFromContainer();
}

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);

    // Title
    setColor(0.8f, 0.8f, 0.85f);
    drawBitmapString("ScrollContainer + ScrollBar Demo", 50, 30);

    // Vertical section label
    setColor(0.6f, 0.6f, 0.65f);
    drawBitmapString("Vertical Scroll (A/D to add/remove)", 50, 55);

    // Horizontal section label
    drawBitmapString("Horizontal Scroll (H/J to add/remove)", 450, 55);

    // Info
    setColor(0.5f, 0.5f, 0.55f);
    drawBitmapString(format("V items: {}  H items: {}",
        vContent_->getChildCount(), hContent_->getChildCount()), 450, 220);

    auto* vContentRect = vScrollContainer_->getContentRect();
    if (vContentRect) {
        drawBitmapString(format("V content: {:.0f}x{:.0f}",
            vContentRect->getWidth(), vContentRect->getHeight()), 450, 240);
    }

    auto* hContentRect = hScrollContainer_->getContentRect();
    if (hContentRect) {
        drawBitmapString(format("H content: {:.0f}x{:.0f}",
            hContentRect->getWidth(), hContentRect->getHeight()), 450, 260);
    }

    // Both directions section label
    setColor(0.6f, 0.6f, 0.65f);
    drawBitmapString("Both Directions (600x600 content)", 450, 285);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'a':
        case 'A':
            addItem();
            break;
        case 'd':
        case 'D':
            removeItem();
            break;
        case 'h':
        case 'H':
            addHItem();
            break;
        case 'j':
        case 'J':
            removeHItem();
            break;
    }
}

void tcApp::addItem() {
    auto item = make_shared<ListItem>(itemCount_, 320, 50);
    vContent_->addChild(item);
    vLayout_->updateLayout();
    itemCount_++;
}

void tcApp::removeItem() {
    const auto& children = vContent_->getChildren();
    if (!children.empty()) {
        vContent_->removeChild(children.back());
        vLayout_->updateLayout();
    }
}

void tcApp::addHItem() {
    static int hItemCount = 0;
    auto item = make_shared<ListItem>(hItemCount, 100, 100);
    item->label = format("H{}", hItemCount + 1);
    hContent_->addChild(item);
    hLayout_->updateLayout();
    hItemCount++;
}

void tcApp::removeHItem() {
    const auto& children = hContent_->getChildren();
    if (!children.empty()) {
        hContent_->removeChild(children.back());
        hLayout_->updateLayout();
    }
}
