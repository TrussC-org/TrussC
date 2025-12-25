#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// easyCamExample - EasyCam 3D camera control demo
// Implementation based on oF's easyCamExample

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void mousePressed(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseScrolled(Vec2 delta) override;

private:
    EasyCam cam;
    bool showHelp = true;

    // Meshes for 3D primitives
    Mesh boxMesh;
    Mesh sphereMesh;
    Mesh coneMesh;
    Mesh cylinderMesh;

    // Lighting
    Light light;
    Material matRed, matOrange, matBlue, matCyan, matYellow, matMagenta;

    void drawGrid(float size, int divisions);
};
