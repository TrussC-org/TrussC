#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("reflect -> inspector + JSON");

    // MCP input tools, so an AI agent can drive the demo (no-op without TRUSSC_MCP).
    mcp::enableDebugger();
    mcp::registerDebuggerTools();

    // A scene container, with one sprite to inspect / serialize.
    sceneRoot_ = make_shared<RectNode>();
    sceneRoot_->setName("scene");
    addChild(sceneRoot_);

    sprite_ = make_shared<SpriteNode>();
    sprite_->setName("sprite");
    sprite_->setPos(590, 500);   // clear of the inspector + JSON panels
    sceneRoot_->addChild(sprite_);

    // The inspector runs itself; F1 toggles it. Select the sprite to edit it.
    NodeInspector::attach(*sceneRoot_, KEY_F1).setAccent(Color(0.16f, 0.55f, 0.62f));
    setSelectedNode(sprite_.get());
}

void tcApp::update() {}

void tcApp::keyPressed(int key) {
    if (key == 'S' || key == 's') {
        // Save: one call turns the whole node (Node transform + our fields +
        // the nested Style group) into JSON.
        snapshot_ = reflectToJson(*sprite_);
        logNotice("snapshot saved:\n" + snapshot_.dump(2));
        status_ = "snapshot saved (edit in the inspector, then press L to restore)";
    } else if (key == 'L' || key == 'l') {
        if (snapshot_.is_object()) {
            // Load: apply the saved JSON back through the reflected setters.
            JsonReadReflector r(snapshot_);
            sprite_->reflectMembers(r);
            status_ = "restored from snapshot";
        } else {
            status_ = "no snapshot yet — press S first";
        }
    }
}

void tcApp::draw() {
    clear(0.11f, 0.12f, 0.13f);

    // The live JSON of the sprite, rebuilt every frame: drag a slider in the
    // inspector and watch it change here. This is the exact data S would save.
    const string json = reflectToJson(*sprite_).dump(2);
    const float x = 700;
    setColor(colors::gold);
    drawBitmapString("reflectToJson(sprite) — live:", x, 28);
    setColor(colors::lightGray);
    float y = 48;
    size_t start = 0;
    while (start <= json.size()) {
        size_t nl = json.find('\n', start);
        string line = json.substr(start, nl == string::npos ? string::npos : nl - start);
        drawBitmapString(line, x, y);
        y += 13;
        if (nl == string::npos) break;
        start = nl + 1;
    }

    setColor(0.6f, 0.6f, 0.65f);
    drawBitmapString("click the sprite to select / F1 inspector / S snapshot / L restore", 16, getWindowHeight() - 40);
    setColor(colors::white);
    drawBitmapString(status_, 16, getWindowHeight() - 22);
}
