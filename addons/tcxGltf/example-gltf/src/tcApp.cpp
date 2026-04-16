#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("glTF Viewer");

    cam.setDistance(300);
    cam.enableMouseInput();

    // Key light (warm white from upper-left)
    keyLight.setDirectional(Vec3(-1, -2, -1));
    keyLight.setDiffuse(1.0f, 0.98f, 0.95f);
    keyLight.setIntensity(2.5f);

    // Fill light (cool, from the right)
    fillLight.setDirectional(Vec3(1, -0.5f, 0.5f));
    fillLight.setDiffuse(0.7f, 0.8f, 1.0f);
    fillLight.setIntensity(0.8f);

    // Procedural IBL
    env.loadProcedural();
    setEnvironment(env);

    // Load default model
    loadModel("Duck.glb");
}

void tcApp::draw() {
    clear(0.12f, 0.12f, 0.14f);

    cam.begin();

    clearLights();
    addLight(keyLight);
    addLight(fillLight);
    setCameraPosition(cam.getPosition());

    if (model.isLoaded()) {
        pushMatrix();
        // Auto-rotate around Y axis
        rotateY(getElapsedTimef() * 0.5f);
        model.draw();
        popMatrix();
    }

    setLightingMode(LightingMode::CpuPhong);
    clearPbrMaterial();

    cam.end();

    // HUD
    setColor(1.0f);
    string info = "glTF Viewer";
    if (model.isLoaded()) {
        info += "\n" + modelName;
        info += " (" + to_string(model.getNodeCount()) + " nodes)";
    } else {
        info += "\nno model loaded";
    }
    info += "\ndrop .glb/.gltf to load";
    drawBitmapString(info, 20, 20);
}

void tcApp::filesDropped(const vector<string>& files) {
    for (const auto& f : files) {
        // Accept .glb and .gltf files
        if (f.size() > 4) {
            string ext = f.substr(f.size() - 4);
            if (ext == ".glb" || ext == "gltf" || ext == ".glb") {
                loadModel(f);
                return;
            }
            // Also check 5-char extension .gltf
            if (f.size() > 5 && f.substr(f.size() - 5) == ".gltf") {
                loadModel(f);
                return;
            }
        }
    }
}

void tcApp::loadModel(const string& path) {
    if (model.load(path)) {
        // Extract filename for display
        auto pos = path.find_last_of("/\\");
        modelName = (pos != string::npos) ? path.substr(pos + 1) : path;

        // Auto-fit camera distance based on model bounds
        // Simple heuristic: scan all vertices for bounding sphere radius
        float maxDist = 1.0f;
        for (size_t i = 0; i < model.getNodeCount(); i++) {
            const auto& verts = model.getNode(i).mesh.getVertices();
            for (const auto& v : verts) {
                float d = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                if (d > maxDist) maxDist = d;
            }
        }
        cam.setDistance(maxDist * 2.5f);
        cam.setTarget(0, maxDist * 0.3f, 0);
    }
}
