#include "tcApp.h"
#include "binding_test.glsl.h"
#include <chrono>

// Lua binding verification harness.
// Loads data/bindcheck.lua (generated alongside data/expected.lua) which checks
// that every expected binding name is reachable from Lua and exercises a safe
// subset of pure functions. Results are printed to stderr, then the app exits.

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    const auto source = fs::path(__FILE__).parent_path().parent_path().parent_path() / "tests";
    const auto temp = fs::temp_directory_path() /
        ("tcxlua-bindcheck-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp);
    (*lua)["bindingTestSource"] = source;
    (*lua)["bindingTestTemp"] = temp;
#ifndef __EMSCRIPTEN__
    (*lua)["bindingTestNativeAudio"] = true;
#else
    (*lua)["bindingTestNativeAudio"] = false;
#endif
    Shader shader;
    shader.load(binding_test_triangles_shader_desc);
    (*lua)["bindingTestShader"] = &shader;
    tcxLua::runFile(*lua, getDataPath("bindcheck.lua"));
    (*lua)["bindingTestShader"] = sol::nil;
    std::error_code error;
    fs::remove_all(temp, error);

    // No need to keep a window open; exit as soon as the check has run.
    exitApp();
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
