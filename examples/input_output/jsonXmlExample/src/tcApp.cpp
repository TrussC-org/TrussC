#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("jsonXmlExample");
    tcSetConsoleLogLevel(LogLevel::Verbose);

    addMessage("=== JSON/XML Example ===");
    addMessage("");
    addMessage("Press 'j' to test JSON");
    addMessage("Press 'x' to test XML");
    addMessage("");
}

void tcApp::draw() {
    clear(30);

    setColor(1.0f);
    float y = 20;
    for (const auto& msg : messages) {
        drawBitmapString(msg, 20, y);
        y += 15;
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'j' || key == 'J') {
        testJson();
    } else if (key == 'x' || key == 'X') {
        testXml();
    }
}

void tcApp::addMessage(const string& msg) {
    messages.push_back(msg);
    // Remove old messages to fit on screen
    while (messages.size() > 35) {
        messages.erase(messages.begin());
    }
}

void tcApp::testJson() {
    addMessage("--- JSON Test ---");

    // Create JSON
    Json j;
    j["name"] = "TrussC";
    j["version"] = 0.1;
    j["features"] = {"graphics", "audio", "events"};
    j["settings"]["width"] = 1024;
    j["settings"]["height"] = 768;
    j["settings"]["fullscreen"] = false;

    // Convert to string
    string jsonStr = toJsonString(j, 2);
    addMessage("Created JSON:");

    // Display each line
    stringstream ss(jsonStr);
    string line;
    while (getline(ss, line)) {
        addMessage("  " + line);
    }

    // Save to file
    string path = "/tmp/trussc_test.json";
    if (saveJson(j, path)) {
        addMessage("Saved to: " + path);
    }

    // Load from file
    Json loaded = loadJson(path);
    if (!loaded.empty()) {
        addMessage("Loaded back:");
        addMessage("  name: " + loaded["name"].get<string>());
        addMessage("  version: " + to_string(loaded["version"].get<double>()));
        addMessage("  features count: " + to_string(loaded["features"].size()));
    }

    addMessage("");
}

void tcApp::testXml() {
    addMessage("--- XML Test ---");

    // Create XML
    Xml xml;
    xml.addDeclaration();

    auto root = xml.addRoot("project");
    root.append_attribute("name") = "TrussC";

    auto info = root.append_child("info");
    info.append_child("version").text() = "0.1";
    info.append_child("author").text() = "TrussC Team";

    auto features = root.append_child("features");
    features.append_child("feature").text() = "graphics";
    features.append_child("feature").text() = "audio";
    features.append_child("feature").text() = "events";

    // Convert to string
    string xmlStr = xml.toString();
    addMessage("Created XML:");

    // Display each line
    stringstream ss(xmlStr);
    string line;
    while (getline(ss, line)) {
        addMessage("  " + line);
    }

    // Save to file
    string path = "/tmp/trussc_test.xml";
    if (xml.save(path)) {
        addMessage("Saved to: " + path);
    }

    // Load from file
    Xml loaded = loadXml(path);
    if (!loaded.empty()) {
        addMessage("Loaded back:");
        auto loadedRoot = loaded.root();
        addMessage("  project name: " + string(loadedRoot.attribute("name").value()));
        addMessage("  version: " + string(loadedRoot.child("info").child("version").text().get()));

        int featureCount = 0;
        for (auto f : loadedRoot.child("features").children("feature")) {
            featureCount++;
        }
        addMessage("  features count: " + to_string(featureCount));
    }

    addMessage("");
}
