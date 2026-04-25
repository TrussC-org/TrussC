#include "tcApp.h"

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    couldUseLuaJITFromSol2 = tcx_lua.canUseLuaJITFromSol2();
    estimatedLuaVersion = tcx_lua.getLuaVersionEstimated(lua);
    estimatedLuaJitUsed = tcx_lua.isLuaJITUsedEstimated(lua);

    (*lua)["couldUseLuaJITFromSol2"] = couldUseLuaJITFromSol2;
    (*lua)["estimatedLuaVersion"] = estimatedLuaVersion;
    (*lua)["estimatedLuaJitUsed"] = estimatedLuaJitUsed;

    std::string setupLuaSource = R"LUA(
        print("LuaJIT enabled on Sol2: " .. (couldUseLuaJITFromSol2 and "Yes" or "No"))
        print("LuaVersion (estimated): " .. estimatedLuaVersion)
        print("LuaJIT Enabled (estimated): " .. (estimatedLuaJitUsed and "Yes" or "No"))
    )LUA";

    sol::optional<sol::error> result = lua->safe_script(setupLuaSource);
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
                drawBitmapString("LuaJIT enabled on Sol2: " .. (couldUseLuaJITFromSol2 and "Yes" or "No"), 20, 20, true)
                drawBitmapString("LuaVersion (estimated): " .. estimatedLuaVersion, 20, 60, true)
                drawBitmapString("LuaJIT Enabled (estimated): " .. (estimatedLuaJitUsed and "Yes" or "No"), 20, 80, true)
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

void tcApp::mousePressed(Vec2 pos, int button) {}
void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
