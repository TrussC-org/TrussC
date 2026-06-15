#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("tcxNodeInspector");

    // Input-injection MCP tools (mouse_click / key_press / ...), so an AI
    // agent can drive the demo. No-ops unless TRUSSC_MCP is set.
    mcp::enableDebugger();
    mcp::registerDebuggerTools();

    // A dedicated container we can rebuild without touching the app's own root.
    sceneRoot_ = make_shared<RectNode>();
    sceneRoot_->setName("scene");
    addChild(sceneRoot_);

    buildDemo(*sceneRoot_);

    // One line and the inspector runs itself: it sets up imgui, draws its own
    // frame on sceneRoot_ every frame, and F1 toggles it. The static call
    // returns the singleton, so we chain a custom accent (teal is the default —
    // shown here so it's obvious where to recolor it).
    NodeInspector::attach(*sceneRoot_, KEY_F1).setAccent(Color(0.16f, 0.55f, 0.62f));
}

void tcApp::update() {}

void tcApp::draw() {
    clear(0.11f, 0.12f, 0.13f);

    // A short hint on the canvas. The inspector draws itself (see setup()).
    setColor(0.5f, 0.5f, 0.55f);
    drawBitmapString("click circles (2D) or cubes (3D) to select / shift+drag orbits the camera / F1 toggles the inspector", 16, 620);
}
