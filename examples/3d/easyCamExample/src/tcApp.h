#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

using namespace trussc;

// easyCamExample - EasyCamによる3Dカメラ操作のデモ
// oFのeasyCamExampleを参考に実装

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float dx, float dy) override;

private:
    EasyCam cam;
    bool showHelp = true;

    // 3Dプリミティブ用メッシュ
    Mesh boxMesh;
    Mesh sphereMesh;
    Mesh coneMesh;
    Mesh cylinderMesh;

    // ライティング
    Light light;
    Material matRed, matOrange, matBlue, matCyan, matYellow, matMagenta;

    void drawGrid(float size, int divisions);
};
