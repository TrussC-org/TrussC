#include "tcApp.h"

void tcApp::setup() {
    // Create a sphere mesh as default
    sphereMesh_ = createSphere(1.0f, 16);
    cam_.enableMouseInput();
    cam_.setDistance(1.5f);

    // Lighting
    light_.setDirectional(-1.0f, -1.0f, -1.0f);
    light_.setDiffuse(1.0f, 0.95f, 0.85f);
    light_.setIntensity(4.0f);
    material_ = Material::plastic(Color(0.8f, 0.8f, 0.8f));

    // IBL for ambient reflections
    env_.loadProcedural();
    setEnvironment(env_);

    // Load bundled model and center camera on it
    if (loader_.load("pottedPlant.obj")) {
        hasModel_ = true;
        // Compute bounding box center for camera target
        Mesh merged = loader_.getMesh();
        Vec3 minP(1e9f, 1e9f, 1e9f), maxP(-1e9f, -1e9f, -1e9f);
        for (auto& v : merged.getVertices()) {
            if (v.x < minP.x) minP.x = v.x; if (v.x > maxP.x) maxP.x = v.x;
            if (v.y < minP.y) minP.y = v.y; if (v.y > maxP.y) maxP.y = v.y;
            if (v.z < minP.z) minP.z = v.z; if (v.z > maxP.z) maxP.z = v.z;
        }
        Vec3 center = (minP + maxP) * 0.5f;
        float size = (maxP - minP).length();
        cam_.setTarget(center);
        cam_.setDistance(size * 1.5f);
    }

    logNotice() << "Drop an OBJ file to load, or press 'E' to export sphere";
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.15f, 0.15f, 0.2f);

    cam_.begin();
    clearLights();
    addLight(light_);
    setCameraPosition(cam_.getPosition());

    if (hasModel_ && !loader_.getGroups().empty()) {
        for (auto& group : loader_.getGroups()) {
            // Use material loaded from MTL (converted to PBR)
            setMaterial(group.material);
            if (group.hasTexture) {
                group.mesh.draw(group.texture);
            } else {
                group.mesh.draw();
            }
        }
    } else {
        setMaterial(material_);
        sphereMesh_.draw();
    }

    clearMaterial();
    cam_.end();
}

void tcApp::keyPressed(int key) {
    if (key == 'E') {
        ObjExporter exporter;
        exporter.addMesh(sphereMesh_, "sphere");
        if (exporter.save("test_sphere.obj")) {
            logNotice() << "Exported to data/test_sphere.obj";
        }
    }
}

void tcApp::filesDropped(const vector<string>& files) {
    for (auto& file : files) {
        // Find first .obj file
        if (toLower(getFileExtension(utf8ToPath(file))) == "obj") {
            loader_.clear();
            if (loader_.load(file)) {
                hasModel_ = true;
                logNotice() << "Loaded: " << file << " (" << loader_.getNumGroups() << " groups)";
            }
            return;
        }
    }
    logWarning() << "No .obj file found in dropped files";
}
