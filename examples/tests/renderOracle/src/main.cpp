// =============================================================================
// renderOracle — a DETERMINISTIC render sample used as a regression oracle for the
// RenderTarget / pipeline-selection refactor.
//
// It draws a fixed composition that exercises the pipeline-selection paths the
// refactor touches, all in one frame:
//   - 2D primitives (rect / circle / line) in several BlendModes
//   - bitmap text
//   - a straight-alpha image (Texture path)
//   - an FBO with 2D drawn into it, composited back (premultiplied)
//   - an FBO with a 3D PBR scene drawn into it (3D-in-FBO), composited back
//   - a 3D PBR scene drawn directly to the swapchain
//
// Determinism: fixed RNG seed, NO time/frame-based animation, fixed transforms.
// On a fixed frame it writes the swapchain to $ORACLE_OUT (default /tmp/oracle.bmp,
// lossless BMP) and quits. Run it before and after a change, diff the two BMPs
// (see diff.py) to detect rendering regressions.
// =============================================================================
#include <TrussC.h>
#include <cstdlib>
using namespace std;
using namespace tc;

class OracleApp : public App {
public:
    void setup() override {
        randomSeed(12345);                 // determinism (in case any path uses random)
        setIndependentFps(60, 60);

        // A small straight-alpha image (deterministic gradient + checker alpha).
        img_.allocate(64, 64, 4);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                float a = ((x / 8 + y / 8) % 2) ? 1.0f : 0.4f;
                img_.setColor(x, y, Color(x / 63.0f, y / 63.0f, 0.5f, a));
            }
        img_.update();

        fbo2d_.allocate(400, 300, 4, TextureFormat::RGBA8, false);
        fbo3d_.allocate(400, 300, 4, TextureFormat::RGBA8, false);  // 4:3 matches window aspect

        sphere_ = createSphere(120.0f, 32);
        box_    = createBox(150.0f);

        light_.setDirectional(-0.4f, -1.0f, -0.7f);
        light_.setDiffuse(1.0f, 0.95f, 0.85f);
        light_.setAmbient(0.12f, 0.12f, 0.15f);
        light_.setIntensity(3.0f);

        matA_.setBaseColor(0.85f, 0.30f, 0.25f).setMetallic(0.1f).setRoughness(0.45f);
        matB_.setBaseColor(0.30f, 0.55f, 0.85f).setMetallic(0.8f).setRoughness(0.25f);

        cam_.setDistance(520);
        cam_.setTarget(0, 0, 0);
    }

    // Draw a small 2D vignette (primitives + blend modes + text). Used both on the
    // swapchain and inside an FBO so both targets exercise the same 2D paths.
    void draw2DVignette(float w, float h) {
        // solid swatches (Alpha)
        resetBlendMode();
        const float sw[6][3] = {{1,0,0},{0,1,0},{0,0,1},{0.25f,0.5f,0.4f},{0.5f,0.5f,0.5f},{1,1,1}};
        for (int i = 0; i < 6; ++i) { setColor(sw[i][0], sw[i][1], sw[i][2]); drawRect(8 + i * 30, 8, 26, 26); }

        // additive overlapping circles
        setBlendMode(BlendMode::Add);
        setColor(0.6f, 0.1f, 0.1f); drawCircle(60, 90, 28);
        setColor(0.1f, 0.6f, 0.1f); drawCircle(80, 90, 28);
        setColor(0.1f, 0.1f, 0.6f); drawCircle(70, 110, 28);

        // multiply
        setBlendMode(BlendMode::Multiply);
        setColor(0.9f, 0.9f, 0.3f); drawCircle(170, 95, 30);
        setColor(0.3f, 0.9f, 0.9f); drawCircle(195, 95, 30);

        // screen
        setBlendMode(BlendMode::Screen);
        setColor(0.5f, 0.2f, 0.6f); drawCircle(280, 95, 30);
        setColor(0.2f, 0.5f, 0.6f); drawCircle(305, 95, 30);

        resetBlendMode();
        setColor(0.9f, 0.9f, 0.95f);
        for (int i = 0; i <= 5; ++i) drawLine(8, 150 + i * 6, w - 8, 150 + i * 6);
        drawBitmapString("renderOracle 2D", 8, 200);

        // straight-alpha image
        setColor(1, 1, 1);
        img_.draw(8, 220);
        img_.draw(80, 220, 64, 64);
    }

    // 3D PBR scene. drawn into whatever target is current (swapchain or FBO).
    void draw3DScene(Mesh& mesh, Material& mat) {
        cam_.begin();
        clearLights();
        setCameraPosition(cam_.getPosition());
        addLight(light_);
        setMaterial(mat);
        mesh.draw();
        clearMaterial();
        cam_.end();
    }

    void draw() override {
        clear(0.06f, 0.06f, 0.08f);

        // (1) 3D PBR directly on the swapchain (active3D + swapchain target).
        draw3DScene(box_, matB_);

        // (2) 2D directly on the swapchain (over the 3D, in the top band).
        draw2DVignette((float)getWindowWidth(), (float)getWindowHeight());

        // (3) FBO with 2D drawn into it, composited back (premultiplied composite).
        // A bright OPAQUE backdrop sits under the panel so the FBO's transparent /
        // semi-transparent areas composite over a non-black color — this is what
        // actually exercises the alpha channel (over black, OVER == ADD and alpha
        // bugs hide).
        resetBlendMode();
        setColor(0.85f, 0.80f, 0.30f);
        drawRect((float)getWindowWidth() - 410, 10, 400, 300);
        fbo2d_.begin(0, 0, 0, 0);
        draw2DVignette((float)fbo2d_.getWidth(), (float)fbo2d_.getHeight());
        fbo2d_.end();
        setColor(1, 1, 1);
        fbo2d_.draw((float)getWindowWidth() - 410, 10, 400, 300);

        // (4) FBO with a 3D PBR scene (3D-in-FBO), composited back.
        fbo3d_.begin(0, 0, 0, 0);
        draw3DScene(sphere_, matA_);
        fbo3d_.end();
        setColor(1, 1, 1);
        fbo3d_.draw((float)getWindowWidth() - 410, (float)getWindowHeight() - 310, 400, 300);

        // Capture on a settled frame (font atlas / pipelines warmed), then quit.
        if (getFrameCount() == 4) {
            const char* out = std::getenv("ORACLE_OUT");
            string path = out ? out : "/tmp/oracle.bmp";
            bool ok = saveScreenshot(path);
            logNotice("oracle") << (ok ? "saved " : "FAILED ") << path;
            sapp_quit();
        }
    }

private:
    Image img_;
    Fbo fbo2d_, fbo3d_;
    Mesh sphere_, box_;
    Light light_;
    Material matA_, matB_;
    EasyCam cam_;
};

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setHighDpi(false);   // fixed framebuffer size (no retina DPI race) -> deterministic capture
    settings.setTitle("renderOracle");
    return TC_RUN_APP(OracleApp, settings);
}
