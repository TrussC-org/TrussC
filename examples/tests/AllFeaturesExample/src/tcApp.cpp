#include "tcApp.h"

void tcApp::setup() {
    tc::logNotice("AllFeaturesExample") << "Initializing all addons...";

    // Box2D
    box2d.setup();
    tc::logNotice("AllFeaturesExample") << "Box2D initialized";

    // OSC
    oscSender.setup("127.0.0.1", 12345);
    oscReceiver.setup(12346);
    tc::logNotice("AllFeaturesExample") << "OSC initialized";

    // TLS
    // Just instantiation to check linking
    tc::TlsClient tls;
    
    // WebSocket
    tc::WebSocketClient ws;

    tc::logNotice("AllFeaturesExample") << "All addons linked successfully";
}

void tcApp::update() {
    box2d.update();
}

void tcApp::draw() {
    clear(0.12f);

    // Rotating box (Core graphics test)
    noFill();
    setColor(colors::white);
    translate(getWindowWidth() / 2.0f, getWindowHeight() / 2.0f);
    rotate(getElapsedTimef() * 0.5f);
    drawBox(200.0f);
    
    drawBitmapString("All Features Test", -60, 0);
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
