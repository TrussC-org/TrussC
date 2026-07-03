#include "tcApp.h"

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE == 1
#define FILE_RELOAD_UNSUPPORTED
#elif defined(__ANDROID__)
#define FILE_RELOAD_UNSUPPORTED
#elif defined(__EMSCRIPTEN__)
#define FILE_RELOAD_UNSUPPORTED
#elif defined(_WIN32)
#define FILE_RELOAD_SUPPORTED
#elif defined(TARGET_OS_MAC) && TARGET_OS_MAC == 1
#define FILE_RELOAD_SUPPORTED
#elif defined(__linux__)
#define FILE_RELOAD_SUPPORTED
#else
#define FILE_RELOAD_UNSUPPORTED
#endif

#ifdef _WIN32
std::string sep = "\\";
#else
std::string sep = "/";
#endif // _WIN32

void tcApp::reloadLuaFile(){
#ifdef FILE_RELOAD_SUPPORTED
    std::string luaScriptBaseName = "sketch.lua";
    fs::path luaScriptPath = getDataPath(luaScriptBaseName);

    if(fileExists(luaScriptPath)){
        sol::optional<sol::error> result = lua->safe_script_file(luaScriptPath.string());
        if (result.has_value()) {
            std::cerr << "Lua execution failed: "
                    << result.value().what() << std::endl;
        }
    }else{
        logError("tcApp") << "Lua file not found at: " << luaScriptPath;
    }
#else
    logWarning("tcApp") << "[file reload] sorry, this platform currently not supported";
#endif // FILE_RELOAD_SUPPORTED
}

void tcApp::setup() {
    lua = tcx_lua.getLuaState();

    reloadLuaFile();

#ifdef FILE_RELOAD_SUPPORTED
    // lua->script("setup()");
    ((*lua)["setup"])();
#endif // FILE_RELOAD_SUPPORTED
}

void tcApp::update() {
#ifdef FILE_RELOAD_SUPPORTED
    // lua->script("update()");
    ((*lua)["update"])();
#endif // FILE_RELOAD_SUPPORTED
}

void tcApp::draw() {
    pushStyle();

#ifdef FILE_RELOAD_SUPPORTED
    // lua->script("draw()");
    ((*lua)["draw"])();
#endif // FILE_RELOAD_SUPPORTED

    popStyle();

#ifdef FILE_RELOAD_SUPPORTED
    setColor(1.0f);
    drawBitmapString("key [R] to reload lua file", 20, getHeight() - 50);
#else
    setColor(1.0f, 1.0f, 0.0f);
    drawBitmapString("[Error] file reload: sorry, this platform currently not supported", 20, 20);
#endif // FILE_RELOAD_SUPPORTED
}

void tcApp::keyPressed(int key) {
#ifdef FILE_RELOAD_SUPPORTED
    // lua->script("keyPressed(" + std::to_string(key) + ")");
    ((*lua)["keyPressed"])(key);

    if(key == 'r' || key == 'R'){
        reloadLuaFile();

        // lua->script("setup()");
        ((*lua)["setup"])();
    }
#endif // FILE_RELOAD_SUPPORTED
}

void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(const MouseEventArgs& e) {}
void tcApp::mouseReleased(const MouseEventArgs& e) {}
void tcApp::mouseMoved(const MouseMoveEventArgs& e) {}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {}
void tcApp::mouseScrolled(const ScrollEventArgs& e) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
