// =============================================================================
// core/tests/inspectorTextureLifetime — tcxNodeInspector thumbnails of image
// members (Fbo) when the texture dies between the inspector's draw and ImGui's
// render.
//
// The inspector records a draw command with the texture's sokol handles and
// ImGui renders it at the end of the frame (onRender). The Fbo can be gone by
// then. TrussC defers GPU destruction until after sg_commit(), but a commit
// can land in between (another window presenting), so this test destroys the
// handles for real in that gap with internal::drainPendingGpuDestroys(), the
// call the commit makes. sokol_imgui asserts in debug builds when it is handed
// a dead view; in release builds the asserts are compiled out and sokol only
// drops the draw, so the test also checks the frame's draw commands: after the
// render, none that draws (ElemCount > 0) may reference a dead view. The guard
// in the inspector turns such a command into an empty draw.
//
// Run with --gpu-check on a display (Xvfb works on Linux).
// =============================================================================
#include <TrussC.h>
#include <tcxImGui.h>
#include <tcxNodeInspector.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"

#include <cstdio>
#include <cstring>

using namespace std;
using namespace tc;
using namespace tcx;

namespace {

int failures = 0;
int errors = 0;              // Error-level logs
int validationErrors = 0;    // sokol validation errors ("[sg]")
bool finished = false;

void check(const char* name, bool ok) {
    printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++failures;
}

// Draw commands of the frame just rendered that draw with a dead view.
// -1: no draw data.
int deadViewDraws(ImGuiContext* ctx) {
    ImGui::SetCurrentContext(ctx);
    ImDrawData* data = ImGui::GetDrawData();
    if (!data) return -1;
    int dead = 0;
    for (int i = 0; i < data->CmdListsCount; ++i) {
        const ImDrawList* list = data->CmdLists[i];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback || cmd.ElemCount == 0) continue;
            const sg_view view = simgui_texture_view_from_imtextureid(cmd.GetTexID());
            if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) ++dead;
        }
    }
    return dead;
}

struct Canvas : Node {
    Fbo fbo;
    TC_REFLECT(Canvas, Node) {
        TC_VALUE(fbo)
    }
};

// Top half red, bottom half blue (user space, y down).
void paint(Fbo& fbo) {
    fbo.begin(0, 0, 1, 1);
    setColor(1, 0, 0);
    fill();
    drawRect(0, 0, (float)fbo.getWidth(), fbo.getHeight() * 0.5f);
    fbo.end();
}

class LifetimeApp : public App {
public:
    void setup() override {
        logListener_ = getLogger().onLog.listen([](LogEventArgs& e) {
            if (e.level != LogLevel::Error) return;
            ++errors;
            if (e.message.find("[sg]") != string::npos) ++validationErrors;
            printf("  logged error: %s\n", e.message.c_str());
        });
        imguiSetup();
        ctx_ = ImGui::GetCurrentContext();
        ImGui::GetIO().IniFilename = nullptr;
        canvas_ = make_shared<Canvas>();
        canvas_->setName("canvas");
        addChild(canvas_);
        canvas_->fbo.allocate(128, 64);
        nodeinspector::NodeInspector::instance().select(canvas_.get());
        after_ = events().afterFrame.listen([this] { afterFrame(); });
    }

    void update() override {
        if (canvas_ && canvas_->fbo.isAllocated() && frame_ <= 1) paint(canvas_->fbo);
    }

    void draw() override {
        clear(0.1f, 0.1f, 0.12f);
        if (!imguiBegin()) return;
        nodeinspector::NodeInspector::instance().drawInspector();
        imguiEnd();   // the render itself happens later, in onRender

        if (frame_ == 1) {
            // The Fbo is destroyed after the inspector drew it.
            deadView_ = canvas_->fbo.getTexture().getView();
            removeChild(canvas_);
            canvas_.reset();
            internal::drainPendingGpuDestroys();
            check("frame 1: the thumbnail's view is really destroyed",
                  sg_query_view_state(deadView_) != SG_RESOURCESTATE_VALID);
        } else if (frame_ == 3) {
            // The Fbo is reallocated (resized) after the inspector drew it.
            deadView_ = canvas_->fbo.getTexture().getView();
            canvas_->fbo.allocate(64, 96);
            internal::drainPendingGpuDestroys();
            check("frame 3: the old view is really destroyed",
                  sg_query_view_state(deadView_) != SG_RESOURCESTATE_VALID);
        }
    }

private:
    void afterFrame() {
        char label[96];
        snprintf(label, sizeof(label), "frame %d: no error logged, no sokol validation error", frame_);
        check(label, errors == 0 && validationErrors == 0);
        snprintf(label, sizeof(label), "frame %d: no draw command samples a dead view", frame_);
        check(label, deadViewDraws(ctx_) == 0);
        if (frame_ == 0) {
            // The live thumbnail: red over blue (so also not upside down).
            Pixels shot;
            const bool grabbed = bool(grabScreen(shot));
            check("frame 0: screen grabbed", grabbed);
            if (grabbed) {
                long redN = 0, blueN = 0, redY = 0, blueY = 0;
                for (int y = 0; y < shot.getHeight(); ++y) {
                    for (int x = 0; x < shot.getWidth(); ++x) {
                        const Color c = shot.getColor(x, y);
                        if (c.r > 0.9f && c.g < 0.1f && c.b < 0.1f) { ++redN; redY += y; }
                        if (c.b > 0.9f && c.r < 0.1f && c.g < 0.1f) { ++blueN; blueY += y; }
                    }
                }
                printf("  thumbnail pixels: red %ld, blue %ld\n", redN, blueN);
                check("frame 0: the thumbnail is drawn (red and blue pixels)", redN > 200 && blueN > 200);
                check("frame 0: red half is above the blue half",
                      redN > 0 && blueN > 0 && (double)redY / redN < (double)blueY / blueN);
            }
        }
        if (frame_ == 1) {
            // The inspector still draws, now without the destroyed node.
            check("frame 1: selection followed the destroyed node", canvas_ == nullptr);
        }
        if (frame_ == 2) {
            // Fresh Fbo for the realloc case.
            canvas_ = make_shared<Canvas>();
            canvas_->setName("canvas2");
            addChild(canvas_);
            canvas_->fbo.allocate(128, 64);
            nodeinspector::NodeInspector::instance().select(canvas_.get());
        }
        if (frame_ >= 4) {
            finished = true;
            exitApp();
        }
        ++frame_;
    }

    shared_ptr<Canvas> canvas_;
    EventListener logListener_, after_;
    sg_view deadView_{};
    ImGuiContext* ctx_ = nullptr;
    int frame_ = 0;
};

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "--gpu-check") != 0) {
        printf("SKIP: inspectorTextureLifetime needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(640, 480);
    settings.setHighDpi(false);
    settings.setSwapInterval(0);
    return tcCoreTest::runDisplayTest<LifetimeApp>(settings, failures, [] {
        check("completed all frames", finished);
    });
}
