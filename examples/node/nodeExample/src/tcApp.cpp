#include "tcApp.h"

using namespace std;

// ---------------------------------------------------------------------------
// setup - Initialization
// ---------------------------------------------------------------------------
void tcApp::setup() {
    logNotice("tcApp") << "02_nodes: Node System Demo";
    logNotice("tcApp") << "  - Space: Pause/resume rotation";
    logNotice("tcApp") << "  - ESC: Exit";

    // Container 1 (left side, clockwise)
    container1_ = make_shared<RotatingContainer>();
    container1_->setPos(240, 320);
    container1_->rotationSpeed = 0.5f;
    container1_->size = 200;

    // Container 2 (right side, counter-clockwise, slightly smaller)
    container2_ = make_shared<RotatingContainer>();
    container2_->setPos(720, 320);
    container2_->rotationSpeed = -0.3f;
    container2_->size = 160;
    container2_->setScale(0.8f, 0.8f);

    // Mouse follower nodes (one for each container)
    follower1_ = make_shared<MouseFollower>();
    follower1_->r = 1.0f;
    follower1_->g = 0.3f;
    follower1_->b = 0.5f;
    container1_->addChild(follower1_);

    follower2_ = make_shared<MouseFollower>();
    follower2_->r = 0.3f;
    follower2_->g = 1.0f;
    follower2_->b = 0.5f;
    container2_->addChild(follower2_);

    // Fixed position child nodes (placed at corners)
    float offset = 80;
    vector<pair<float, float>> positions = {
        {-offset, -offset}, {offset, -offset},
        {-offset, offset}, {offset, offset}
    };

    for (int i = 0; i < 4; i++) {
        auto child = make_shared<FixedChild>();
        child->setPos(positions[i].first, positions[i].second);
        child->hue = i * QUARTER_TAU;
        container1_->addChild(child);
    }

    for (int i = 0; i < 4; i++) {
        auto child = make_shared<FixedChild>();
        child->setPos(positions[i].first, positions[i].second);
        child->hue = i * QUARTER_TAU + HALF_TAU;
        child->size = 20;
        container2_->addChild(child);
    }

    // Add to root (App)
    addChild(container1_);
    addChild(container2_);
}

// ---------------------------------------------------------------------------
// update - Update
// ---------------------------------------------------------------------------
void tcApp::update() {
    // App's own update processing goes here if needed
    // Child nodes are updated automatically by the framework
}

// ---------------------------------------------------------------------------
// draw - Draw
// ---------------------------------------------------------------------------
void tcApp::draw() {
    // Clear background
    clear(0.1f, 0.1f, 0.15f);

    // Display mouse position in global coordinates
    float gx = getGlobalMouseX();
    float gy = getGlobalMouseY();
    setColor(1.0f, 1.0f, 1.0f, 0.5f);
    drawCircle(gx, gy, 5);

    // Display description at top-left of screen
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Node System Demo - Local Coordinate Transformation", 20, 25);
    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Each box has its own local coordinate system.", 20, 45);
    drawBitmapString("Mouse position is transformed to local coords.", 20, 60);

    // Global mouse coordinates
    setColor(1.0f, 1.0f, 0.5f);
    drawBitmapString(format("global: {:.0f}, {:.0f}", gx, gy), 20, 90);

    // Controls description
    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString("[SPACE] pause/resume rotation  [ESC] quit", 20, getWindowHeight() - 20);

    // Child nodes are drawn automatically by the framework (after this draw())
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == KEY_SPACE) {
        // Pause/resume rotation
        static bool paused = false;
        paused = !paused;

        container1_->rotationSpeed = paused ? 0.0f : 0.5f;
        container2_->rotationSpeed = paused ? 0.0f : -0.3f;

        logNotice("tcApp") << "Rotation " << (paused ? "paused" : "resumed");
    }
}

void tcApp::mousePressed(Vec2 pos, int button) {
    logVerbose("tcApp") << "Global mouse: " << pos.x << ", " << pos.y;

    // Display each follower's local coordinates
    logVerbose("tcApp") << "  Follower1 local: " << follower1_->getMouseX()
                       << ", " << follower1_->getMouseY();
    logVerbose("tcApp") << "  Follower2 local: " << follower2_->getMouseX()
                       << ", " << follower2_->getMouseY();
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    (void)pos; (void)button;
}
