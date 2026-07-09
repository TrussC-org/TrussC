#include "tcApp.h"

// Lua binding verification harness.
// Loads data/bindcheck.lua (generated alongside data/expected.lua) which checks
// that every expected binding name is reachable from Lua and exercises a safe
// subset of pure functions. Results are printed to stderr, then the app exits.

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    const std::string scriptPath = getDataPath("bindcheck.lua");
    sol::optional<sol::error> result = lua->safe_script_file(scriptPath);
    if (result.has_value()) {
        tcLogError("bindcheck") << "Lua execution failed: " << result.value().what();
    }

    // No need to keep a window open; exit as soon as the check has run.
    requestExitApp();
}

void tcApp::update() {}
void tcApp::draw() {}

void tcApp::keyPressed(int key) {}
void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(const MouseEventArgs& e) {}
void tcApp::mouseReleased(const MouseEventArgs& e) {}
void tcApp::mouseMoved(const MouseMoveEventArgs& e) {}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {}
void tcApp::mouseScrolled(const ScrollEventArgs& e) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
