// #713 Q3: same UI/workload, with uploads before the pass or at the draw point.
#include <tcxImGui.h>
#include "tcImGuiTarget.h"
#include "../../../../core/tests/common/tcDisplayTest.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
using namespace tc;
namespace {
using Clock = std::chrono::steady_clock;
int failures = 0;
bool completed = false;
class CostApp : public App {
    Fbo fbo_;
    simgui_tc_context context_ = {};
    EventListener after_, log_;
    int errors_ = 0;
    Clock::time_point start_;
    std::vector<double> samples_, completedSamples_;
    Pixels pixels_;
    int arm_ = 0, frame_ = 0;
    static constexpr int warmup = 30, measured = 120;
    bool useFbo() const { return arm_ >= 2; }
    bool breakPass() const { return (arm_ % 2) != 0; }
    void ui() {
        simgui_tc_set_context(context_);
        simgui_frame_desc_t desc = {};
        desc.width = useFbo() ? fbo_.getWidth() : getFramebufferWidth();
        desc.height = useFbo() ? fbo_.getHeight() : getFramebufferHeight();
        desc.delta_time = 1.0/60; desc.dpi_scale = useFbo() ? 1 : getDpiScale();
        if (frame_ == 0) {
            if (!tcx::imgui::detail::beginTargetFrame(context_,desc)) ++failures;
        } else simgui_new_frame(&desc);
        ImGui::GetForegroundDrawList()->AddRectFilled({32,32},{96,96},IM_COL32(255,0,0,255));
    }
public:
    void setup() override {
        sg_enable_stats();
        log_ = getLogger().onLog.listen([this](LogEventArgs& event) {
            if (event.level == LogLevel::Error) ++errors_;
        });
        fbo_.allocate(256,256);
        pixels_.allocate(256,256,4);
        std::printf("PASS_BREAK window_px=%dx%d fbo_px=%dx%d\n",getFramebufferWidth(),getFramebufferHeight(),fbo_.getWidth(),fbo_.getHeight());
        simgui_desc_t desc = {}; desc.logger.func = internal::sokolLog;
        desc.color_format = internal::currentTarget().colorFormat;
        desc.depth_format = internal::currentTarget().depthFormat;
        desc.sample_count = internal::currentTarget().sampleCount;
        context_ = simgui_tc_make_context(&desc);
        simgui_tc_set_context(context_); ImGui::GetIO().IniFilename = nullptr;
        after_ = events().afterFrame.listen([this] {
            const double elapsed = std::chrono::duration<double,std::milli>(Clock::now()-start_).count();
            const auto passes = sg_query_stats().prev_frame.num_passes;
            // Complete the target on the GPU too: asynchronous submission
            // alone would omit tile store/load work on Metal and GLES.
            const bool captured = useFbo() ? fbo_.readPixels(pixels_.getData()) : bool(grabScreen(pixels_));
            if (!captured) ++failures;
            const double completedElapsed = std::chrono::duration<double,std::milli>(Clock::now()-start_).count();
            if (frame_ >= warmup) { samples_.push_back(elapsed); completedSamples_.push_back(completedElapsed); }
            if (++frame_ != warmup + measured) return;
            std::sort(samples_.begin(),samples_.end());
            std::sort(completedSamples_.begin(),completedSamples_.end());
            const double completedAverage = std::accumulate(completedSamples_.begin(),completedSamples_.end(),0.0)/completedSamples_.size();
            const double average = std::accumulate(samples_.begin(),samples_.end(),0.0)/samples_.size();
            std::printf("PASS_BREAK target=%s break=%s samples=%zu frame_ms_mean=%.6f p50=%.6f p95=%.6f completed_ms_mean=%.6f completed_p50=%.6f completed_p95=%.6f passes=%u\n",
                useFbo()?"fbo":"window",breakPass()?"yes":"no",samples_.size(),average,
                samples_[samples_.size()/2],samples_[samples_.size()*95/100],completedAverage,
                completedSamples_[completedSamples_.size()/2],completedSamples_[completedSamples_.size()*95/100],passes);
            frame_=0; samples_.clear(); completedSamples_.clear();
            if (++arm_ == 4) { completed=true; exitApp(); }
        });
    }
    void draw() override {
        start_=Clock::now();
        // Baseline flushes before starting either target. The measured arm
        // moves exactly that upload to the ImGui draw point inside the pass.
        internal::parkWindowPass();
        if (frame_ != 0) {
            ui();
            if (!breakPass()) simgui_flush();
        }
        clear(0,0,0);
        if (useFbo()) fbo_.begin(0,0,0,1);
        internal::ensureCurrentPass();
        setColor(0,0,1); drawRect(16,16,64,64);
        auto& target=internal::currentTarget(); target.flushLayers();
        if (frame_ == 0) {
            // Prime the production target pipeline once per arm, outside the
            // samples. Subsequent raw flush/draw calls reuse that same recipe.
            ui(); tcx::imgui::detail::renderTargetFrame(context_);
        } else {
            if (breakPass()) {
                internal::parkWindowPass(); simgui_flush(); internal::ensureCurrentPass();
            }
            simgui_draw();
        }
        setColor(1,1,0); drawRect(64,64,48,48);
        if (useFbo()) { fbo_.end(); setColor(1); fbo_.draw(0,0); }
    }
    void exit() override {
        tcx::imgui::detail::destroyTargetContext(context_);
        if (!completed || errors_) ++failures;
        std::printf("PASS_BREAK completed=%d errors=%d failures=%d\n",completed,errors_,failures);
        exitApp(failures?1:0);
    }
};
}
int main(int argc,char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--measure") {
        std::puts("SKIP: pass-break cost needs a display; use --measure (Linux: Xvfb/Mesa)"); return 0;
    }
    WindowSettings settings; settings.setSize(256,256); settings.setSwapInterval(0);
    return tcCoreTest::runDisplayTest<CostApp>(settings,failures);
}
