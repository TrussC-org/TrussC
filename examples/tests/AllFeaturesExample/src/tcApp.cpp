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

    // LUT (3D color grading)
    lut = tcx::lut::createVintage(16);
    tc::logNotice("AllFeaturesExample") << "LUT initialized: " << lut.getSize() << "x" << lut.getSize() << "x" << lut.getSize();

    tc::logNotice("AllFeaturesExample") << "All features linked successfully";
}

void tcApp::update() {
    box2d.update();
}

void tcApp::draw() {
    clear(0.12f);

    pushMatrix();

    // Rotating box (Core graphics test)
    noFill();
    setColor(colors::white);
    translate(getWindowWidth() / 2.0f, getWindowHeight() / 2.0f);
    rotate(getElapsedTimef() * 0.5f);
    drawBox(200.0f);

    popMatrix();

    // beginStroke/endStroke test
    setColor(colors::hotPink);
    setStrokeWeight(8.0f);
    setStrokeCap(StrokeCap::Round);
    setStrokeJoin(StrokeJoin::Round);
    beginStroke();
    vertex(50, 50);
    vertex(150, 80);
    vertex(100, 150);
    endStroke();

    setColor(colors::white);
    drawBitmapString("All Features Test", 10, 20);
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
