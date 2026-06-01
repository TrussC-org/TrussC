#include "tcApp.h"

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    sol::optional<sol::error> result = lua->safe_script("print(\"Hello from Lua!\")");
	if (result.has_value()) {
		std::cerr << "Lua execution failed: "
		          << result.value().what() << std::endl;
	}
}

void tcApp::update() {

}

void tcApp::draw() {
    if(isFirstDraw){
        std::string drawLuaSource = R"LUA(
            function draw()
                clear(0.12, 1.0)

                -- Rotating box
                noFill()
                translate(getWindowWidth() / 2, getWindowHeight() / 2)
                rotate(getElapsedTimef() * 0.1, getElapsedTimef() * 0.15, 0);
                drawBox(200.0)
            end
        )LUA";

        lua->script(drawLuaSource);

        isFirstDraw = false;
    }

    // lua->script("draw()");
    ((*lua)["draw"])();
}

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
