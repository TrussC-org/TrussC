#include <TrussC.h>
#include "unionAdapter.h"
#include "triangles.glsl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi.h>
#endif
#include <stdexcept>
#if defined(__linux__) && defined(SOKOL_GLCORE)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif
using namespace tc;
namespace {
using Clock = std::chrono::steady_clock;
std::string output, images;
int resolutionFilter = 0;
const char* modeNames[] = {"none", "stencil_clear", "stencil_cycle", "stencil_redraw", "union_frame", "union_cached"};
const char* sceneNames[] = {"long200", "short5000", "self2000"};
struct Stroke { std::vector<Vec2> points, triangles, cached; Color color; };
struct Counts { int draws=0, switches=0, clears=0; };
// All modes append through this same submitter. Color changes are vertex data;
// only pipeline changes and explicit clear barriers force a flush.
struct Vertex { float x,y,r,g,b,a; };
sg_buffer vertexBuffer{};
size_t bufferCapacity=0;
class Submitter {
    struct Command { sg_pipeline pipeline; int start,count; };
    std::vector<Vertex> vertices;
    std::vector<Command> commands;
    sg_pipeline current{};
public:
    Counts counts;
    void select(sg_pipeline p) {
        if(p.id==current.id) return;
        current=p;
    }
    void append(const std::vector<Vec2>& triangles, Color c) {
        if(commands.empty() || commands.back().pipeline.id!=current.id) {
            commands.push_back({current,int(vertices.size()),0});
        }
        for(auto p:triangles) vertices.push_back({p.x,p.y,c.r,c.g,c.b,c.a});
        commands.back().count+=int(triangles.size());
    }
    void clearStencilBarrier() { commands.push_back({{},0,0}); }
    void finish() {
        size_t bytes=vertices.size()*sizeof(Vertex);
        if(bytes>bufferCapacity) {
            if(vertexBuffer.id) sg_destroy_buffer(vertexBuffer);
            sg_buffer_desc d={}; d.size=bytes; d.usage.write_transient=true;
            vertexBuffer=sg_make_buffer(&d); bufferCapacity=bytes;
        }
        sg_write_buffer_desc write={}; write.dst.buffer=vertexBuffer;
        write.src.data={vertices.data(),bytes}; sg_write_buffer_transient(&write);
        auto& t=internal::currentTarget(); internal::ensureCurrentPass();
        stroke_overlap_params_t params={}; params.targetSize[0]=float(t.width); params.targetSize[1]=float(t.height);
        sg_bindings bindings={}; bindings.vertex_buffers[0]=vertexBuffer;
        uint32_t last=0;
        for(auto command:commands) {
            if(!command.pipeline.id) { clearStencil(); ++counts.clears; ++counts.draws; ++counts.switches; last=0; continue; }
            if(last!=command.pipeline.id) { sg_apply_pipeline(command.pipeline); ++counts.switches; last=command.pipeline.id; }
            sg_apply_viewport(0,0,t.width,t.height,true); sg_apply_scissor_rect(0,0,t.width,t.height,true);
            sg_apply_bindings(&bindings); sg_apply_uniforms(UB_stroke_overlap_params,SG_RANGE(params));
            sg_draw(command.start,command.count,1); ++counts.draws;
        }
        internal::applyTargetDrawingState();
    }
};
double elapsed(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }
double quantile(std::vector<double> v, double q) {
    std::sort(v.begin(),v.end());
    double index=q*(v.size()-1); auto i=size_t(index);
    return v[i]+(v[std::min(i+1,v.size()-1)]-v[i])*(index-i);
}
Pixels read(Fbo& f) {
    Pixels p; p.allocate(f.getWidth(),f.getHeight(),4);
    if (!f.readPixels(p.getData())) throw std::runtime_error("color readback failed");
    return p;
}
void complete(Fbo& f) {
#if defined(__linux__) && defined(SOKOL_GLCORE)
    (void)f; glFinish();
#else
    (void)read(f);
#endif
}
class Bench : public App {
    Fbo target, coverage, reference;
    std::vector<Stroke> strokes;
    sg_pipeline normal{}, erase{}, opaque{}, values[256]{};
    sg_shader shader{};
    int resolution=0, scene=0, mode=0, frame=0, referenceStroke=0;
    bool configured=false, referenceReady=false, firstResult=true;
    int width=640,height=480;
    float strokeWidth=12;
    Clock::time_point caseStart;
    std::vector<double> cpus,walls,clipperTimes,earcutTimes,drawTimes;
    strokeOverlap::UnionTiming frameTiming, cacheTiming;
    double cacheMs=0;
    std::ofstream json;
    std::string backend,renderer;
    Pixels referencePixels;
    uint32_t seed=0x1386;
    float random01() { seed=seed*1664525u+1013904223u; return float(seed>>8)/16777216.0f; }
    void configure() {
        width=resolution?1920:640; height=resolution?1080:480; strokeWidth=12.0f*height/480;
        target.allocate(width,height,1); coverage.allocate(width,height,1); reference.allocate(width,height,1);
        strokes.clear(); seed=0x1386; cacheTiming={}; cacheMs=0;
        const Color palette[] = {Color(.85f,.2f,.15f,.5f),Color(.1f,.65f,.85f,.5f),Color(.65f,.3f,.8f,.5f),Color(.2f,.8f,.35f,.5f)};
        int count=scene==0?200:scene==1?5000:1;
        for (int i=0;i<count;++i) {
            Stroke s; s.color=palette[i%4];
            int points=scene==0?24:scene==1?4:2000;
            float x=random01()*width,y=random01()*height;
            for (int j=0;j<points;++j) {
                if (scene==0) { x=(.05f+.9f*random01())*width; y=(.05f+.9f*random01())*height; }
                else if (scene==1) { x=std::clamp(x+(random01()-.5f)*24*height/480,0.0f,float(width)); y=std::clamp(y+(random01()-.5f)*24*height/480,0.0f,float(height)); }
                else { float t=j*.037f; x=width*(.5f+.44f*std::sin(t*3)); y=height*(.5f+.44f*std::sin(t*4+.3f)); }
                s.points.emplace_back(x,y);
            }
            StrokeMesh generator;
            generator.setWidth(strokeWidth).setJoinType(StrokeMesh::JOIN_ROUND).setCapType(StrokeMesh::CAP_ROUND);
            for(auto p:s.points) generator.addVertex(p);
            generator.update();
            for(auto p:generator.getMesh().getVertices()) s.triangles.emplace_back(p.x,p.y);
            if (strokeOverlap::unionAvailable()) {
                auto start=Clock::now();
                if(!strokeOverlap::unionStroke(s.points,strokeWidth,s.cached,cacheTiming)) throw std::runtime_error("cached union failed");
                cacheMs+=elapsed(start);
            }
            strokes.push_back(std::move(s));
        }
        target.begin(0,0,0,1); setupScreenOrtho(); setBlendMode(BlendMode::Alpha);
        auto& t=internal::currentTarget(); t.stencilOwned=true;
        sg_pipeline_desc desc={}; desc.shader=shader;
        desc.layout.attrs[ATTR_stroke_overlap_triangles_position].format=SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_stroke_overlap_triangles_color0].format=SG_VERTEXFORMAT_FLOAT4;
        desc.layout.buffers[0].stride=sizeof(Vertex);
        desc.colors[0].blend.enabled=true;
        desc.colors[0].blend.src_factor_rgb=SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.colors[0].blend.src_factor_alpha=SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.dst_factor_alpha=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.depth.compare=SG_COMPAREFUNC_ALWAYS;
        auto pipeline=[&]() { auto p=t.pipeline(internal::PipelineKey::raw(internal::PipelineRole::Fill,desc)).raw;
            if(sg_query_pipeline_state(p)!=SG_RESOURCESTATE_VALID) throw std::runtime_error("pipeline allocation failed"); return p; };
        normal=pipeline(); desc.colors[0].blend.enabled=false; opaque=pipeline(); desc.colors[0].blend.enabled=true;
        desc.stencil.enabled=true; desc.stencil.read_mask=desc.stencil.write_mask=255;
        desc.stencil.front.compare=SG_COMPAREFUNC_NOT_EQUAL; desc.stencil.front.pass_op=SG_STENCILOP_REPLACE;
        desc.stencil.back=desc.stencil.front;
        for(int v=1;v<=255;++v) { desc.stencil.ref=uint8_t(v); values[v]=pipeline(); }
        desc.stencil.ref=0; desc.stencil.front.compare=SG_COMPAREFUNC_ALWAYS;
        desc.stencil.back=desc.stencil.front; desc.colors[0].write_mask=SG_COLORMASK_NONE;
        erase=pipeline(); clearStencil(); target.end();
        reference.begin(0,0,0,1); reference.end();
        referenceStroke=0; referenceReady=false; configured=true;
        size_t vertices=0; for(auto& s:strokes) vertices+=s.triangles.size();
        std::printf("SCENE %s %dx%d strokes=%zu vertices=%zu precreated_refs=255 cache_ms=%.3f clipper_ms=%.3f earcut_ms=%.3f\n",sceneNames[scene],width,height,strokes.size(),vertices,cacheMs,cacheTiming.clipperMs,cacheTiming.earcutMs); std::fflush(stdout);
    }
    void makeReferenceStep() {
        auto& s=strokes[referenceStroke];
        coverage.begin(0,0,0,0); setupScreenOrtho();
        Submitter submit; submit.select(opaque); submit.append(s.triangles,Color(1,1,1,1)); submit.finish(); coverage.end();
        reference.begin(); setupScreenOrtho(); setBlendMode(BlendMode::Alpha);
        // Fbo textures are premultiplied: tint RGB carries the opacity too.
        setColor(s.color.r*s.color.a,s.color.g*s.color.a,s.color.b*s.color.a,s.color.a);
        coverage.draw(0,0); reference.end();
        if(++referenceStroke==int(strokes.size())) {
            referencePixels=read(reference); referenceReady=true;
            if(!resolution && !images.empty()) referencePixels.save(fs::path(images)/(std::string(sceneNames[scene])+"-reference.png"));
        }
    }
    void beginFrame() {
        target.begin(0,0,0,1); setupScreenOrtho(); setBlendMode(BlendMode::Alpha);
        internal::currentTarget().stencilOwned=true;
        // Initial clear and color/pass setup are common, outside measured work.
        clearStencil(); internal::currentTarget().flushLayers(); completeBefore();
    }
    void completeBefore() {
#if defined(__linux__) && defined(SOKOL_GLCORE)
        glFinish();
#endif
    }
    Counts record() {
        frameTiming={};
        Submitter submit;
        for(size_t k=0;k<strokes.size();++k) {
            auto& s=strokes[k];
            if(mode==2 && k && k%255==0) submit.clearStencilBarrier();
            submit.select(mode==1||mode==3?values[1]:mode==2?values[k%255+1]:normal);
            if(mode>=4) {
                if(mode==4) { std::vector<Vec2> united; if(!strokeOverlap::unionStroke(s.points,strokeWidth,united,frameTiming)) throw std::runtime_error("frame union failed"); submit.append(united,s.color); }
                else submit.append(s.cached,s.color);
            } else submit.append(s.triangles,s.color);
            if(mode==1) submit.clearStencilBarrier();
            if(mode==3) { submit.select(erase); submit.append(s.triangles,s.color); }
        }
        submit.finish(); return submit.counts;
    }
    void saveResult(Counts counts) {
        auto pixels=read(target);
        int maximum=0; uint64_t total=0, bad=0;
        for(int p=0;p<width*height;++p) {
            bool off=false;
            for(int c=0;c<4;++c) { int d=std::abs(int(pixels.getData()[p*4+c])-int(referencePixels.getData()[p*4+c])); maximum=std::max(maximum,d); total+=d; off|=d>2; }
            bad+=off;
        }
        if(!resolution && !images.empty()) {
            pixels.save(fs::path(images)/(std::string(sceneNames[scene])+"-"+modeNames[mode]+".png"));
            if(mode==0) {
                Pixels crop; crop.allocate(256,128,4);
                for(int y=0;y<128;++y) for(int x=0;x<128;++x) for(int c=0;c<4;++c) {
                    int source=((height/2-64+y)*width+width/2-64+x)*4+c;
                    crop.getData()[(y*256+x)*4+c]=pixels.getData()[source];
                    crop.getData()[(y*256+128+x)*4+c]=referencePixels.getData()[source];
                }
                crop.save(fs::path(images)/(std::string(sceneNames[scene])+"-none-reference-crop.png"));
            }
        }
        double cm=quantile(cpus,.5),cp=quantile(cpus,.95),wm=quantile(walls,.5),wp=quantile(walls,.95),mad=double(total)/(width*height*4);
        std::printf("| %s | %dx%d | %s | %zu | %.3f / %.3f | %.3f / %.3f | %d | %d | %d / %.5f / %llu |\n",sceneNames[scene],width,height,modeNames[mode],cpus.size(),cm,cp,wm,wp,counts.draws,counts.switches,maximum,mad,(unsigned long long)bad); std::fflush(stdout);
        if(!firstResult) json<<",\n"; firstResult=false;
        json<<"{\"scene\":\""<<sceneNames[scene]<<"\",\"width\":"<<width<<",\"height\":"<<height<<",\"mode\":\""<<modeNames[mode]<<"\",\"frames\":"<<cpus.size()<<",\"cpu_median\":"<<cm<<",\"cpu_p95\":"<<cp<<",\"wall_median\":"<<wm<<",\"wall_p95\":"<<wp<<",\"draws\":"<<counts.draws<<",\"pipeline_switches\":"<<counts.switches<<",\"clears\":"<<counts.clears<<",\"clipper_median\":"<<quantile(clipperTimes,.5)<<",\"earcut_median\":"<<quantile(earcutTimes,.5)<<",\"draw_submit_median\":"<<quantile(drawTimes,.5)<<",\"cache_ms\":"<<cacheMs<<",\"cache_clipper_ms\":"<<cacheTiming.clipperMs<<",\"cache_earcut_ms\":"<<cacheTiming.earcutMs<<",\"max_diff\":"<<maximum<<",\"mean_abs_diff\":"<<mad<<",\"pixels_over_2\":"<<bad<<"}"; json.flush();
        if(mode>0 && mode<4 && bad) throw std::runtime_error("stencil/reference mismatch");
    }
public:
    void setup() override {
        shader=sg_make_shader(stroke_overlap_triangles_shader_desc(sg_query_backend()));
        if(sg_query_shader_state(shader)!=SG_RESOURCESTATE_VALID) throw std::runtime_error("shader creation failed");
        backend=sg_query_backend()==SG_BACKEND_GLCORE?"GLCORE":sg_query_backend()==SG_BACKEND_D3D11?"D3D11":"other";
#if defined(__linux__) && defined(SOKOL_GLCORE)
        renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
#elif defined(_WIN32) && defined(SOKOL_D3D11)
        renderer="D3D11 adapter query unavailable";
        auto device=static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
        IDXGIDevice* dxgiDevice=nullptr;
        if(device && SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgiDevice)))) {
            IDXGIAdapter* adapter=nullptr;
            if(SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
                DXGI_ADAPTER_DESC description={};
                if(SUCCEEDED(adapter->GetDesc(&description))) {
                    char utf8[1024]={};
                    WideCharToMultiByte(CP_UTF8,0,description.Description,-1,utf8,sizeof(utf8),nullptr,nullptr);
                    renderer=utf8;
                }
                adapter->Release();
            }
            dxgiDevice->Release();
        }
#else
        renderer="unavailable (record device/driver externally)";
#endif
        json.open(output); if(!json) throw std::runtime_error("cannot open JSON output");
        json<<"{\"backend\":"<<std::quoted(backend)<<",\"renderer\":"<<std::quoted(renderer)<<",\"note\":\"llvmpipe is a CPU rasterizer; fill costs exaggerated. 30 warmup, up to 300 samples; 60s case budget includes warmup. Initial target clear excluded. Switch counts include initial pipeline bind and explicit clears, exclude common setup/restoration.\",\"unavailable\":"<<(strokeOverlap::unionAvailable()?"[]":"[\"union_frame\",\"union_cached\"]")<<",\"results\":[\n";
        std::printf("BACKEND %s RENDERER %s\nllvmpipe is a CPU rasterizer; fill costs are exaggerated.\n| scene | resolution | mode | frames | CPU median/p95 ms | wall median/p95 ms | draws | switches | diff max/MAD/pixels>2 |\n",backend.c_str(),renderer.c_str());
        resolution=resolutionFilter==1080?1:0;
    }
    void draw() override {
        try {
            if(!configured) configure();
            if(!referenceReady) { makeReferenceStep(); return; }
            if(frame==0) caseStart=Clock::now();
            beginFrame(); auto start=Clock::now(); Counts counts=record(); target.end(); double cpu=elapsed(start); complete(target); double wall=elapsed(start);
            if(frame>=30) { cpus.push_back(cpu); walls.push_back(wall); clipperTimes.push_back(frameTiming.clipperMs); earcutTimes.push_back(frameTiming.earcutMs); drawTimes.push_back(cpu-frameTiming.clipperMs-frameTiming.earcutMs); }
            ++frame;
            if(frame<330 && (elapsed(caseStart)<60000 || cpus.empty())) return;
            saveResult(counts); frame=0; cpus.clear(); walls.clear(); clipperTimes.clear(); earcutTimes.clear(); drawTimes.clear();
            ++mode;
            if(mode==4 && !strokeOverlap::unionAvailable()) mode=6;
            if(mode==6) {
                mode=0; configured=false;
                if(++scene==3) { scene=0; ++resolution; }
                if(resolution==2 || (resolutionFilter==480 && resolution==1)) { json<<"\n]}\n"; json.close(); exitApp(); }
            }
        } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); exitApp(1); }
    }
};
}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i) {
        std::string arg=argv[i];
        if(arg=="--output" && i+1<argc) output=fs::absolute(argv[++i]).string();
        else if(arg=="--images" && i+1<argc) images=fs::absolute(argv[++i]).string();
        else if(arg=="--resolution" && i+1<argc) { resolutionFilter=std::atoi(argv[++i]); if(resolutionFilter!=480 && resolutionFilter!=1080) return 2; }
        else return 2;
    }
    if(output.empty()) output=(fs::absolute(argv[0]).parent_path()/"results.json").string();
    if(!images.empty()) fs::create_directories(images);
    WindowSettings s; s.setSize(640,480).setHighDpi(false).setSampleCount(1).setSwapInterval(0).setPipelinePoolSize(4096);
    return TC_RUN_APP(Bench,s);
}
