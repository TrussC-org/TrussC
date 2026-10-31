#include <TrussC.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <thread>
#include <atomic>
using namespace tc;
std::atomic<int> frozenExitListeners{0};
CoreEvents* source;
int failures=0;
void check(const char* label,bool ok) { std::printf("gpu-freeze: %s: %s\n",label,ok?"PASS":"FAIL");failures+=!ok; }
struct Probe : App {
    EventListener listener;
    Mesh points;
    void setup() override {
        source = &events();
        points.setMode(PrimitiveMode::Points);
        points.addVertex({0,0,0});
        points.uploadPointsToGpu();
        check("point buffer uploaded before freeze",points.getGpuPointBuffer().id!=0); // buffer already exists; lazy pipeline still uninitialized

        listener = source->exit.listen([] {
            if (internal::sessionEndExit() && !isMainThread()) ++frozenExitListeners;
        });
    }
    void update() override {
        internal::EntryStackGuard interruptedDraw(internal::AppEntry::Draw);
        internal::endRuntimeForSession();
        std::printf("frozen worker exit listeners=%d\n", frozenExitListeners.load());
        std::fflush(stdout);
        std::_Exit(failures?91:0);
    }
    void exit() override {
        std::thread worker([] {
            source->exit.notify();
            Pixels pixels;
            check("worker captureWindow admission",!captureWindow(pixels) && !pixels.isAllocated());
        });
        worker.join();
        check("App-owned worker exit listener frozen",frozenExitListeners==0);
        Pixels capture; bool captured=grabScreen(capture);
        check("grabScreen inside Draw-interrupted exit",!captured && !capture.isAllocated());
        const auto before=sg_query_stats();
        points.draw();
        const auto after=sg_query_stats();
        check("uploaded Mesh lazy point draw creates no resources",before.total.shaders.allocated==after.total.shaders.allocated && before.total.buffers.allocated==after.total.buffers.allocated);
        std::printf("frozen grabScreen=%d bytes=%zu\n",captured,capture.getTotalBytes());
        IesProfile profile;
        bool ok = profile.loadFromString("TILT=NONE\n1 1 1 1 1 1 1 1 1 1 1 1 1\n0 0 21\n");
        check("IES loadFromString frozen",!ok && !profile.isLoaded() && !profile.getView().id && !profile.getSampler().id);
        std::printf("frozen=%d IES load=%d loaded=%d view=%u sampler=%u\n",
          bool(internal::sessionEndExit()), ok, profile.isLoaded(), profile.getView().id, profile.getSampler().id);
    }
};
int main() {
    getMainThreadId(); internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint major=0, minor=0, count=0;
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
    EGLConfig config{};
    const EGLint version[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
    if (!eglInitialize(display,&major,&minor) || !eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display,attrs,&config,1,&count) || !count) return 2;
    auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);
    const EGLint size[]={EGL_WIDTH,4,EGL_HEIGHT,4,EGL_NONE};
    auto surface=eglCreatePbufferSurface(display,config,size);
    if (!eglMakeCurrent(display,surface,surface,context)) return 3;
    sg_desc gd{}; sg_setup(&gd); sg_enable_stats(); sgl_desc_t sd{}; sgl_setup(&sd);
    headless::active=true;
    auto window=std::make_shared<Window>(); internal::WindowRequestAccess::bindForTests(*window,901);
    internal::retainRuntimeWindow(window); window->setApp<Probe>(); internal::applyPendingAppForTests(*window);
    internal::currentWindowCtx()=&window->context();
    window->context().fbWidth=4; window->context().fbHeight=4;
    internal::RuntimeActivityScope interrupted;
    window->tickTree();
    return 4;
}
