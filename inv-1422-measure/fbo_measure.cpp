// Scratch mechanism probe for #1422; deliberately independent of TrussC.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void checked() { require(glGetError() == GL_NO_ERROR, "GL error"); }
GLuint program;
GLint reflectionLoc, flipLoc;
GLuint fixture;
const int fw=37, fh=23;
std::vector<unsigned char> pixels(fw*fh*4);
struct Target {
    int w,h,samples; GLuint texture=0, framebuffer=0, published=0, publication=0, msaa=0, renderbuffer=0;
    Target(int width,int height,bool halfFloat,int sampleCount,bool extra):w(width),h(height),samples(sampleCount) {
        auto allocate=[&](GLuint& tex,GLuint& fb) {
            glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
            glTexStorage2D(GL_TEXTURE_2D,1,halfFloat?GL_RGBA16F:GL_RGBA8,w,h);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1,&fb); glBindFramebuffer(GL_FRAMEBUFFER,fb);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
            require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"texture framebuffer incomplete");
        };
        allocate(texture,framebuffer);
        if(extra) allocate(published,publication);
        if(samples>1) {
            glGenRenderbuffers(1,&renderbuffer); glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,halfFloat?GL_RGBA16F:GL_RGBA8,w,h);
            glGenFramebuffers(1,&msaa); glBindFramebuffer(GL_FRAMEBUFFER,msaa);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,renderbuffer);
            require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"MSAA framebuffer incomplete");
        }
        checked();
    }
    ~Target() {
        glDeleteTextures(1,&texture); glDeleteTextures(1,&published);
        glDeleteFramebuffers(1,&framebuffer); glDeleteFramebuffers(1,&publication); glDeleteFramebuffers(1,&msaa);
        glDeleteRenderbuffers(1,&renderbuffer);
    }
};
enum Mode { Baseline, E, A };
const char* names[]={"baseline","E","A"};
struct State { GLuint fb; int w,h; Mode mode; bool clip; int x,y,cw,ch; };
std::vector<State> stack;
void select(const State& s) {
    glBindFramebuffer(GL_FRAMEBUFFER,s.fb); glViewport(0,0,s.w,s.h);
    glUniform1f(reflectionLoc,s.mode==A?-1.f:1.f);
    glFrontFace(s.mode==A?GL_CW:GL_CCW);
    if(s.clip) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(s.x,s.mode==A?s.y:s.h-s.y-s.ch,s.cw,s.ch);
    } else glDisable(GL_SCISSOR_TEST);
}
void begin(Target& t,Mode mode) {
    stack.push_back({t.samples>1?t.msaa:t.framebuffer,t.w,t.h,mode,false,0,0,0,0}); select(stack.back());
}
void quad(GLuint source,bool flip=false) {
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,source);
    glUniform1i(flipLoc,flip?1:0); glDrawArrays(GL_TRIANGLES,0,6);
}
GLuint end(Target& t,Mode mode) {
    if(t.samples>1) {
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,t.msaa); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,t.framebuffer);
        glBlitFramebuffer(0,0,t.w,t.h,0,0,t.w,t.h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    if(mode==E) {
        // Resolve first, then one full-screen copy with reversed physical rows.
        select({t.publication,t.w,t.h,E,false,0,0,0,0}); quad(t.texture);
    }
    stack.pop_back();
    if(!stack.empty()) select(stack.back());
    return mode==E?t.published:t.texture;
}
GLuint compile(GLenum type,const std::string& code) {
    GLuint shader=glCreateShader(type); const char* text=code.c_str(); glShaderSource(shader,1,&text,nullptr); glCompileShader(shader);
    GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[4096]; glGetShaderInfoLog(shader,sizeof(log),nullptr,log); std::fprintf(stderr,"%s\n",log); }
    require(ok,"shader compile"); return shader;
}
void initShader(bool gles) {
    std::string prefix=gles?"#version 300 es\nprecision highp float;\n":"#version 330 core\n";
    GLuint vs=compile(GL_VERTEX_SHADER,prefix+R"(
        uniform float reflection; out vec2 uv;
        const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1));
        void main() { vec2 p=corners[gl_VertexID]; uv=vec2(p.x*.5+.5,.5-p.y*.5); gl_Position=vec4(p.x,p.y*reflection,0,1); }
    )");
    GLuint fs=compile(GL_FRAGMENT_SHADER,prefix+R"(
        uniform sampler2D sourceTexture; uniform bool flip; in vec2 uv; out vec4 color;
        void main() { color=texture(sourceTexture,vec2(uv.x,flip?1.-uv.y:uv.y)); }
    )");
    program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
    GLint ok; glGetProgramiv(program,GL_LINK_STATUS,&ok); require(ok,"program link");
    glDeleteShader(vs); glDeleteShader(fs); glUseProgram(program);
    reflectionLoc=glGetUniformLocation(program,"reflection"); flipLoc=glGetUniformLocation(program,"flip");
    glUniform1i(glGetUniformLocation(program,"sourceTexture"),0);
    GLuint vao; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    for(int y=0;y<fh;++y) for(int x=0;x<fw;++x) {
        unsigned char* p=&pixels[(y*fw+x)*4];
        p[0]=(y<fh/2?(x<fw/2?255:0):(x<fw/2?0:255));
        p[1]=(x<fw/2?0:255); p[2]=(y<fh/2?0:(x<fw/2?255:0)); p[3]=255;
        if(x>=7&&x<10&&y>=3&&y<6) p[0]=p[1]=p[2]=255;
    }
    glGenTextures(1,&fixture); glBindTexture(GL_TEXTURE_2D,fixture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,fw,fh,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
}
std::vector<unsigned char> read(GLuint fb,int w,int h) {
    std::vector<unsigned char> data(w*h*4); glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data.data()); return data;
}
void smoke(Mode mode,int samples) {
    Target child(fw,fh,false,samples,mode==E), outer(61,43,false,1,mode==E), observed(fw,fh,false,1,mode==E);
    // Real EGL default framebuffer is the native window reference.
    stack.push_back({0,64,64,Baseline,false,0,0,0,0}); select(stack.back());
    glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    // The parent survives inner end.
    begin(outer,mode); glClearColor(.25f,.5f,.75f,1); glClear(GL_COLOR_BUFFER_BIT);
    stack.back().clip=true; stack.back().x=4; stack.back().y=2; stack.back().cw=fw; stack.back().ch=fh; select(stack.back());
    begin(child,mode); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); quad(fixture);
    GLuint result=end(child,mode);
    GLint binding; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&binding);
    require(static_cast<GLuint>(binding)==outer.framebuffer,"inner end did not restore parent");
    require(glIsEnabled(GL_SCISSOR_TEST),"parent scissor not restored");
    // Canonical public texture is sampled by an ordinary shader, into a reflected observation target.
    begin(observed,A); quad(result); end(observed,A);
    require(read(observed.framebuffer,fw,fh)==pixels,"published texture orientation/markers/culling");
    select(stack.back()); quad(fixture); end(outer,mode);
    // Compare all outer pixels, including clip placement and preservation outside clip.
    auto data=read(mode==E?outer.publication:outer.framebuffer,61,43);
    for(int y=0;y<43;++y) for(int x=0;x<61;++x) {
        bool inside=x>=4&&x<4+fw&&y>=2&&y<2+fh;
        const unsigned char* p=&data[(y*61+x)*4];
        if(inside) {
            int sx=static_cast<int>((x+.5)*fw/61), sy=static_cast<int>((y+.5)*fh/43);
            require(std::memcmp(p,&pixels[(sy*fw+sx)*4],4)==0,"outer scissor/quad mismatch");
        } else require(p[0]==64&&p[1]==128&&p[2]==191&&p[3]==255,"outer marker preservation");
    }
    select(stack.back());
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&binding);
    require(binding==0,"outer end did not restore window");
    // Draw into a small viewport, preserving the earlier window marker.
    glViewport(5,7,fw,fh); quad(mode==E?outer.published:outer.texture);
    auto window=read(0,64,64);
    require(window[0]==255&&window[1]==0&&window[2]==255,"window marker lost");
    for(int y=0;y<fh;++y) for(int x=0;x<fw;++x) {
        int sx=static_cast<int>((x+.5)*61/fw), sy=static_cast<int>((1.-(y+.5)/fh)*43);
        require(std::memcmp(&window[((y+7)*64+x+5)*4],&data[(sy*61+sx)*4],4)==0,"outer sampled to window orientation");
    }
    // An independent source is drawn afterward to check the window output transform.
    glViewport(0,0,fw,fh); quad(fixture);
    window=read(0,fw,fh);
    for(int y=0;y<fh;++y) require(std::memcmp(&window[y*fw*4],&pixels[(fh-1-y)*fw*4],fw*4)==0,"window orientation after outer end");
    stack.pop_back();
    // Odd/even ping-pong chains must not conceal paired orientation mistakes.
    for(int pass=1;pass<=6;++pass) {
        Target& destination=pass%2?child:observed;
        begin(destination,mode); quad(pass==1?fixture:result); result=end(destination,mode);
        auto chain=read(mode==E?destination.publication:destination.framebuffer,fw,fh);
        require(chain==pixels,"ping-pong published pixels");
    }
    glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); checked();
    std::printf("SMOKE mode=%s samples=%d canonical=PASS nested=PASS scissor=PASS culling=PASS window=PASS chain_1_to_6=PASS pixels=PASS\n",names[mode],samples);
}
bool flipYSmoke() {
    bool available=false;
    GLint extensionCount=0;
    glGetIntegerv(GL_NUM_EXTENSIONS,&extensionCount);
    for(GLint i=0;i<extensionCount;++i) {
        if(!std::strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS,i)),"GL_MESA_framebuffer_flip_y")) available=true;
    }
    if(!available) {
        std::puts("SKIP flip-y-smoke GL_MESA_framebuffer_flip_y absent");
        return true;
    }
    auto setFlip=reinterpret_cast<PFNGLFRAMEBUFFERPARAMETERIMESAPROC>(eglGetProcAddress("glFramebufferParameteriMESA"));
    auto getFlip=reinterpret_cast<PFNGLGETFRAMEBUFFERPARAMETERIVMESAPROC>(eglGetProcAddress("glGetFramebufferParameterivMESA"));
    require(setFlip&&getFlip,"MESA framebuffer entry points missing");
    bool passed=true;
    auto report=[&](int samples,const char* check,bool ok) {
        std::printf("%s flip-y samples=%d check=%s\n",ok?"PASS":"FAIL",samples,check);
        passed=passed&&ok;
    };
    auto flip=[&](GLuint fb,int value) {
        glBindFramebuffer(GL_FRAMEBUFFER,fb);
        setFlip(GL_FRAMEBUFFER,GL_FRAMEBUFFER_FLIP_Y_MESA,value);
    };
    auto reversed=[](const std::vector<unsigned char>& data) {
        std::vector<unsigned char> result(data.size());
        for(int y=0;y<fh;++y) std::memcpy(&result[y*fw*4],&data[(fh-1-y)*fw*4],fw*4);
        return result;
    };
    // Independent fragment probe: native window Y, facing, and the sign of a Y derivative.
    GLuint vs=compile(GL_VERTEX_SHADER,R"(#version 300 es
        precision highp float;
        out vec2 uv;
        const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1));
        void main() {
            vec2 p=corners[gl_VertexID];
            uv=vec2(p.x*.5+.5,.5-p.y*.5);
            gl_Position=vec4(p,0,1);
        }
    )");
    GLuint fs=compile(GL_FRAGMENT_SHADER,R"(#version 300 es
        precision highp float;
        in vec2 uv;
        out vec4 color;
        void main() {
            color=vec4(floor(gl_FragCoord.y)/255.,gl_FrontFacing?1.:0.,dFdy(uv.y)<0.?1.:0.,1.);
        }
    )");
    GLuint probe=glCreateProgram();
    glAttachShader(probe,vs);
    glAttachShader(probe,fs);
    glLinkProgram(probe);
    GLint linked=0;
    glGetProgramiv(probe,GL_LINK_STATUS,&linked);
    require(linked,"flip-y probe link");
    glDeleteShader(vs);
    glDeleteShader(fs);
    for(int samples:{1,4}) {
        Target target(fw,fh,false,samples,false);
        GLuint renderFb=samples>1?target.msaa:target.framebuffer;
        flip(renderFb,1);
        GLint parameter=0;
        getFlip(GL_FRAMEBUFFER,GL_FRAMEBUFFER_FLIP_Y_MESA,&parameter);
        report(samples,"parameter",parameter==1);
        flip(target.framebuffer,1);
        GLint actualSamples=0;
        glBindRenderbuffer(GL_RENDERBUFFER,target.renderbuffer);
        if(samples>1) glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&actualSamples);
        std::printf("CONFIG requested_samples=%d renderbuffer_samples=%d\n",samples,actualSamples);
        auto selectRender=[&]() {
            glUseProgram(program);
            glBindFramebuffer(GL_FRAMEBUFFER,renderFb);
            glViewport(0,0,fw,fh);
            glDisable(GL_SCISSOR_TEST);
            glFrontFace(GL_CCW);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glUniform1f(reflectionLoc,1.f);
            glClearColor(1,0,1,1);
            glClear(GL_COLOR_BUFFER_BIT);
        };
        auto resolve=[&]() {
            glDisable(GL_SCISSOR_TEST);
            if(samples>1) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER,renderFb);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target.framebuffer);
                glBlitFramebuffer(0,0,fw,fh,0,0,fw,fh,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            }
        };
        auto sampleDefault=[&](GLuint source=0) {
            glUseProgram(program);
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            glViewport(0,0,fw,fh);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glUniform1f(reflectionLoc,1.f);
            quad(source?source:target.texture);
            // Default framebuffer readback is bottom-first; comparison is top-first.
            return reversed(read(0,fw,fh));
        };
        selectRender();
        quad(fixture);
        resolve();
        report(samples,"ordinary_texture_sample_top_at_v0",sampleDefault()==pixels);
        auto flaggedRows=read(target.framebuffer,fw,fh);
        report(samples,"readpixels_flipped_fbo_bottom_first",flaggedRows==reversed(pixels));
        flip(target.framebuffer,0);
        report(samples,"readpixels_unflipped_alias_top_first",read(target.framebuffer,fw,fh)==pixels);
        if(samples>1) {
            // Equal blit bounds preserve logical coordinates, so differing flags reverse storage rows.
            resolve();
            report(samples,"resolve_different_flags_reverses_storage",sampleDefault()==reversed(pixels));
        }
        flip(target.framebuffer,1);
        resolve();
        report(samples,"resolve_matching_flags_preserves_storage",sampleDefault()==pixels);

        selectRender();
        glEnable(GL_SCISSOR_TEST);
        glScissor(4,fh-2-7,19,7);
        quad(fixture);
        resolve();
        auto clipped=sampleDefault();
        bool scissorOk=true;
        int clipMismatches=0;
        for(int y=0;y<fh;++y) for(int x=0;x<fw;++x) {
            const unsigned char background[]={255,0,255,255};
            const auto* expected=x>=4&&x<23&&y>=2&&y<9?&pixels[(y*fw+x)*4]:background;
            const auto* actual=&clipped[(y*fw+x)*4];
            if(std::memcmp(actual,expected,4)!=0) {
                if(clipMismatches++<4) std::printf("DETAIL scissor samples=%d x=%d y=%d rgba=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                    samples,x,y,actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);
                scissorOk=false;
            }
        }
        report(samples,"scissor_existing_top_left_mapping",scissorOk);
        if(samples>1) {
            // Fresh native FBOs have never had flip-y enabled.
            Target native(fw,fh,false,samples,false);
            selectRender();
            glBindFramebuffer(GL_FRAMEBUFFER,native.msaa);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
            glScissor(4,fh-2-7,19,7);
            quad(fixture);
            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,native.msaa);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,native.framebuffer);
            glBlitFramebuffer(0,0,fw,fh,0,0,fw,fh,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            auto nativeClip=reversed(sampleDefault(native.texture));
            bool nativeOk=true;
            int nativeMismatches=0;
            for(int y=0;y<fh;++y) for(int x=0;x<fw;++x) {
                const unsigned char background[]={255,0,255,255};
                const auto* expected=x>=4&&x<23&&y>=2&&y<9?&pixels[(y*fw+x)*4]:background;
                const auto* actual=&nativeClip[(y*fw+x)*4];
                if(std::memcmp(actual,expected,4)!=0) {
                    if(nativeMismatches++<4) std::printf("DETAIL native_scissor samples=%d x=%d y=%d rgba=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                        samples,x,y,actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);
                    nativeOk=false;
                }
            }
            report(samples,"scissor_native_control",nativeOk);
            report(samples,"scissor_flip_matches_native_control",nativeClip==clipped);
        }

        selectRender();
        glViewport(3,fh-4-11,19,11);
        quad(fixture);
        resolve();
        auto viewport=sampleDefault();
        bool viewportOk=true;
        for(int y=0;y<fh;++y) for(int x=0;x<fw;++x) {
            const unsigned char background[]={255,0,255,255};
            const unsigned char* expected=background;
            if(x>=3&&x<22&&y>=4&&y<15) {
                int sx=static_cast<int>((x-3+.5)*fw/19),sy=static_cast<int>((y-4+.5)*fh/11);
                expected=&pixels[(sy*fw+sx)*4];
            }
            viewportOk=viewportOk&&std::memcmp(&viewport[(y*fw+x)*4],expected,4)==0;
        }
        report(samples,"viewport_existing_top_left_mapping",viewportOk);

        selectRender();
        glFrontFace(GL_CW);
        quad(fixture);
        resolve();
        auto culled=sampleDefault();
        bool cullOk=true;
        for(size_t i=0;i<culled.size();i+=4) cullOk=cullOk&&culled[i]==255&&culled[i+1]==0&&culled[i+2]==255;
        report(samples,"culling_CW_rejected_CCW_kept",cullOk);
        for(GLenum winding:{GL_CCW,GL_CW}) {
            selectRender();
            glDisable(GL_CULL_FACE);
            glFrontFace(winding);
            glUseProgram(probe);
            glDrawArrays(GL_TRIANGLES,0,6);
            resolve();
            auto fragment=sampleDefault();
            bool coordOk=true,facingOk=true,derivativeOk=true;
            // Interior pixels avoid primitive-edge helper invocations and MSAA coverage.
            for(int y=2;y<fh-2;++y) for(int x=2;x<fw-2;++x) {
                const auto* p=&fragment[(y*fw+x)*4];
                coordOk=coordOk&&p[0]==fh-1-y;
                facingOk=facingOk&&p[1]==(winding==GL_CCW?255:0);
                derivativeOk=derivativeOk&&p[2]==255;
            }
            report(samples,winding==GL_CCW?"fragcoord_native_bottom_origin":"fragcoord_native_bottom_origin_CW",coordOk);
            report(samples,winding==GL_CCW?"frontfacing_CCW_true":"frontfacing_CW_false",facingOk);
            report(samples,winding==GL_CCW?"derivative_native_sign":"derivative_native_sign_CW",derivativeOk);
        }
        checked();
    }
    glDeleteProgram(probe);
    glUseProgram(program);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    return passed;
}
struct Case { const char* name; int w,h; bool halfFloat; int samples,count,passes; };
double percentile(std::vector<double> v,double q) { std::sort(v.begin(),v.end()); return v[static_cast<size_t>(std::ceil(q*v.size()))-1]; }
void bench(const Case& c,int rounds,int batch) {
    // Allocate only the active mode's targets so baseline/A do not retain E's extra storage.
    std::array<std::vector<double>,3> values;
    for(int round=0;round<rounds;++round) for(int position=0;position<3;++position) {
        Mode mode=static_cast<Mode>((position+round)%3);
        std::vector<Target*> targets;
        for(int i=0;i<c.count;++i) targets.push_back(new Target(c.w,c.h,c.halfFloat,c.samples,mode==E));
        auto frame=[&]() {
            GLuint source=fixture;
            for(int pass=0;pass<c.passes;++pass) {
                Target& target=*targets[pass%c.count]; begin(target,mode);
                // Frozen opaque full-screen quad. Native baseline compensates only native Fbo inputs.
                quad(source,mode==Baseline&&pass>0&&c.passes!=c.count); source=end(target,mode);
                if(c.passes==c.count) source=fixture; // independent small Fbos
            }
            glFinish();
        };
        frame(); frame(); // warm each freshly allocated set, outside timing
        for(int sample=0;sample<batch;++sample) {
            auto start=std::chrono::steady_clock::now(); frame();
            values[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        checked(); for(auto* target:targets) delete target;
    }
    for(int mode=0;mode<3;++mode) {
        std::printf("RESULT case=%s mode=%s n=%zu median_ms=%.6f p95_ms=%.6f\n",c.name,names[mode],values[mode].size(),percentile(values[mode],.5),percentile(values[mode],.95));
        for(size_t i=0;i<values[mode].size();++i) std::printf("SAMPLE case=%s mode=%s index=%zu ms=%.6f\n",c.name,names[mode],i,values[mode][i]);
    }
    std::printf("MEMORY case=%s extra_textures=%d bytes=%llu minimum_copy_bytes_per_frame=%llu\n",c.name,c.count,
        static_cast<unsigned long long>(c.w)*c.h*(c.halfFloat?8:4)*c.count,
        static_cast<unsigned long long>(c.w)*c.h*(c.halfFloat?8:4)*2*c.passes);
    std::fflush(stdout);
}
int main(int argc,char** argv) {
    try {
        bool gles=false,onlySmoke=false,extensions=false,flipSmoke=false; int rounds=15,batch=3;
        for(int i=1;i<argc;++i) {
            if(!std::strcmp(argv[i],"--gles")) gles=true;
            else if(!std::strcmp(argv[i],"--gl")) gles=false;
            else if(!std::strcmp(argv[i],"--extensions")) extensions=true;
            else if(!std::strcmp(argv[i],"--flip-y-smoke")) flipSmoke=true;
            else if(!std::strcmp(argv[i],"--smoke-only")) onlySmoke=true;
            else if(!std::strcmp(argv[i],"--rounds")&&i+1<argc) rounds=std::stoi(argv[++i]);
            else if(!std::strcmp(argv[i],"--batch")&&i+1<argc) batch=std::stoi(argv[++i]);
            else throw std::runtime_error("usage: fbo_measure [--gl|--gles] [--extensions] [--flip-y-smoke] [--smoke-only] [--rounds N] [--batch N]");
        }
        if(flipSmoke) gles=true;
        require(rounds>0&&batch>0,"positive sample counts required");
        Display* xd=XOpenDisplay(nullptr); require(xd,"XOpenDisplay failed (explicit DISPLAY required)");
        EGLDisplay d=eglGetDisplay(static_cast<EGLNativeDisplayType>(xd)); require(d!=EGL_NO_DISPLAY,"EGL display");
        EGLint major,minor; require(eglInitialize(d,&major,&minor),"EGL initialize");
        require(eglBindAPI(gles?EGL_OPENGL_ES_API:EGL_OPENGL_API),"EGL bind API");
        EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,gles?EGL_OPENGL_ES3_BIT:EGL_OPENGL_BIT,EGL_NONE};
        EGLConfig config; EGLint count; require(eglChooseConfig(d,attrs,&config,1,&count)&&count,"EGL config");
        EGLint contextAttrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,gles?0:3,EGL_NONE};
        EGLContext context=eglCreateContext(d,config,EGL_NO_CONTEXT,contextAttrs); require(context!=EGL_NO_CONTEXT,"EGL context");
        EGLint surfaceAttrs[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
        EGLSurface surface=eglCreatePbufferSurface(d,config,surfaceAttrs); require(surface!=EGL_NO_SURFACE,"EGL pbuffer");
        require(eglMakeCurrent(d,surface,surface,context),"EGL current");
        if(flipSmoke) {
            EGLint surfaceSamples=0;
            eglGetConfigAttrib(d,config,EGL_SAMPLES,&surfaceSamples);
            std::printf("CONFIG default_surface_samples=%d\n",surfaceSamples);
        }
        if(extensions) {
            std::printf("GL_VERSION=%s\n",glGetString(GL_VERSION));
            std::printf("GL_RENDERER=%s\n",glGetString(GL_RENDERER));
            std::printf("GL_SHADING_LANGUAGE_VERSION=%s\n",glGetString(GL_SHADING_LANGUAGE_VERSION));
            if(gles) {
                std::printf("GL_EXTENSIONS=%s\n",glGetString(GL_EXTENSIONS));
            } else {
                GLint extensionCount=0;
                glGetIntegerv(GL_NUM_EXTENSIONS,&extensionCount);
                std::puts("GL_EXTENSIONS:");
                for(GLint i=0;i<extensionCount;++i) std::printf("%s\n",glGetStringi(GL_EXTENSIONS,static_cast<GLuint>(i)));
            }
            checked();
            eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT); eglDestroySurface(d,surface); eglDestroyContext(d,context); eglTerminate(d); XCloseDisplay(xd);
            return 0;
        }
        std::printf("DEVICE api=%s vendor=%s renderer=%s version=%s\n",gles?"GLES":"GL",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
        if(flipSmoke) std::puts("METHOD flip-y-smoke samples=1,4 frozen=37x23_markers no_clip_control no_vertex_reflection");
        else std::printf("METHOD rounds=%d batch=%d warmup_per_block=2 order=rotating_B_E_A timer=steady_clock_with_glFinish frozen=37x23_markers\n",rounds,batch);
        initShader(gles);
        bool passed=true;
        if(flipSmoke) passed=flipYSmoke();
        else for(Mode mode:{E,A}) for(int samples:{1,4}) smoke(mode,samples);
        if(!onlySmoke&&!flipSmoke) {
            const Case cases[]={ {"1080p_RGBA8",1920,1080,false,1,1,1}, {"4K_RGBA16F",3840,2160,true,1,1,1},
                {"1080p_MSAA4",1920,1080,false,4,1,1}, {"six_pass",1920,1080,false,1,2,6}, {"100_small",256,256,false,1,100,100} };
            stack.push_back({0,64,64,Baseline,false,0,0,0,0});
            for(const auto& c:cases) bench(c,rounds,batch);
            stack.pop_back();
        }
        checked(); std::puts("COMPLETE gl_error=0");
        eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT); eglDestroySurface(d,surface); eglDestroyContext(d,context); eglTerminate(d); XCloseDisplay(xd);
        return passed?0:1;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
