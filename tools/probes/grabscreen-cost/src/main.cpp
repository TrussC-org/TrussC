#include <TrussC.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#define PROBE_GL 1
#endif
#ifdef TC_COST_INTERNAL
#include "/tmp/grabscreen/stats.h"
#endif
using namespace tc;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
static int W=1280,H=720,N=600,warm=100;
static std::string mode="off", output="cost.csv";
static bool split=false;
class Probe : public App {
    EventListener listener;
    Pixels pixels;
    std::vector<unsigned char> small;
    std::vector<std::vector<double>> rows;
    Clock::time_point previous;
    int frame=0;
    unsigned long long checksum=0;
    void finish() {
        std::ofstream f(output);
        f << "frame_ms,capture_ms,alloc_ms,finish_ms,read_ms,flip_ms,other_ms\n";
        for (auto& r:rows) { for(size_t i=0;i<r.size();++i) f << (i?",":"") << r[i]; f << '\n'; }
        const char* names[]={"frame","capture","alloc","finish","read","flip","other"};
        std::printf("RESULT mode=%s fb=%dx%d samples=%zu checksum=%llu split=%d\n",mode.c_str(),sapp_width(),sapp_height(),rows.size(),checksum,split);
        for(size_t c=0;c<7;++c) {
            std::vector<double> v; for(auto& r:rows) v.push_back(r[c]);
            std::sort(v.begin(),v.end());
            std::printf("%s mean=%.6f median=%.6f p95=%.6f ms\n",names[c],std::accumulate(v.begin(),v.end(),0.0)/v.size(),(v[(v.size()-1)/2]+v[v.size()/2])/2,v[(size_t)std::ceil(v.size()*.95)-1]);
        }
        std::fflush(stdout);
        exitApp();
    }
    void after() {
        const auto a=Clock::now();
        double alloc=0,wait=0,read=0,flip=0;
#ifdef TC_COST_INTERNAL
        cost::alloc=cost::wait=cost::read=cost::flip=0; cost::split=split;
#endif
        if(mode=="full") {
            if(!grabScreen(pixels) || pixels.getWidth()!=W || pixels.getHeight()!=H) std::exit(3);
            checksum += pixels.getData()[(size_t(H/2)*W+W/2)*4];
#ifdef TC_COST_INTERNAL
            alloc=cost::alloc; wait=cost::wait; read=cost::read; flip=cost::flip;
#endif
        }
#ifdef PROBE_GL
        else if(mode=="1" || mode=="32" || mode=="raw") {
            const int rw=mode=="raw"?W:std::stoi(mode),rh=mode=="raw"?H:rw;
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glReadBuffer(GL_BACK);
            glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glPixelStorei(GL_PACK_ALIGNMENT,1);
            glPixelStorei(GL_PACK_ROW_LENGTH,0); glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
            auto b=Clock::now(); if(split) glFinish(); auto c=Clock::now();
            glReadPixels((W-rw)/2,(H-rh)/2,rw,rh,GL_RGBA,GL_UNSIGNED_BYTE,small.data());
            auto d=Clock::now(); wait=ms(b,c); read=ms(c,d);
            if(glGetError()!=GL_NO_ERROR) std::exit(4);
            checksum += small[0];
        }
#endif
#ifdef PROBE_GL
        if(frame==0 && mode=="full" && std::getenv("COST_VALIDATE")) {
            for(int side : {1,32}) {
                const int x=(W-side)/2,y=(H-side)/2;
                glReadPixels(x,y,side,side,GL_RGBA,GL_UNSIGNED_BYTE,small.data());
                if(glGetError()!=GL_NO_ERROR) std::exit(5);
                for(int r=0;r<side;++r)
                    if(std::memcmp(small.data()+r*side*4,pixels.getData()+((size_t(H-1-y-r)*W)+x)*4,side*4)) std::exit(6);
            }
            if(!pixels.save(std::getenv("COST_VALIDATE"))) std::exit(7);
            std::puts("VALIDATED same-frame 1x1 and 32x32 against full capture");
        }
#endif
        const auto end=Clock::now();
        if(frame>=warm && frame<warm+N) {
            const double capture=ms(a,end);
            rows.push_back({ms(previous,end),capture,alloc,wait,read,flip,capture-alloc-wait-read-flip});
        }
        previous=end;
        if(++frame==warm+N) finish();
    }
public:
    void setup() override {
        if(sapp_width()!=W || sapp_height()!=H) { std::fprintf(stderr,"wrong framebuffer %dx%d expected %dx%d\n",sapp_width(),sapp_height(),W,H); std::exit(2); }
        rows.reserve(N);
        small.resize(mode=="raw"?size_t(W)*H*4:32*32*4);
#ifdef PROBE_GL
        std::printf("GL vendor=%s renderer=%s version=%s requested_swap=%d\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION),0);
#else
        std::printf("backend=%d requested_swap=%d\n",int(sg_query_backend()),0);
#endif
        listener=events().afterFrame.listen([this]{after();});
    }
    void draw() override {
        clear(0.06f,0.08f,0.12f);
        for(int i=0;i<768;++i) {
            setColor((i%17)/17.f,(i%29)/29.f,(i%37)/37.f,0.75f);
            const float x=float((i*97+frame*3)%W), y=float((i*53+frame*2)%H);
            drawRect(x,y,W/16.f,H/18.f);
        }
        for(int i=0;i<64;++i) {
            setColor(0.8f,(i%11)/11.f,0.4f,0.6f);
            drawCircle(float((i*127+frame)%W),float((i*71+frame)%H),H/40.f);
        }
    }
};
int main(int argc,char**argv) {
    for(int i=1;i<argc;++i) {
        std::string s=argv[i];
        if(s=="--split") split=true;
        else if(i+1<argc && s=="--width") W=std::atoi(argv[++i]);
        else if(i+1<argc && s=="--height") H=std::atoi(argv[++i]);
        else if(i+1<argc && s=="--frames") N=std::atoi(argv[++i]);
        else if(i+1<argc && s=="--warmup") warm=std::atoi(argv[++i]);
        else if(i+1<argc && s=="--mode") mode=argv[++i];
        else if(i+1<argc && s=="--out") output=argv[++i];
        else { std::fprintf(stderr,"invalid argument %s\n",s.c_str()); return 1; }
    }
    if(W<32 || H<32 || N<1 || warm<1) return 1;
    if(mode!="off" && mode!="full"
#ifdef PROBE_GL
       && mode!="1" && mode!="32" && mode!="raw"
#endif
    ) return 1;
    WindowSettings settings;
    settings.setSize(W,H).setHighDpi(true).setPixelPerfect(true).setSampleCount(1).setSwapInterval(0);
    settings.title="grabScreen cost";
    return TC_RUN_APP(Probe,settings);
}
