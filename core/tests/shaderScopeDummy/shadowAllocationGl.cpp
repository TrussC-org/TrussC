// A separate native executable: GL interposition must not affect the dummy or
// the combined runner. No large allocation is needed to inject driver errors.
#if defined(__linux__) && defined(SOKOL_GLCORE)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <dlfcn.h>
#include <TrussC.h>
#include "../common/tcDisplayTest.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace tc;
namespace {
enum class Fault { None, TextureName, ColorStorage, DepthStorage, SamplingView, SamplerName, SamplerStorage };
Fault fault = Fault::None;
int injected = 0, failures = 0;
bool completed = false;
template<class Function> Function native(const char* name) {
    auto function = reinterpret_cast<Function>(dlsym(RTLD_NEXT,name));
    if (!function) std::abort();
    return function;
}
bool take(Fault wanted) {
    if (fault != wanted) return false;
    fault = Fault::None; ++injected; return true;
}
void check(const char* label, bool ok) {
    std::printf("shadowAllocationGl %s: %s\n",label,ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
}
extern "C" void glGenTextures(GLsizei count, GLuint* names) {
    static auto call = native<decltype(&glGenTextures)>("glGenTextures");
    if (take(Fault::TextureName)) { call(-1,names); names[0] = 0; }
    else call(count,names);
}
extern "C" void glTexStorage3D(GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height, GLsizei depth) {
    static auto call = native<decltype(&glTexStorage3D)>("glTexStorage3D");
    call(target,levels,format,take(Fault::ColorStorage) ? 0 : width,height,depth);
}
extern "C" void glTexStorage2D(GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height) {
    static auto call = native<decltype(&glTexStorage2D)>("glTexStorage2D");
    call(target,levels,format,take(Fault::DepthStorage) ? 0 : width,height);
}
extern "C" void glTextureView(GLuint texture, GLenum target, GLuint original, GLenum format,
        GLuint minLevel, GLuint levels, GLuint minLayer, GLuint layers) {
    static auto call = native<decltype(&glTextureView)>("glTextureView");
    call(texture,take(Fault::SamplingView) ? 0 : target,original,format,minLevel,levels,minLayer,layers);
}
extern "C" void glGenSamplers(GLsizei count, GLuint* names) {
    static auto call = native<decltype(&glGenSamplers)>("glGenSamplers");
    if (take(Fault::SamplerName)) { call(-1,names); names[0] = 0; }
    else call(count,names);
}
extern "C" void glSamplerParameteri(GLuint sampler, GLenum parameter, GLint value) {
    static auto call = native<decltype(&glSamplerParameteri)>("glSamplerParameteri");
    call(sampler,take(Fault::SamplerStorage) ? 0 : parameter,value);
}
namespace {
class AllocationApp : public App {
    static std::vector<float> contents(const internal::ShadowStorage& storage) {
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&previous);
        glBindTexture(GL_TEXTURE_2D_ARRAY,sg_gl_query_image_info(storage.colorImage).tex[0]);
        std::vector<float> pixels(size_t(storage.resolution)*storage.resolution*internal::maxShadowLights);
        glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_RED,GL_FLOAT,pixels.data());
        glBindTexture(GL_TEXTURE_2D_ARRAY,GLuint(previous));
        return pixels;
    }
public:
    void setup() override {
        auto& a = internal::currentWindowContext();
        internal::WindowContext b;
        int pendingWarnings = 0;
        auto pendingListener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning &&
                e.message.find("0x0500") != std::string::npos &&
                e.message.find("left pending by earlier GL code") != std::string::npos) ++pendingWarnings;
        });
        glEnable(0); // A stale user error must not be attributed to allocation.
        check("allocate two owner sets",internal::ensureShadowStorage(a,64) && internal::ensureShadowStorage(b,128));
        check("pre-existing creation error reported separately",a.shadow.storage && pendingWarnings == 1 && glGetError() == GL_NO_ERROR);
        glEnable(0);
        check("reuse does not consume user errors",internal::ensureShadowStorage(a,64) && glGetError() == GL_INVALID_ENUM && pendingWarnings == 1);
        {
            // A listener that leaves its own GL error must not restart the
            // drain: growth completes, with at most one Warning per creation.
            bool raising = true;
            auto raiser = getLogger().onLog.listen([&](LogEventArgs& e) {
                if (raising && e.level == LogLevel::Warning &&
                    e.message.find("left pending by earlier GL code") != std::string::npos) glEnable(0);
            });
            glEnable(0);
            const int before = pendingWarnings;
            check("GL-emitting log listener does not stall growth",internal::ensureShadowStorage(a,96) && a.shadow.storage &&
                  a.shadow.storage->resolution == 96 && pendingWarnings > before && pendingWarnings - before <= 16);
            raising = false;
            check("listener's own GL error stays pending for the application",glGetError() == GL_INVALID_ENUM);
            while (glGetError() != GL_NO_ERROR) {}
            pendingWarnings = before; // later checks count from the stale-error case above
        }
        auto foreign = b.shadow.storage;
        for (int layer = 0; layer < internal::maxShadowLights; ++layer) {
            internal::RenderTarget target;
            target.passKind = internal::TargetPassKind::Fixed;
            target.isFbo = true; target.width = target.height = 128;
            target.colorFormat = SG_PIXELFORMAT_R32F; target.sampleCount = 1;
            target.depthFormat = SG_PIXELFORMAT_DEPTH;
            target.binding.attachments.colors[0] = foreign->colorAttachments[layer];
            target.binding.attachments.depth_stencil = foreign->depthAttachment;
            target.clearAction.colors[0].load_action = SG_LOADACTION_CLEAR;
            target.clearAction.colors[0].clear_value = {float(42+layer),0,0,1};
            target.clearAction.depth.load_action = SG_LOADACTION_CLEAR;
            target.clearAction.depth.clear_value = 1;
            internal::TargetPassScope scope(target);
            check("initialize foreign layer",scope.active());
        }
        const auto before = contents(*foreign);
        check("foreign known R32F contents", before.front() == 42 && before.back() == 45);
        int reported = 0, attempts = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error && e.message.find("shadow use discarded") != std::string::npos) ++reported;
        });
        for (auto stage : {Fault::TextureName,Fault::ColorStorage,Fault::DepthStorage,
                           Fault::SamplingView,Fault::SamplerName,Fault::SamplerStorage}) {
            // The isolated debug backend keeps descriptor validation enabled,
            // with only its GL error assertions disabled so the host can observe
            // the injected errors. Zero-name Sokol assertions remain unchanged.
            if (std::getenv("TC_SHADOW_DEBUG_BACKEND") &&
                (stage == Fault::TextureName || stage == Fault::SamplerName)) continue;
            if (stage == Fault::SamplingView && !sg_query_features().gl_texture_views) {
                std::puts("SKIP: native texture views unsupported"); continue;
            }
            const auto oldImage = a.shadow.storage->colorImage;
            a.shadow.count = 2;
            fault = stage; ++attempts;
            check("driver failure rejects generation", !internal::ensureShadowStorage(a,256));
            check("failure discards only this owner's shadow use", fault == Fault::None &&
                !a.shadow.storage && a.shadow.count == 0 && b.shadow.storage == foreign &&
                sg_query_image_state(foreign->colorImage) == SG_RESOURCESTATE_VALID);
            check("driver error consumed coherently",glGetError() == GL_NO_ERROR);
            check("foreign image bytes unchanged",contents(*foreign) == before);
            internal::endGpuFrame();
            check("old failed-owner maps retire",sg_query_image_state(oldImage) == SG_RESOURCESTATE_INVALID);
            check("allocation recovers after failure",internal::ensureShadowStorage(a,64));
        }
        glEnable(0);
        check("pre-existing growth error reported separately",internal::ensureShadowStorage(a,128) && pendingWarnings == 2 && glGetError() == GL_NO_ERROR);
        glEnable(0);
        check("invalid size leaves user error pending",!internal::ensureShadowStorage(a,0) && pendingWarnings == 2 && glGetError() == GL_INVALID_ENUM);
        check("every injected driver failure reported",reported == attempts + 1 && injected == attempts);
        completed = true;
        exitApp();
    }
};
}
int main(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1],"--gpu-check")) return 2;
    WindowSettings settings; settings.setSize(64,64); settings.setHighDpi(false); settings.sampleCount = 1;
    return tcCoreTest::runDisplayTest<AllocationApp>(settings,failures,[] { check("native allocation cases completed",completed); });
}
#else
#include <cstdio>
int main() { std::puts("SKIP: shadowAllocationGl requires Linux GL"); return 0; }
#endif
