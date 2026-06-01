#include "tcApp.h"

// Displays:
//   - Platform (compile-time OS detection)
//   - GraphicsBackend (runtime sokol_gfx backend query)
//   - BuildInfo (timestamps injected by trussc_app() at CMake configure time)
//
// Handy for verifying behavior across macOS / Windows / Linux / iOS / Android /
// Web (WebGL2 / WebGPU).

void tcApp::setup() {
    auto tf = [](bool b) { return b ? "true" : "false"; };
    logNotice(string("Platform.name           = ") + Platform::name());
    logNotice(string("Platform.isMacOS        = ") + tf(Platform::isMacOS()));
    logNotice(string("Platform.isDesktop      = ") + tf(Platform::isDesktop()));
    logNotice(string("GraphicsBackend.name    = ") + GraphicsBackend::name());
    logNotice(string("GraphicsBackend.isMetal = ") + tf(GraphicsBackend::isMetal()));
    logNotice(string("BuildInfo.dateTime      = ") + BuildInfo::dateTime());
    logNotice(string("BuildInfo.year          = ") + to_string(BuildInfo::year()));
    logNotice(string("BuildInfo.month         = ") + to_string(BuildInfo::month()));
    logNotice(string("BuildInfo.day           = ") + to_string(BuildInfo::day()));
    logNotice(string("BuildInfo.timestamp     = ") + to_string(BuildInfo::timestamp()));
}
void tcApp::update() {}

static float drawSectionTitle(float x, float y, const string& title) {
    setColor(0.55f, 0.75f, 1.0f);
    drawBitmapString(title, x, y);
    setColor(colors::white);
    return y + 22;
}

static float drawKV(float x, float y, const string& key, const string& value) {
    setColor(0.55f);
    drawBitmapString(key, x, y);
    setColor(colors::white);
    drawBitmapString(value, x + 140, y);
    return y + 16;
}

static float drawFlag(float x, float y, const string& name, bool value) {
    setColor(0.55f);
    drawBitmapString(name, x, y);
    setColor(value ? colors::green : Color(0.35f, 0.35f, 0.35f));
    drawBitmapString(value ? "Y" : "N", x + 140, y);
    setColor(colors::white);
    return y + 16;
}

void tcApp::draw() {
    clear(0.08f);

    const float x = 20;
    float y = 24;

    setColor(colors::white);
    drawBitmapString("TrussC Platform Info", x, y);
    y += 30;

    // --- Platform (compile-time) ---
    y = drawSectionTitle(x, y, "Platform");
    y = drawKV(x, y, "name", Platform::name());
    y = drawFlag(x, y, "isWeb",     Platform::isWeb());
    y = drawFlag(x, y, "isMacOS",   Platform::isMacOS());
    y = drawFlag(x, y, "isIOS",     Platform::isIOS());
    y = drawFlag(x, y, "isWindows", Platform::isWindows());
    y = drawFlag(x, y, "isAndroid", Platform::isAndroid());
    y = drawFlag(x, y, "isLinux",   Platform::isLinux());
    y += 4;
    y = drawFlag(x, y, "isApple",   Platform::isApple());
    y = drawFlag(x, y, "isMobile",  Platform::isMobile());
    y = drawFlag(x, y, "isDesktop", Platform::isDesktop());
    y += 12;

    // --- Graphics Backend (runtime) ---
    y = drawSectionTitle(x, y, "Graphics Backend");
    y = drawKV(x, y, "name", GraphicsBackend::name());
    y = drawFlag(x, y, "isOpenGL", GraphicsBackend::isOpenGL());
    y = drawFlag(x, y, "isMetal",  GraphicsBackend::isMetal());
    y = drawFlag(x, y, "isD3D11",  GraphicsBackend::isD3D11());
    y = drawFlag(x, y, "isWebGPU", GraphicsBackend::isWebGPU());
    y = drawFlag(x, y, "isWebGL2", GraphicsBackend::isWebGL2());
    y = drawFlag(x, y, "isVulkan", GraphicsBackend::isVulkan());
    y += 12;

    // --- Build Info ---
    y = drawSectionTitle(x, y, "Build Info");
    y = drawKV(x, y, "dateTime",  BuildInfo::dateTime());
    y = drawKV(x, y, "date",      BuildInfo::date());
    y = drawKV(x, y, "time",      BuildInfo::time());
    y = drawKV(x, y, "timestamp", to_string(BuildInfo::timestamp()));
    y = drawKV(x, y, "y/m/d",
               to_string(BuildInfo::year()) + "/"
             + to_string(BuildInfo::month()) + "/"
             + to_string(BuildInfo::day()));
    y = drawKV(x, y, "h:m:s",
               to_string(BuildInfo::hour()) + ":"
             + to_string(BuildInfo::minute()) + ":"
             + to_string(BuildInfo::second()));
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
