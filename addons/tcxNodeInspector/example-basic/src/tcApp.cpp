#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("tcxNodeInspector");
    imguiSetup();   // registers the ImGui overlay (and MCP ImGui tools when TRUSSC_MCP is set)

    // Input-injection MCP tools (mouse_click / key_press / ...), so an AI
    // agent can drive the demo. No-ops unless TRUSSC_MCP is set.
    mcp::enableDebugger();
    mcp::registerDebuggerTools();

    // A dedicated container we can rebuild without touching the app's own root.
    sceneRoot_ = make_shared<RectNode>();
    sceneRoot_->setName("scene");
    addChild(sceneRoot_);

    buildDemo(*sceneRoot_);

    // Give the inspector a distinct accent (teal is the default — shown here so
    // it's obvious where to recolor it).
    inspector_.setAccent(Color(0.16f, 0.55f, 0.62f));
}

void tcApp::update() {}

void tcApp::keyPressed(const KeyEventArgs& e) {
    if (e.key == KEY_F1) inspector_.toggle();   // hide/show the whole tool
}

void tcApp::draw() {
    clear(0.11f, 0.12f, 0.13f);

    // A short hint on the canvas.
    setColor(0.5f, 0.5f, 0.55f);
    drawBitmapString("click circles (2D) or cubes (3D) to select / shift+drag orbits the camera / F1 toggles the inspector", 16, 620);

    imguiBegin();
    inspector_.draw(*sceneRoot_);   // hierarchy + inspector + gizmo
    imguiEnd();
}

void tcApp::cleanup() {
    imguiShutdown();
}
