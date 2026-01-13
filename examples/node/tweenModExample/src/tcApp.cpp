// =============================================================================
// tweenModExample - TweenMod Animation Demo Implementation
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // -------------------------------------------------------------------------
    // Position animation demo
    // -------------------------------------------------------------------------
    moveBox_ = make_shared<AnimBox>(60);
    moveBox_->setPos(baseX_, 80);
    moveBox_->label = "Move";
    moveBox_->boxColor = Color(0.5f, 0.4f, 0.7f);
    addChild(moveBox_);

    // -------------------------------------------------------------------------
    // Scale animation demo
    // -------------------------------------------------------------------------
    scaleBox_ = make_shared<AnimBox>(60);
    scaleBox_->setPos(baseX_, 160);
    scaleBox_->label = "Scale";
    scaleBox_->boxColor = Color(0.4f, 0.6f, 0.5f);
    addChild(scaleBox_);

    // -------------------------------------------------------------------------
    // Rotation animation demo
    // -------------------------------------------------------------------------
    rotateBox_ = make_shared<AnimBox>(60);
    rotateBox_->setPos(baseX_, 240);
    rotateBox_->label = "Rotate";
    rotateBox_->boxColor = Color(0.7f, 0.5f, 0.4f);
    addChild(rotateBox_);

    // -------------------------------------------------------------------------
    // Combined animation demo
    // -------------------------------------------------------------------------
    comboBox_ = make_shared<AnimBox>(60);
    comboBox_->setPos(baseX_, 320);
    comboBox_->label = "Combo";
    comboBox_->boxColor = Color(0.6f, 0.6f, 0.4f);
    addChild(comboBox_);

    // -------------------------------------------------------------------------
    // Easing comparison demo
    // Available EaseTypes: Linear, Quad, Cubic, Quart, Quint, Sine, Expo, Circ, Back, Elastic, Bounce
    // Available EaseModes: In, Out, InOut
    // -------------------------------------------------------------------------
    struct EaseInfo {
        string name;
        EaseType type;
    };
    vector<EaseInfo> easeTypes = {
        {"Cubic", EaseType::Cubic},
        {"Elastic", EaseType::Elastic},
        {"Bounce", EaseType::Bounce}
    };

    float startY = 420;
    for (size_t i = 0; i < easeTypes.size(); i++) {
        auto box = make_shared<AnimBox>(30);
        box->setPos(100, startY + i * 25);
        box->boxColor = Color::fromHSB(i * 0.12f, 0.5f, 0.6f);
        addChild(box);

        easeDemos_.push_back({box, easeTypes[i].name, easeTypes[i].type});
    }

    logNotice("tcApp") << "=== tweenModExample ===";
    logNotice("tcApp") << "TweenMod animation demo";
    logNotice("tcApp") << "";
    logNotice("tcApp") << "Press SPACE to start animations";
    logNotice("tcApp") << "Press R to reset positions";
}

void tcApp::update() {
    // Check if main animations completed
    if (animating_) {
        bool allDone = true;
        if (moveBox_->tween && moveBox_->tween->isPlaying()) allDone = false;
        if (scaleBox_->tween && scaleBox_->tween->isPlaying()) allDone = false;
        if (rotateBox_->tween && rotateBox_->tween->isPlaying()) allDone = false;
        if (comboBox_->tween && comboBox_->tween->isPlaying()) allDone = false;

        for (auto& demo : easeDemos_) {
            if (demo.box->tween && demo.box->tween->isPlaying()) allDone = false;
        }

        if (allDone) {
            animating_ = false;
        }
    }
}

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);

    // Instructions
    setColor(0.7f, 0.7f, 0.75f);
    drawBitmapString("Press SPACE to animate, R to reset", 50, 30);

    // Status
    setColor(0.5f, 0.5f, 0.55f);
    string status = animating_ ? "Animating..." : "Ready";
    drawBitmapString(status, 50, 50);

    // Main demo labels
    setColor(0.6f, 0.6f, 0.65f);
    drawBitmapString("moveTo()", 500, 100);
    drawBitmapString("scaleTo()", 500, 180);
    drawBitmapString("rotateTo()", 500, 260);
    drawBitmapString("Combined", 500, 340);

    // Easing demo section
    setColor(0.7f, 0.7f, 0.75f);
    drawBitmapString("Easing Comparison (EaseMode::Out)", 100, 400);

    // Easing labels
    setColor(0.5f, 0.5f, 0.55f);
    for (size_t i = 0; i < easeDemos_.size(); i++) {
        float y = 420 + i * 25;
        drawBitmapString(easeDemos_[i].name, 10, y + 20);
    }

    // Target line
    setColor(0.3f, 0.3f, 0.35f);
    drawLine(targetX_, 60, targetX_, 380);
    drawLine(500, 410, 500, 510);

    // Frame rate
    setColor(0.4f, 0.4f, 0.45f);
    drawBitmapString(format("FPS: {:.1f}", getFrameRate()), 10, 580);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        startAnimations();
    } else if (key == 'r' || key == 'R') {
        resetPositions();
    }
}

void tcApp::startAnimations() {
    if (animating_) return;
    animating_ = true;

    // Reset to starting positions first
    resetPositions();

    float dur = 1.0f;

    // Position animation - move to target X
    moveBox_->tween->moveTo(targetX_, 80)
        .duration(dur)
        .ease(EaseType::Cubic, EaseMode::InOut)
        .start();

    // Scale animation - scale up with overshoot
    scaleBox_->tween->scaleTo(2.0f)
        .duration(dur)
        .ease(EaseType::Back, EaseMode::Out)
        .start();

    // Rotation animation - spin one full rotation
    rotateBox_->tween->rotateBy(TAU)
        .duration(dur)
        .ease(EaseType::Cubic, EaseMode::InOut)
        .start();

    // Combined animation - move + scale + rotate
    comboBox_->tween->moveTo(targetX_, 320)
        .scaleTo(1.5f)
        .rotateBy(TAU * 0.5f)
        .duration(dur)
        .ease(EaseType::Elastic, EaseMode::Out)
        .start();

    // Easing comparison - all boxes move right with different easings
    for (auto& demo : easeDemos_) {
        demo.box->tween->moveTo(500, demo.box->getY())
            .duration(dur)
            .ease(demo.type, EaseMode::Out)
            .start();
    }
}

void tcApp::resetPositions() {
    moveBox_->setPos(baseX_, 80);
    moveBox_->setScale(1.0f);
    moveBox_->setRot(0);

    scaleBox_->setPos(baseX_, 160);
    scaleBox_->setScale(1.0f);

    rotateBox_->setPos(baseX_, 240);
    rotateBox_->setRot(0);

    comboBox_->setPos(baseX_, 320);
    comboBox_->setScale(1.0f);
    comboBox_->setRot(0);

    float startY = 420;
    for (size_t i = 0; i < easeDemos_.size(); i++) {
        easeDemos_[i].box->setPos(100, startY + i * 25);
    }
}
