// =============================================================================
// layoutModExample - LayoutMod Demo Implementation
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // -------------------------------------------------------------------------
    // VStack container (Vertical layout)
    // -------------------------------------------------------------------------
    vContainer_ = make_shared<LayoutContainer>(200, 350);
    vContainer_->setPos(50, 80);
    vContainer_->title = "VStack (Vertical)";
    vContainer_->layout = vContainer_->addMod<LayoutMod>(LayoutDirection::Vertical, 10.0f);
    vContainer_->layout->setPadding(15);
    addChild(vContainer_);

    // Add initial items
    for (int i = 0; i < 3; i++) {
        addBoxToVStack();
    }

    // -------------------------------------------------------------------------
    // HStack container (Horizontal layout)
    // -------------------------------------------------------------------------
    hContainer_ = make_shared<LayoutContainer>(450, 80);
    hContainer_->setPos(280, 80);
    hContainer_->title = "HStack (Horizontal)";
    hContainer_->layout = hContainer_->addMod<LayoutMod>(LayoutDirection::Horizontal, 10.0f);
    hContainer_->layout->setPadding(10);
    addChild(hContainer_);

    // Add initial items
    for (int i = 0; i < 4; i++) {
        addBoxToHStack();
    }

    // -------------------------------------------------------------------------
    // Grid-like container (nested VStack + HStack)
    // -------------------------------------------------------------------------
    gridContainer_ = make_shared<LayoutContainer>(450, 220);
    gridContainer_->setPos(280, 210);
    gridContainer_->title = "Nested Layout (VStack of HStacks)";
    gridContainer_->layout = gridContainer_->addMod<LayoutMod>(LayoutDirection::Vertical, 10.0f);
    gridContainer_->layout->setPadding(15);
    addChild(gridContainer_);

    // Create rows
    Color rowColors[] = {
        Color(0.4f, 0.25f, 0.25f),
        Color(0.25f, 0.4f, 0.25f),
        Color(0.25f, 0.25f, 0.4f)
    };

    for (int row = 0; row < 3; row++) {
        auto rowContainer = make_shared<LayoutContainer>(420, 50);
        rowContainer->bgColor = Color(0.12f, 0.12f, 0.15f);
        rowContainer->addMod<LayoutMod>(LayoutDirection::Horizontal, 8.0f)->setPadding(5);

        for (int col = 0; col < 5; col++) {
            auto box = make_shared<ColorBox>(75, 40, rowColors[row]);
            box->label = format("R{}C{}", row + 1, col + 1);
            rowContainer->addChild(box);
        }

        // Update layout after adding children
        if (auto* layout = rowContainer->getMod<LayoutMod>()) {
            layout->updateLayout();
        }

        gridContainer_->addChild(rowContainer);
    }

    // Update grid container layout
    gridContainer_->layout->updateLayout();

    logNotice("tcApp") << "=== layoutModExample ===";
    logNotice("tcApp") << "LayoutMod demo - automatic layout of child nodes";
    logNotice("tcApp") << "";
    logNotice("tcApp") << "Keys:";
    logNotice("tcApp") << "  V - Add box to VStack";
    logNotice("tcApp") << "  H - Add box to HStack";
    logNotice("tcApp") << "  D - Remove last from VStack";
    logNotice("tcApp") << "  F - Remove last from HStack";
}

void tcApp::update() {
    // Child nodes updated automatically
}

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);

    // Instructions
    setColor(0.6f, 0.6f, 0.65f);
    drawBitmapString("Press V/H to add boxes, D/F to remove", 50, 30);
    drawBitmapString(format("VStack items: {}  HStack items: {}",
        vContainer_->getChildCount(), hContainer_->getChildCount()), 50, 50);

    // Info for nested layout
    setColor(0.5f, 0.5f, 0.55f);
    drawBitmapString("Nested layout: VStack containing HStack rows", 280, 450);

    // Frame rate
    setColor(0.4f, 0.4f, 0.45f);
    drawBitmapString(format("FPS: {:.1f}", getFrameRate()), getWidth() - 100, 30);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'V':
            addBoxToVStack();
            break;
        case 'H':
            addBoxToHStack();
            break;
        case 'D':
            removeLastFromVStack();
            break;
        case 'F':
            removeLastFromHStack();
            break;
    }
}

void tcApp::addBoxToVStack() {
    boxCounter_++;
    auto box = make_shared<ColorBox>(170, 40, Color(0.3f, 0.35f, 0.45f));
    box->label = format("Box {}", boxCounter_);
    vContainer_->addChild(box);
    vContainer_->layout->updateLayout();
}

void tcApp::addBoxToHStack() {
    boxCounter_++;
    auto box = make_shared<ColorBox>(80, 60, Color(0.45f, 0.35f, 0.3f));
    box->label = format("B{}", boxCounter_);
    hContainer_->addChild(box);
    hContainer_->layout->updateLayout();
}

void tcApp::removeLastFromVStack() {
    const auto& children = vContainer_->getChildren();
    if (!children.empty()) {
        vContainer_->removeChild(children.back());
        vContainer_->layout->updateLayout();
    }
}

void tcApp::removeLastFromHStack() {
    const auto& children = hContainer_->getChildren();
    if (!children.empty()) {
        hContainer_->removeChild(children.back());
        hContainer_->layout->updateLayout();
    }
}
