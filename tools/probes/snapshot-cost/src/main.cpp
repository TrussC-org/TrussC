#include <TrussC.h>
#include "native.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <limits>
#include <string>
#include <vector>
using namespace tc;
using Clock = std::chrono::steady_clock;
struct Options { int width=1920, height=1080, windows=1, samples=1, vsync=0, frames=180, warmup=60, blocks=8; } opt;
struct Block { std::vector<double> cpu, interval, scene, copy, gpu; };
static std::vector<class ProbeApp*> apps;
static int globalBlock=0;
static bool copyBlock(int b) { return (b%4==1 || b%4==2); } // ABBA, repeated
static double mean(const std::vector<double>& v) {return v.empty()?std::numeric_limits<double>::quiet_NaN():std::accumulate(v.begin(),v.end(),0.0)/v.size();}
static double p95(std::vector<double> v) {if(v.empty())return std::numeric_limits<double>::quiet_NaN();std::sort(v.begin(),v.end());return v[size_t((v.size()-1)*.95)];}
class ProbeApp : public App {
    int id, count=0, sceneFrame=0, block=0; bool copy=false, measured=false, printed=false;
    Clock::time_point started, previous; bool hadPrevious=false;
    ProbeNative* native=nullptr; EventListener renderListener, afterListener;
    std::vector<Block> stats; std::shared_ptr<Window> second;
    std::shared_ptr<ProbeApp> sub;
public:
    bool done=false;
    explicit ProbeApp(int i=0):id(i),stats(opt.blocks) {apps.push_back(this);}
    void setup() override {
        setFps(VSYNC); native=probeCreate();
        renderListener=events().onRender.listen([this]{
            auto& ctx=internal::currentWindowContext();
            ctx.swapchainTarget.flushLayers();
            ctx.swapchainTarget.endPass();
            // No app drawing follows: present's second flush/end are no-ops.
            probeEnd(native,ctx.lastSwapchain);
        });
        afterListener=events().afterFrame.listen([this]{
            if(measured)stats[block].cpu.push_back(std::chrono::duration<double,std::milli>(Clock::now()-started).count());
            collect(false);
            ++count;++sceneFrame;
            // A block changes only when BOTH windows have enough samples.
            // Extra frames in the faster window keep the same global mode.
            if(std::all_of(apps.begin(),apps.end(),[](auto* a){return a->count>=opt.frames+opt.warmup;})) {
                ++globalBlock;
                for(auto* a:apps){a->count=0;if(globalBlock>=opt.blocks)a->done=true;}
                if(globalBlock>=opt.blocks)requestExitApp();
            }
        });
        if(id==0 && opt.windows==2) {
            WindowSettings s; s.width=opt.width;s.height=opt.height;s.sampleCount=opt.samples;
            s.highDpi=true;s.pixelPerfect=true;s.swapInterval=opt.vsync;s.title="snapshotCost secondary";
            second=createWindow(s);
            if(!second) {std::fprintf(stderr,"ERROR: two windows unsupported by this backend at a711686f; no two-window measurement\n"); std::exit(2);}
            sub=std::make_shared<ProbeApp>(1);second->setApp(sub);
        }
    }
    void draw() override {
        started=Clock::now();
        block=std::min(globalBlock,opt.blocks-1);
        copy=copyBlock(block);measured=!done && count>=opt.warmup && count<opt.warmup+opt.frames;
        if(measured && hadPrevious)stats[block].interval.push_back(std::chrono::duration<double,std::milli>(started-previous).count());
        previous=started;hadPrevious=true;
        probeBegin(native,block,copy,measured);
        const float w=getWindowWidth(),h=getWindowHeight();
        clear(.035f,.055f,.085f);
        // Deterministic animated exhibition-like scene: 96 colored panels,
        // translucent moving circles and text, all through TrussC's renderer.
        for(int y=0;y<8;++y)for(int x=0;x<12;++x) {
            setColor(.1f+.045f*x,.13f+.07f*y,.35f+.02f*((x+y)%6));
            drawRect(x*w/12+3,y*h/8+3,w/12-6,h/8-6);
        }
        const float t=float(sceneFrame)*.021f;
        for(int i=0;i<32;++i) {
            setColor(.85f,.45f+.2f*std::sin(float(i)),.2f,.55f);
            drawCircle(w*(.5f+.43f*std::sin(t+i*.7f)),h*(.5f+.4f*std::cos(t*.7f+i)),h*.035f);
        }
        setColor(1);drawBitmapString("TrussC snapshot-cost / same scene in both modes",24,32);
        if(sceneFrame==0)std::printf("WINDOW,id=%d,physical=%dx%d,logical=%dx%d,samples=%d,format=%d\n",id,
            internal::currentTarget().width,internal::currentTarget().height,int(w),int(h),opt.samples,int(internal::currentTarget().colorFormat));
    }
    void collect(bool drain) {
        std::vector<GpuSample> rows;probePoll(native,rows,drain);
        for(auto& r:rows){auto& s=stats.at(r.block);if(r.sceneMs>=0)s.scene.push_back(r.sceneMs);if(r.copyMs>=0)s.copy.push_back(r.copyMs);if(r.totalMs>=0)s.gpu.push_back(r.totalMs);}
    }
    void report() {
        if(printed)return;printed=true; collect(true);
        std::printf("window,block,snapshot,cpu_n,gpu_n,frame_interval_ms,fps,cpu_submit_ms,cpu_p95_ms,scene_gpu_ms,copy_gpu_ms,total_gpu_ms,gpu_p95_ms\n");
        for(int b=0;b<opt.blocks;++b){auto& s=stats[b];std::printf("%d,%d,%d,%zu,%zu,%.6f,%.3f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",id,b,copyBlock(b),s.cpu.size(),s.gpu.size(),mean(s.interval),1000/mean(s.interval),mean(s.cpu),p95(s.cpu),mean(s.scene),mean(s.copy),mean(s.gpu),p95(s.gpu));}
        probeDestroy(native);native=nullptr;
    }
    void exit() override {if(id==0)for(auto* a:apps)a->report();}
};
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];if(i+1>=argc){std::fprintf(stderr,"missing option value\n");return 2;}int n=std::atoi(argv[++i]);
        if(a=="--width")opt.width=n;else if(a=="--height")opt.height=n;else if(a=="--windows")opt.windows=n;else if(a=="--samples")opt.samples=n;else if(a=="--vsync")opt.vsync=n;else if(a=="--frames")opt.frames=n;else if(a=="--warmup")opt.warmup=n;else if(a=="--blocks")opt.blocks=n;else return 2;
    }
    if(opt.width<=0||opt.height<=0||opt.windows<1||opt.windows>2||opt.frames<=0||opt.warmup<0||opt.blocks<4||(opt.samples!=1&&opt.samples!=4))return 2;
    std::printf("CONFIG,requested=%dx%d,windows=%d,samples=%d,vsync=%d,frames=%d,warmup=%d,blocks=%d\n",opt.width,opt.height,opt.windows,opt.samples,opt.vsync,opt.frames,opt.warmup,opt.blocks);
    WindowSettings s;s.width=opt.width;s.height=opt.height;s.highDpi=true;s.pixelPerfect=true;s.sampleCount=opt.samples;s.swapInterval=opt.vsync;s.title="snapshotCost";
    return TC_RUN_APP(ProbeApp,s);
}
