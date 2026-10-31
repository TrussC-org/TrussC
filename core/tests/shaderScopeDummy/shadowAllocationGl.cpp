// Real native storage failure without exhausting memory or requiring an X server.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <TrussC.h>
#include <cstdio>
#include <cstdlib>
using namespace tc;
namespace {
bool failStorage = false;
int injected = 0;
}
extern "C" void glTexStorage3D(GLenum target, GLsizei levels, GLenum format,
        GLsizei width, GLsizei height, GLsizei depth) {
    static auto call = reinterpret_cast<decltype(&glTexStorage3D)>(dlsym(RTLD_NEXT, "glTexStorage3D"));
    if (!call) std::abort();
    if (failStorage) { failStorage = false; ++injected; width = 0; }
    call(target, levels, format, width, height, depth);
}
int main() {
#if TC_CORE_SOKOL_ASSERTS
    std::puts("SKIP: interposed storage error requires a core built with NDEBUG");
    return 0;
#endif
    auto getPlatform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getPlatform) return 1;
    EGLDisplay display = getPlatform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API)) return 1;
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig config; EGLint count;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1) return 1;
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return 1;
    sg_desc desc = {};
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    desc.environment.defaults.sample_count = 1;
    sg_setup(&desc);
    bool ok = sg_isvalid();
    int errors = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("could not create shadow resource") != std::string::npos) ++errors;
    });
    {
        internal::WindowContext owner, foreign;
        glEnable(0); // Pending application errors are drained once before creation.
        ok &= internal::ensureShadowStorage(owner, 64) && internal::ensureShadowStorage(foreign, 64);
        const auto previous = owner.shadow.storage, other = foreign.shadow.storage;
        owner.shadow.count = 2; owner.shadow.current = 1;
        for (int attempt = 0; attempt < 3; ++attempt) {
            failStorage = true;
            ok &= !internal::ensureShadowStorage(owner, 128);
            ok &= !failStorage && owner.shadow.storage == previous && foreign.shadow.storage == other &&
                  owner.shadow.count == 2 && owner.shadow.current == 1 && glGetError() == GL_NO_ERROR;
            GLint width = 0;
            glBindTexture(GL_TEXTURE_2D_ARRAY, sg_gl_query_image_info(previous->colorImage).tex[0]);
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &width);
            ok &= width == 64 && sg_query_image_state(previous->colorImage) == SG_RESOURCESTATE_VALID;
        }
        ok &= internal::ensureShadowStorage(owner, 128) && owner.shadow.storage != previous &&
              owner.shadow.storage->resolution == 128 && foreign.shadow.storage == other;
    }
    ok &= injected == 3 && errors == 3;
    std::printf("SHADOW NATIVE surfaceless storage_failure/previous_generation/foreign=%d injected=%d errors=%d\n", ok, injected, errors);
    sg_shutdown();
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return ok ? 0 : 1;
}
