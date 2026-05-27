#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("fboMipmapExample");

    cam.setDistance(800);
    cam.setTarget(0, 0, 0);
    cam.enableMouseInput();

    // Same texture pattern, two Fbos.
    fboPlain.allocate(512, 512, 1, TextureFormat::RGBA8, /*mipmaps*/ false);
    fboMipped.allocate(512, 512, 1, TextureFormat::RGBA8, /*mipmaps*/ true);
    renderStripes(fboPlain);
    renderStripes(fboMipped);

    boxMesh = createBox(140);
}

// Black/white vertical stripes, 4 px wide each (8 px period). Sub-texel
// frequency once the cube shrinks below ~64 screen pixels, so the no-mipmap
// version aliases hard while the mipmap version stays smooth.
void tcApp::renderStripes(Fbo& fbo) {
    fbo.begin(1.0f, 1.0f, 1.0f, 1.0f);  // clear to white
    setColor(0.0f, 0.0f, 0.0f);
    for (int x = 0; x < 512; x += 8) {
        drawRect((float)x, 0.0f, 4.0f, 512.0f);
    }
    fbo.end();
}

void tcApp::update() {
    // Sweep the camera from near (cube fills the frame) to far (cube ≈ 30 px
    // on screen). The aliasing in the plain cube becomes obvious well before
    // the far end. Period ≈ 12 s so each cycle gives time to watch one cube
    // go small and back without rushing.
    float t = (float)getElapsedTime();
    cam.setDistance(1400.0f + 1100.0f * sin(t * 0.52f));  // 300 .. 2500
}

void tcApp::draw() {
    clear(0.08f);

    cam.begin();
    setColor(1.0f, 1.0f, 1.0f);

    float t = (float)getElapsedTime();
    float spinY = t * 0.5f;
    float spinX = t * 0.3f;

    // Left cube — no mipmaps. Expect moiré / shimmer when small.
    pushMatrix();
    translate(-150, 0, 0);
    rotateY(spinY);
    rotateX(spinX);
    boxMesh.draw(fboPlain.getTexture());
    popMatrix();

    // Right cube — mipmaps. Stays smooth at every scale.
    pushMatrix();
    translate(150, 0, 0);
    rotateY(spinY);
    rotateX(spinX);
    boxMesh.draw(fboMipped.getTexture());
    popMatrix();

    cam.end();

    // 2D HUD
    setColor(1.0f);
    drawBitmapString("LEFT cube : Fbo mipmaps OFF (watch the stripes shimmer when far)", 20, 25);
    drawBitmapString("RIGHT cube: Fbo mipmaps ON  (stays smooth at any size)", 20, 45);
    drawBitmapString("Mouse: left-drag rotate, scroll zoom", 20, getHeight() - 20);
}
