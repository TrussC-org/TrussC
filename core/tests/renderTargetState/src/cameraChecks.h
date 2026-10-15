// Run this same check against 56467c6b with cameraFields() returning its
// WindowContext. The public API and expected transitions below are identical.
struct CameraSnapshot {
    Mat4 view, projection;
    float fov, width, height, distance;
    std::shared_ptr<const CameraContext> context;
    Vec3 screen, world, contextScreen;
    Ray ray;
};

// App::handleDraw is the public tree-draw entry; it stamps Node camera state.
class PickProbe : public App {
    bool hitTest(const Ray&, float& distance) override {
        distance = 1;
        return true;
    }
};

CameraSnapshot cameraSnapshot() {
    const auto& camera = cameraFields();
    auto node = std::make_shared<PickProbe>();
    node->handleDraw();
    const auto context = node->getCameraContext();
    return {camera.currentViewMatrix, camera.currentProjectionMatrix,
            camera.currentScreenFov, camera.currentViewW, camera.currentViewH,
            camera.currentCameraDist, context, worldToScreen(Vec3(7, 9, 0)),
            screenToWorld(Vec2(11, 13)), context->worldToScreen(Vec3(7, 9, 0)),
            context->screenPointToRay(11, 13)};
}

bool sameMatrix(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) if (a.m[i] != b.m[i]) return false;
    return true;
}

bool sameVec(const Vec3& a, const Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void expectCamera(const CameraSnapshot& expected, bool pickable, bool sameContext) {
    const auto actual = cameraSnapshot();
    check("view/projection matrices match base transition exactly",
          sameMatrix(actual.view, expected.view) && sameMatrix(actual.projection, expected.projection));
    check("screen FOV, view size and camera distance match base transition exactly",
          actual.fov == expected.fov && actual.width == expected.width &&
          actual.height == expected.height && actual.distance == expected.distance);
    check("public coordinate conversions match base transition exactly",
          sameVec(actual.screen, expected.screen) && sameVec(actual.world, expected.world) &&
          sameVec(actual.contextScreen, expected.contextScreen));
    check("Node camera context and public pick ray match base transition exactly",
          actual.context->pickable == pickable &&
          actual.context->viewW == expected.width && actual.context->viewH == expected.height &&
          sameMatrix(actual.context->view, expected.context->view) &&
          sameMatrix(actual.context->projection, expected.context->projection) &&
          sameVec(actual.ray.origin, expected.ray.origin) &&
          sameVec(actual.ray.direction, expected.ray.direction) &&
          (!sameContext || actual.context == expected.context));
    auto node = std::make_shared<PickProbe>();
    node->handleDraw();
    check("public picking follows restored/offscreen context",
          node->findHitNodeFromScreen(11, 13).hit() == pickable);
}

// Hex floats retain every bit for an exact base/working-tree transcript diff.
void dumpCamera(const char* scenario, const char* phase) {
    const auto s = cameraSnapshot();
    std::printf("CAMERA %s %s", scenario, phase);
    for (float v : s.view.m) std::printf(" %a", double(v));
    for (float v : s.projection.m) std::printf(" %a", double(v));
    for (float v : {s.fov, s.width, s.height, s.distance,
                    s.context->viewW, s.context->viewH,
                    s.screen.x, s.screen.y, s.screen.z,
                    s.world.x, s.world.y, s.world.z,
                    s.contextScreen.x, s.contextScreen.y, s.contextScreen.z,
                    s.ray.origin.x, s.ray.origin.y, s.ray.origin.z,
                    s.ray.direction.x, s.ray.direction.y, s.ray.direction.z,
                    getDefaultScreenFov(), getDefaultScreenFovDeg()})
        std::printf(" %a", double(v));
    for (float v : s.context->view.m) std::printf(" %a", double(v));
    for (float v : s.context->projection.m) std::printf(" %a", double(v));
    std::printf(" pickable=%d\n", int(s.context->pickable));
}

bool cameraChecksFinished = false;
class CameraChecksApp : public App {
    Fbo fbo_;
    EasyCam easy_;
    int frame_ = 0;
    bool primed_ = false;
    CameraSnapshot perspective_, ortho_;
public:
    void setup() override {
        fbo_.allocate(37, 23);
        reviewAfterFrame = events().afterFrame.listen(captureReviewBlend);
        // Establish reference values through the public screen setup API.
        // Skip the setup frame below so normal frame setup replaces these
        // before the first measured scenario.
        setupScreenFovWithSize(getDefaultScreenFov(), 37, 23);
        perspective_ = cameraSnapshot();
        setupScreenFovWithSize(0, 37, 23);
        ortho_ = cameraSnapshot();
    }
    void draw() override {
        if (!primed_) { primed_ = true; return; }
        if (frame_ == 6) { runReviewChecks(fbo_); ++frame_; return; }
        if (frame_ == 7) { exitApp(); return; }
        const char* scenarios[] = {"default-frame", "screen-fov", "screen-ortho",
                                   "easycam", "nested-push-pop", "default-ortho"};
        const char* scenario = scenarios[frame_];
        if (frame_ == 1) setupScreenFov(TAU / 6, 0.25f, 750);
        if (frame_ == 2) setupScreenOrtho();
        if (frame_ == 3) { easy_.setDistance(173); easy_.begin(); }
        if (frame_ == 5) { setDefaultScreenOrtho(); setupScreenOrtho(); }
        const bool nested = frame_ == 4;
        if (nested) {
            pushMatrix(); translate(3, 4, 5);
            pushMatrix(); rotate(0.3f);
            pushStyle(); pushStyle();
        }
        const auto before = cameraSnapshot();
        if (frame_ == 0) {
            check("plain frame starts with the default screen camera",
                  before.fov == TAU / 8 && before.width == 96 && before.height == 64 &&
                  before.context->pickable);
        }
        const auto model = getMatrix();
        const float defaultFov = getDefaultScreenFov();
        dumpCamera(scenario, "before");

        const auto& offscreen = frame_ == 5 ? ortho_ : perspective_;

        // Exercise both begin overloads. Both overwrite camera fields on base.
        if (frame_ % 2) fbo_.begin(); else fbo_.begin(0, 0, 0);
        expectCamera(offscreen, false, false);
        check("public default FOV getters survive Fbo begin",
              getDefaultScreenFov() == defaultFov && getDefaultScreenFovDeg() == rad2deg(defaultFov));
        dumpCamera(scenario, "inside");
        if (nested) {
            pushMatrix(); translate(1, 2, 3); popMatrix();
            pushStyle(); popStyle();
            expectCamera(offscreen, false, false);
        }
        // A camera change within the pass must be observable there and must
        // not escape end(). Reuse the Fbo next frame to catch stale state.
        setupScreenFovWithSize(TAU / 5, 19, 17, 0.5f, 600);
        const auto changed = cameraSnapshot();
        check("public setup within Fbo changes camera immediately",
              changed.fov == TAU / 5 && changed.width == 19 && changed.height == 17 &&
              changed.context->pickable && changed.context != before.context);
        dumpCamera(scenario, "inside-changed");
        fbo_.end();
        expectCamera(before, true, true);
        check("public default FOV getters survive Fbo end",
              getDefaultScreenFov() == defaultFov && getDefaultScreenFovDeg() == rad2deg(defaultFov));
        dumpCamera(scenario, "after-end");
        check("camera handoff preserves the legacy CPU model scope", sameMatrix(getMatrix(), model));
        if (nested) {
            popStyle(); popStyle();
            popMatrix(); popMatrix();
            expectCamera(before, true, true);
            dumpCamera(scenario, "after-pop");
        }
        if (frame_ == 3) easy_.end();
        if (++frame_ == 6) {
            setDefaultScreenFov(TAU / 8);
            cameraChecksFinished = true;

        }
    }
};

void runCameraChecks(int samples = 1) {
    WindowSettings settings;
    settings.setSize(96, 64);
    settings.setHighDpi(false);
    settings.sampleCount = samples;
    runApp<CameraChecksApp>(settings);
    check("all six camera scenarios completed", cameraChecksFinished);
    check("review behavior probes and screen readback completed", reviewDrawn && reviewCaptured);
    reviewAfterFrame = {};
}
