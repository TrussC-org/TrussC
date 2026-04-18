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

    // Test SoundBuffer AAC loading (will log warning on Web)
    tc::SoundBuffer soundTest;
    soundTest.loadAac("nonexistent.aac");
    soundTest.loadAacFromMemory(nullptr, 0);
    tc::logNotice("AllFeaturesExample") << "SoundBuffer AAC test completed";

    // Test VideoPlayer audio methods (will log warning on Web)
    tc::VideoPlayer videoTest;
    videoTest.hasAudio();
    videoTest.getAudioCodec();
    videoTest.getAudioData();
    videoTest.getAudioSampleRate();
    videoTest.getAudioChannels();
    tc::logNotice("AllFeaturesExample") << "VideoPlayer audio methods test completed";

    // Test file/directory utilities (tcFile.h)
    tc::logNotice("AllFeaturesExample") << "Testing file utilities...";

    // Path utilities
    string testPath = "path/to/file.txt";
    tc::getFileName(testPath);
    tc::getBaseName(testPath);
    tc::getFileExtension(testPath);
    tc::getParentDirectory(testPath);
    tc::joinPath("path", "file.txt");

    // File system operations
    tc::fileExists("nonexistent.txt");
    tc::directoryExists(".");
    tc::listDirectory(".");

    // FileWriter test
    {
        tc::FileWriter writer;
        // Don't actually write to test
        tc::logNotice("AllFeaturesExample") << "FileWriter instantiation OK";
    }

    // FileReader test
    {
        tc::FileReader reader;
        // Don't actually read to test
        tc::logNotice("AllFeaturesExample") << "FileReader instantiation OK";
    }

    tc::logNotice("AllFeaturesExample") << "File utilities test completed";

    // --- 3D / PBR Lighting ---
    tc::logNotice("AllFeaturesExample") << "Testing 3D/PBR features...";

    // Light (all types)
    tc::Light dirLight;
    dirLight.setDirectional(tc::Vec3(0, -1, 0));
    dirLight.setDiffuse(1.0f, 1.0f, 1.0f);
    dirLight.setIntensity(1.0f);

    tc::Light spotLight;
    spotLight.setSpot(tc::Vec3(0, 100, 0), tc::Vec3(0, -1, 0), 0.0f, 0.45f);
    spotLight.setAttenuation(1.0f, 0.0f, 0.0f);
    spotLight.enableShadow(512);
    spotLight.setShadowBias(1.0f);

    // Material (PBR presets)
    tc::Material matGold = tc::Material::gold();
    tc::Material matPlastic = tc::Material::plastic(tc::Color(0.8f, 0.2f, 0.2f));
    tc::Material matEmerald = tc::Material::emerald();
    matGold.setBaseColor(1, 0.8f, 0.3f).setMetallic(1.0f).setRoughness(0.2f);

    // IesProfile
    tc::IesProfile iesTest;
    // Environment
    tc::Environment envTest;

    // EasyCam
    tc::EasyCam camTest;
    camTest.setDistance(500);
    camTest.setTarget(0, 0, 0);

    // Mesh primitives
    tc::Mesh sphereMesh = tc::createSphere(50, 16);
    tc::Mesh boxMesh = tc::createBox(50);
    tc::Mesh planeMesh = tc::createPlane(100, 100);
    tc::Mesh cylMesh = tc::createCylinder(20, 60, 12);
    tc::Mesh coneMesh = tc::createCone(20, 60, 12);

    tc::logNotice("AllFeaturesExample") << "3D/PBR test completed";

    // --- GPU Resources ---
    tc::logNotice("AllFeaturesExample") << "Testing GPU resources...";

    tc::Texture texTest;
    tc::Fbo fboTest;
    tc::Pixels pixTest;
    pixTest.allocate(4, 4, tc::PixelFormat::RGBA);

    tc::logNotice("AllFeaturesExample") << "GPU resources test completed";

    // --- Animation / Types ---
    tc::logNotice("AllFeaturesExample") << "Testing animation/types...";

    tc::Tween tweenTest;
    tc::Font fontTest;
    tc::Node nodeTest;
    tc::Rect rectTest(0, 0, 100, 100);
    rectTest.getCenter();

    tc::logNotice("AllFeaturesExample") << "Animation/types test completed";

    // --- Serial ---
    tc::Serial serialTest;

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
