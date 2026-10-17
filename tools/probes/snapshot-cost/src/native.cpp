// Probe-only Sokol implementation and access to the native pre-commit buffer.
// Nothing in the tracked framework is edited or instrumented.
#if defined(__APPLE__)
#include "../../../../core/platform/mac/sokol_impl.mm"
#elif defined(_WIN32)
#include "../../../../core/platform/win/sokol_impl.cpp"
#else
#include "../../../../core/platform/linux/sokol_impl.cpp"
#endif
#undef SOKOL_IMPL
#undef SOKOL_GFX_IMPL
#include "native.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>
#include <string>
static void fail(const char* s) {std::fprintf(stderr,"PROBE ERROR: %s\n",s);std::exit(3);}
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
#if defined(SOKOL_GLES3)
#include <EGL/egl.h>
#endif
struct TimerApi {
    void (*gen)(GLsizei,GLuint*)=nullptr;void (*del)(GLsizei,const GLuint*)=nullptr;
    void (*begin)(GLenum,GLuint)=nullptr;void (*end)(GLenum)=nullptr;
    void (*getiv)(GLuint,GLenum,GLint*)=nullptr;void (*get64)(GLuint,GLenum,uint64_t*)=nullptr;
    bool available=false;
} timer;
static void* proc(const char* s) {
#if defined(SOKOL_GLES3)
    return reinterpret_cast<void*>(eglGetProcAddress(s));
#else
    return reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(s)));
#endif
}
struct Query {GLuint q[2]{};int block=0;bool copy=false;};
struct ProbeNative {int width=0,height=0,bpp=4;GLint format=GL_RGBA8;GLuint texture=0,fbo=0;bool copy=false,measured=false;Query cur;std::deque<Query> pending;};
ProbeNative* probeCreate() {
    std::printf("GPU,vendor=%s,renderer=%s,version=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
#if defined(SOKOL_GLES3)
    bool supported=false;GLint n=0;glGetIntegerv(GL_NUM_EXTENSIONS,&n);
    for(GLint i=0;i<n;++i)if(!std::strcmp((const char*)glGetStringi(GL_EXTENSIONS,i),"GL_EXT_disjoint_timer_query"))supported=true;
    const char* suffix="EXT";
#else
    bool supported=true;const char* suffix="";
#endif
#define LOAD(member,name) timer.member=reinterpret_cast<decltype(timer.member)>(proc((std::string(name)+suffix).c_str()))
    LOAD(gen,"glGenQueries");LOAD(del,"glDeleteQueries");LOAD(begin,"glBeginQuery");LOAD(end,"glEndQuery");LOAD(getiv,"glGetQueryObjectiv");LOAD(get64,"glGetQueryObjectui64v");
#undef LOAD
    timer.available=supported&&timer.gen&&timer.del&&timer.begin&&timer.end&&timer.getiv&&timer.get64;
    std::printf("TIMER,available=%d,asynchronous=1\n",timer.available);
    return new ProbeNative;
}
void probeBegin(ProbeNative* p,int block,bool copy,bool measured) {
    p->copy=copy;p->measured=measured&&timer.available;p->cur={};p->cur.block=block;p->cur.copy=copy;
    if(p->measured){timer.gen(2,p->cur.q);timer.begin(0x88BF,p->cur.q[0]);}
}
void probeEnd(ProbeNative* p,const sg_swapchain& sc) {
    if(p->measured)timer.end(0x88BF); // scene including end-pass/resolve
    if(p->copy) {
        if(!p->texture||p->width!=sc.width||p->height!=sc.height) {
            if(p->texture)glDeleteTextures(1,&p->texture);if(p->fbo)glDeleteFramebuffers(1,&p->fbo);
            p->width=sc.width;p->height=sc.height;
#if defined(SOKOL_GLES3)
            GLint previousRead,alpha=0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,sc.gl.framebuffer);
            glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,sc.gl.framebuffer?GL_COLOR_ATTACHMENT0:GL_BACK,GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,&alpha);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead);
            if(glGetError()!=GL_NO_ERROR)fail("source attachment format query");
            p->format=alpha?GL_RGBA8:GL_RGB8;p->bpp=alpha?4:3;
            std::printf("SOURCE,GLES_alpha_bits=%d,retained=%s\n",alpha,alpha?"RGBA8":"RGB8");
#endif
            GLint tex;glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex);
            glGenTextures(1,&p->texture);glBindTexture(GL_TEXTURE_2D,p->texture);
            glTexImage2D(GL_TEXTURE_2D,0,p->format,sc.width,sc.height,0,p->bpp==4?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,nullptr);glBindTexture(GL_TEXTURE_2D,tex);
            glGenFramebuffers(1,&p->fbo);
            std::printf("SNAPSHOT,payload_bytes=%llu,physical=%dx%d,format=%s\n",(unsigned long long)sc.width*sc.height*p->bpp,sc.width,sc.height,p->bpp==4?"RGBA8":"RGB8");
        }
        GLint read,draw;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,sc.gl.framebuffer);
        GLint readBuffer;glGetIntegerv(GL_READ_BUFFER,&readBuffer);
        glReadBuffer(sc.gl.framebuffer?GL_COLOR_ATTACHMENT0:GL_BACK);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,p->fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p->texture,0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)fail("snapshot FBO incomplete");
        if(p->measured)timer.begin(0x88BF,p->cur.q[1]);
        // Mirror #858/#870 for this *probe-only* swapchain resolve.
        if(sc.sample_count>1 && _sg.gl.v3d_resolve_flush)glFlush();
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0,0,sc.width,sc.height,0,0,sc.width,sc.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        if(p->measured)timer.end(0x88BF);
        glReadBuffer(readBuffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        if(GLenum err=glGetError();err!=GL_NO_ERROR){std::fprintf(stderr,"snapshot GL error=0x%x\n",err);fail("snapshot GL error");}
        sg_reset_state_cache();
    }
    if(p->measured)p->pending.push_back(p->cur);
}
void probePoll(ProbeNative* p,std::vector<GpuSample>& out,bool drain) {
    if(drain)glFinish(); // shutdown only; never in measured frames
#if defined(SOKOL_GLES3)
    GLint disjoint=0;if(timer.available)glGetIntegerv(0x8FBB,&disjoint);
    if(disjoint){std::fprintf(stderr,"TIMER DISJOINT: discarded pending samples\n");for(auto q:p->pending)timer.del(2,q.q);p->pending.clear();return;}
#endif
    while(!p->pending.empty()) {
        auto q=p->pending.front();GLint ready=0;timer.getiv(q.q[q.copy?1:0],0x8867,&ready);
        if(!ready)break;uint64_t a=0,b=0;timer.get64(q.q[0],0x8866,&a);if(q.copy)timer.get64(q.q[1],0x8866,&b);
        out.push_back({q.block,q.copy,a/1e6,b/1e6,(a+b)/1e6});timer.del(2,q.q);p->pending.pop_front();
    }
}
void probeDestroy(ProbeNative* p){if(p->texture)glDeleteTextures(1,&p->texture);if(p->fbo)glDeleteFramebuffers(1,&p->fbo);delete p;}
#elif defined(SOKOL_METAL)
struct Completion {std::mutex mutex;std::vector<GpuSample> samples;};
struct ProbeNative {id<MTLTexture> texture=nil;bool copy=false,measured=false;int block=0;std::shared_ptr<Completion> completion=std::make_shared<Completion>();id<MTLCommandBuffer> last=nil;};
ProbeNative* probeCreate(){std::printf("GPU,Metal,%s\n",[[_sg.mtl.device name] UTF8String]);return new ProbeNative;}
void probeBegin(ProbeNative* p,int b,bool copy,bool m){p->block=b;p->copy=copy;p->measured=m;}
void probeEnd(ProbeNative* p,const sg_swapchain& sc) {
    id<CAMetalDrawable> drawable=(__bridge id<CAMetalDrawable>)sc.metal.current_drawable;
    if(!drawable || !_sg.mtl.cmd_buffer)fail("Metal drawable/renderer buffer missing");
    id<MTLTexture> src=drawable.texture;
    if(p->copy) {
        if(!p->texture || p->texture.width!=src.width || p->texture.height!=src.height || p->texture.pixelFormat!=src.pixelFormat) {
            MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat width:src.width height:src.height mipmapped:NO];
            d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead;
            p->texture=[_sg.mtl.device newTextureWithDescriptor:d];if(!p->texture)fail("Metal snapshot allocation");
            std::printf("SNAPSHOT,bytes=%llu,physical=%lux%lu,Metal_format=%lu\n",(unsigned long long)src.width*src.height*4,(unsigned long)src.width,(unsigned long)src.height,(unsigned long)src.pixelFormat);
        }
        id<MTLBlitCommandEncoder> blit=[_sg.mtl.cmd_buffer blitCommandEncoder];
        [blit copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(src.width,src.height,1) toTexture:p->texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit endEncoding];
    }
    p->last=_sg.mtl.cmd_buffer;
    if(p->measured) {
        auto output=p->completion;int b=p->block;bool copy=p->copy;
        [_sg.mtl.cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> cb){
            if(cb.status!=MTLCommandBufferStatusCompleted)fail("Metal command buffer failed");
            double ms=(cb.GPUEndTime-cb.GPUStartTime)*1000;
            std::lock_guard<std::mutex> guard(output->mutex);output->samples.push_back({b,copy,-1,-1,ms});
        }];
    }
}
void probePoll(ProbeNative* p,std::vector<GpuSample>& out,bool drain){if(drain&&p->last)[p->last waitUntilCompleted];std::lock_guard<std::mutex> guard(p->completion->mutex);out.swap(p->completion->samples);}
void probeDestroy(ProbeNative* p){delete p;}
#elif defined(SOKOL_D3D11)
struct Query {ID3D11Query* time[3]{};ID3D11Query* disjoint=nullptr;int block=0;bool copy=false;};
struct ProbeNative {ID3D11Texture2D* texture=nullptr;D3D11_TEXTURE2D_DESC desc{};bool copy=false,measured=false;Query cur;std::deque<Query> pending;};
static void release(Query& q){for(auto* t:q.time)if(t)t->Release();if(q.disjoint)q.disjoint->Release();}
ProbeNative* probeCreate(){std::printf("GPU,D3D11,timestamp-disjoint queries\n");return new ProbeNative;}
void probeBegin(ProbeNative* p,int b,bool copy,bool m) {
    p->copy=copy;p->measured=m;p->cur={};p->cur.block=b;p->cur.copy=copy;
    if(m){D3D11_QUERY_DESC d={D3D11_QUERY_TIMESTAMP_DISJOINT,0};if(FAILED(_sg.d3d11.dev->CreateQuery(&d,&p->cur.disjoint)))fail("disjoint query");
        d.Query=D3D11_QUERY_TIMESTAMP;for(auto& t:p->cur.time)if(FAILED(_sg.d3d11.dev->CreateQuery(&d,&t)))fail("timestamp query");
        _sg.d3d11.ctx->Begin(p->cur.disjoint);_sg.d3d11.ctx->End(p->cur.time[0]);}
}
void probeEnd(ProbeNative* p,const sg_swapchain& sc) {
    if(p->measured)_sg.d3d11.ctx->End(p->cur.time[1]);
    if(p->copy) {
        auto* view=(ID3D11RenderTargetView*)(sc.d3d11.resolve_view?sc.d3d11.resolve_view:sc.d3d11.render_view);
        ID3D11Resource* res=nullptr;view->GetResource(&res);ID3D11Texture2D* src=nullptr;
        HRESULT hr=res->QueryInterface(__uuidof(ID3D11Texture2D),(void**)&src);res->Release();if(FAILED(hr))fail("swapchain texture");
        D3D11_TEXTURE2D_DESC d;src->GetDesc(&d);if(d.SampleDesc.Count!=1)fail("expected resolved D3D11 backbuffer");
        if(!p->texture||d.Width!=p->desc.Width||d.Height!=p->desc.Height||d.Format!=p->desc.Format) {
            if(p->texture)p->texture->Release();d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;d.MiscFlags=0;
            if(FAILED(_sg.d3d11.dev->CreateTexture2D(&d,nullptr,&p->texture)))fail("D3D11 snapshot allocation");p->desc=d;
            std::printf("SNAPSHOT,bytes=%llu,physical=%ux%u,DXGI_format=%u\n",(unsigned long long)d.Width*d.Height*4,d.Width,d.Height,unsigned(d.Format));
        }
        _sg.d3d11.ctx->CopyResource(p->texture,src);src->Release();
    }
    if(p->measured){_sg.d3d11.ctx->End(p->cur.time[2]);_sg.d3d11.ctx->End(p->cur.disjoint);p->pending.push_back(p->cur);}
}
void probePoll(ProbeNative* p,std::vector<GpuSample>& out,bool drain) {
    if(drain)_sg.d3d11.ctx->Flush();
    while(!p->pending.empty()) {
        auto q=p->pending.front();D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
        HRESULT hr=_sg.d3d11.ctx->GetData(q.disjoint,&d,sizeof(d),drain?0:D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr==S_FALSE){if(!drain)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        if(FAILED(hr))fail("D3D11 timer read");UINT64 t[3]{};
        bool ready=true;
        for(int i=0;i<3;++i){
            while((hr=_sg.d3d11.ctx->GetData(q.time[i],&t[i],sizeof(UINT64),drain?0:D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE){if(!drain){ready=false;break;}std::this_thread::yield();}
            if(FAILED(hr))fail("timestamp read");if(!ready)break;
        }
        if(!ready)break;
        if(!d.Disjoint && d.Frequency)out.push_back({q.block,q.copy,1000.0*(t[1]-t[0])/d.Frequency,1000.0*(t[2]-t[1])/d.Frequency,1000.0*(t[2]-t[0])/d.Frequency});
        else std::fprintf(stderr,"TIMER DISJOINT: discarded sample\n");release(q);p->pending.pop_front();
    }
}
void probeDestroy(ProbeNative* p){if(p->texture)p->texture->Release();delete p;}
#else
#error Unsupported probe backend
#endif
