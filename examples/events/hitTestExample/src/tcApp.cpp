#include "tcApp.h"

using namespace std;

// ---------------------------------------------------------------------------
// setup - Initialization
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "hitTestExample: Ray-based Hit Test Demo" << endl;
    cout << "  - Click buttons to increment counter" << endl;
    cout << "  - Rotating panel buttons also work!" << endl;
    cout << "  - Press SPACE to pause/resume rotation" << endl;
    cout << "  - Press ESC to quit" << endl;

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
    char buf[128];
    snprintf(buf, sizeof(buf), "Mouse: %.0f, %.0f", getGlobalMouseX(), getGlobalMouseY());
    setColor(1.0f, 1.0f, 0.5f);
    drawBitmapString(buf, 20, getWindowHeight() - 40);

    // Controls description
    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString("[SPACE] pause/resume  [ESC] quit", 20, getWindowHeight() - 20);

    // Panel status
    snprintf(buf, sizeof(buf), "Panel rotation: %.1f deg  %s",
             panel_->rotation * 180.0f / PI,
             paused_ ? "(PAUSED)" : "");
    setColor(0.8f, 0.8f, 0.8f);
    drawBitmapString(buf, 600, 50);

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
        cout << "Rotation " << (paused_ ? "paused" : "resumed") << endl;
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    // Dispatch event using Ray-based Hit Test
    auto hitNode = dispatchMousePress((float)x, (float)y, button);

    if (hitNode) {
        cout << "Hit node received event" << endl;
    } else {
        cout << "No hit (clicked background)" << endl;
    }
}

void tcApp::mouseReleased(int x, int y, int button) {
    dispatchMouseRelease((float)x, (float)y, button);
}

void tcApp::mouseMoved(int x, int y) {
    // Update hover state
    // Currently simple implementation (check all buttons)
    Ray globalRay = Ray::fromScreenPoint2D((float)x, (float)y);

    // Update hover state for each button
    auto updateHover = [&](CounterButton::Ptr btn) {
        // Get button's global inverse matrix
        Mat4 globalInv = btn->getGlobalMatrixInverse();
        Ray localRay = globalRay.transformed(globalInv);

        float dist;
        btn->isHovered = btn->hitTest(localRay, dist);
    };

    updateHover(button1_);
    updateHover(button2_);
    updateHover(button3_);
    updateHover(panelButton1_);
    updateHover(panelButton2_);
}
