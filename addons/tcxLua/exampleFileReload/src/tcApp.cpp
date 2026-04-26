#include "tcApp.h"

#ifdef _WIN32
std::string sep = "\\";
#else
std::string sep = "/";
#endif // _WIN32

void tcApp::reloadLuaFile(){
    std::string luaScriptBaseName = "sketch.lua";
    std::string luaScriptPath = getDataPath(luaScriptBaseName);

    if(fileExists(luaScriptPath)){
        sol::optional<sol::error> result = lua->safe_script_file(luaScriptPath);
        if (result.has_value()) {
            std::cerr << "Lua execution failed: "
                    << result.value().what() << std::endl;
        }
    }else{
        tcLogError("tcApp") << "Lua file not found at: " << luaScriptPath;
    }
}

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    reloadLuaFile();

    // lua->script("setup()");
    ((*lua)["setup"])();
}

void tcApp::update() {
    // lua->script("update()");
    ((*lua)["update"])();
}

void tcApp::draw() {
    pushStyle();

    // lua->script("draw()");
    ((*lua)["draw"])();

    popStyle();

    setColor(1.0f);
    drawBitmapString("key [R] to reload lua file", 20, getHeight() - 50);
}

void tcApp::keyPressed(int key) {
    // lua->script("keyPressed(" + std::to_string(key) + ")");
    ((*lua)["keyPressed"])(key);

    if(key == 'r' || key == 'R'){
        reloadLuaFile();

        // lua->script("setup()");
        ((*lua)["setup"])();
    }
}

void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(Vec2 pos, int button) {}
void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
