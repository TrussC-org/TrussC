#include "tcApp.h"


// ---------------------------------------------------------------------------
// setup - Initialization
// ---------------------------------------------------------------------------
void tcApp::setup() {
    tcLogNotice("tcApp") << "hitTestExample: Ray-based Hit Test Demo";
    tcLogNotice("tcApp") << "  - Click buttons to increment counter";
    tcLogNotice("tcApp") << "  - Rotating panel buttons also work!";
    tcLogNotice("tcApp") << "  - Press SPACE to pause/resume rotation";
    tcLogNotice("tcApp") << "  - Press ESC to quit";

    // Static buttons (left side) - overlapped diagonally to show only front responds
    button1_ = make_shared<CounterButton>();
    button1_->x = 50;
    button1_->y = 150;
    button1_->label = "Back";
    button1_->baseColor = Color(0.4f, 0.2f, 0.2f);
    addChild(button1_);

    button2_ = make_shared<CounterButton>();
    button2_->x = 100;  // Shifted to overlap
    button2_->y = 180;
    button2_->label = "Middle";
    button2_->baseColor = Color(0.2f, 0.4f, 0.2f);
    addChild(button2_);

    button3_ = make_shared<CounterButton>();
    button3_->x = 150;  // Shifted more to overlap
    button3_->y = 210;
    button3_->label = "Front";
    button3_->baseColor = Color(0.2f, 0.2f, 0.4f);
    addChild(button3_);

    // Rotating panel (right side)
    panel_ = make_shared<RotatingPanel>();
    panel_->x = 800;
    panel_->y = 300;
    panel_->width = 350;
    panel_->height = 250;
    addChild(panel_);

    // Buttons inside panel
    panelButton1_ = make_shared<CounterButton>();
    panelButton1_->x = 30;
    panelButton1_->y = 50;
    panelButton1_->label = "Panel Btn1";
    panelButton1_->baseColor = Color(0.5f, 0.3f, 0.1f);
    panel_->addChild(panelButton1_);

    panelButton2_ = make_shared<CounterButton>();
    panelButton2_->x = 30;
    panelButton2_->y = 120;
    panelButton2_->label = "Panel Btn2";
    panelButton2_->baseColor = Color(0.1f, 0.3f, 0.5f);
    panel_->addChild(panelButton2_);
}

// ---------------------------------------------------------------------------
// update - Update
// ---------------------------------------------------------------------------
void tcApp::update() {
    // Child nodes are updated automatically
}

// ---------------------------------------------------------------------------
// draw - Draw
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.12f);

    // Title
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Ray-based Hit Test Demo", 20, 30);

    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Static buttons (left) and rotating panel (right)", 20, 50);
    drawBitmapString("Click works on rotated buttons too!", 20, 65);

    // Mouse position
    setColor(1.0f, 1.0f, 0.5f);
    drawBitmapString(format("Mouse: {:.0f}, {:.0f}", getGlobalMouseX(), getGlobalMouseY()),
        20, getWindowHeight() - 40);

    // Controls description
    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString("[SPACE] pause/resume  [ESC] quit", 20, getWindowHeight() - 20);

    // Panel status
    setColor(0.8f, 0.8f, 0.8f);
    drawBitmapString(format("Panel rotation: {:.1f} deg  {}",
             panel_->rotation * 180.0f / PI,
             paused_ ? "(PAUSED)" : ""), 600, 50);

    // Child nodes are drawn automatically
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == KEY_SPACE) {
        paused_ = !paused_;
        panel_->rotationSpeed = paused_ ? 0.0f : 0.3f;
        tcLogNotice("tcApp") << "Rotation " << (paused_ ? "paused" : "resumed");
    }
}

// Mouse events are automatically dispatched by TrussC.h
