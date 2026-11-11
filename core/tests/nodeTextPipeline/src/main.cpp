// #1432: frozen text/Shader/PBR benchmark and temporary-pipeline restoration.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "pipeline.glsl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace tc;
namespace {
int failures = 0;
bool completed = false, benchmark = false;
int measuredFrames = 240;
const char* benchmarkMode = "text";
void check(const char* label, bool passed) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", label);
    if (!passed) ++failures;
}
class DrawTree : public Node {
public:
    using Node::drawChildren;
};
class DrawNode : public Node {
    std::function<void()> draw_;
public:
    explicit DrawNode(std::function<void()> draw) : draw_(std::move(draw)) {}
    void draw() override { draw_(); }
};
void add(Node& tree, std::function<void()> draw) {
    tree.addChild(std::make_shared<DrawNode>(std::move(draw)));
}
void expect(const Pixels& pixels, int x, int y, Color wanted, const char* label) {
    const auto actual = pixels.getColor(x, y);
    std::printf("PIXEL %s (%d,%d) rgba=%.0f,%.0f,%.0f,%.0f\n",label,x,y,actual.r*255,actual.g*255,actual.b*255,actual.a*255);
    auto matches = [](float a, float b) { return std::abs(a-b) <= 2.f/255; };
    check(label, matches(actual.r,wanted.r) && matches(actual.g,wanted.g) && matches(actual.b,wanted.b));
}
class PipelineApp : public App {
    Font font_;
    Shader shader_;
    Mesh mesh_;
    Material material_;
    ShaderVertex triangle_[3] = {};
    std::shared_ptr<DrawTree> tree_ = std::make_shared<DrawTree>();
    int frame_ = 0;
    std::vector<double> treeTimes_, frameTimes_;
    std::vector<int> commands_;
    std::chrono::steady_clock::time_point frameStart_;
    EventListener after_;
    static double median(std::vector<double> values) {
        std::sort(values.begin(),values.end());
        const auto mid=values.size()/2;
        return (values[mid]+values[(values.size()-1)/2])*0.5;
    }
    static double p95(std::vector<double> values) {
        std::sort(values.begin(), values.end());
        return values[size_t(std::ceil(values.size() * 0.95)) - 1];
    }
    void correctness() {
        check("custom shader loads", shader_.load(node_pipeline_mesh_shader_desc));
        Texture blue;
        const unsigned char texel[] = {0,0,255,255};
        blue.allocate(1,1,TextureFormat::RGBA8,TextureUsage::Dynamic);
        blue.loadData(texel,1,1,4);
        for (int samples : {1,4}) for (auto format : {TextureFormat::RGBA8,TextureFormat::RGBA16F})
        for (bool depth : {false,true}) {
            Fbo outer, inner, nested;
            outer.allocate(128,96,samples,format);
            inner.allocate(24,24); nested.allocate(8,8);
            outer.begin(0.25f,0.25f,0.25f);
            setupScreenOrtho();
            if (depth) enableDepthTest(); else disableDepthTest();
            const auto fill = internal::activeFill2D();
            const auto coverage = internal::activeCoverage2D();
            auto& target = internal::currentTarget();
            check("immediate helpers match canonical recipes",
                fill.id == target.pipeline(internal::PipelineKey::immediate(internal::fillSelection(BlendMode::Alpha,depth))).immediate.id &&
                coverage.id == target.pipeline(internal::PipelineKey::immediate({internal::PipelineRole::Coverage,BlendMode::Alpha,depth})).immediate.id);
            auto modified = internal::PipelineKey::immediate(internal::fillSelection(BlendMode::Alpha,depth));
            modified.desc.colors[0].blend.enabled = false;
            const auto custom = target.pipeline(modified).immediate;
            check("modified immediate recipes remain separate",custom.id != fill.id && internal::activeFill2D().id == fill.id);
            auto nodes = std::make_shared<DrawTree>();
            add(*nodes,[&] {
                setColor(1,0,0); font_.drawString("A",4,4);
                // Leave a temporary coverage pipeline loaded, even though Font
                // currently restores it itself. Blue has R=0, A=1: the next
                // filled quad distinguishes coverage from Fill in pixels.
                internal::loadTemporaryPipeline(internal::activeCoverage2D());
                sgl_enable_texture(); sgl_texture(blue.getView(),blue.getSampler());
            });
            add(*nodes,[] { setColor(1); drawRect(32,4,16,16); sgl_disable_texture(); });
            add(*nodes,[] { setBlendMode(BlendMode::Add); setColor(1,0,0,0.5f); drawRect(56,4,16,16); });
            add(*nodes,[] { setColor(0,1,0,0.5f); drawRect(80,4,16,16); });
            add(*nodes,[&] { setColor(1,0,0); font_.drawString("A",4,36); });
            add(*nodes,[&] {
                shader_.begin();
                ShaderVertex vertices[] = {
                    {-0.5f,0.25f,0,0,0,0,0,1,1}, {-0.25f,0.25f,0,0,0,0,0,1,1},
                    {-0.5f,-0.08333333f,0,0,0,0,0,1,1}, {-0.25f,-0.08333333f,0,0,0,0,0,1,1}};
                shader_.submitVertices(vertices,4,PrimitiveType::TriangleStrip);
                shader_.end();
            });
            add(*nodes,[&] { setColor(1,0,0); font_.drawString("A",56,36); });
            add(*nodes,[&] {
                inner.begin(0,0,0); setupScreenOrtho();
                setColor(0,1,0); drawRect(0,0,24,24);
                nested.begin(0,0,1); nested.end();
                setColor(1); nested.draw(0,0); inner.end();
                setColor(1); inner.draw(80,36);
            });
            add(*nodes,[] { setColor(1,0,0,0.5f); drawRect(104,36,16,16); });
            nodes->drawChildren(); outer.end();
            Pixels pixels; pixels.allocate(128,96,4);
            if(format==TextureFormat::RGBA16F) {
                Pixels floats; floats.allocate(128,96,4,PixelFormat::F32);
                check("float readback",outer.readPixelsFloat(floats.getDataF32()));
                for(int y=0;y<96;++y) for(int x=0;x<128;++x) pixels.setColor(x,y,floats.getColor(x,y));
            } else check("byte readback",outer.readPixels(pixels.getData()));
            uint64_t hash = 14695981039346656037ULL;
            for (size_t i=0;i<128*96*4;++i) hash = (hash ^ pixels.getData()[i])*1099511628211ULL;
            std::printf("PIXELS samples=%d float=%d depth=%d hash=%llu\n",samples,
                int(format==TextureFormat::RGBA16F),int(depth),static_cast<unsigned long long>(hash));
            expect(pixels,40,12,Color(0,0,1),"text then Fill restores straight texture sampling");
            expect(pixels,64,12,Color(0.75f,0.25f,0.25f),"node blend changes to Add");
            expect(pixels,88,12,Color(0.125f,0.625f,0.125f),"next node resets Add to Alpha");
            expect(pixels,40,44,Color(0,0,1),"custom Shader between text nodes");
            expect(pixels,88,44,Color(0,1,0),"nested Fbo returns to parent target");
            expect(pixels,84,40,Color(0,0,1),"nested Fbo preserves its own pixels");
            expect(pixels,112,44,Color(0.625f,0.125f,0.125f),"Fill after Fbo return");
            int redBefore=0, redAfter=0;
            bool textMatches=true;
            for(int y=36;y<56;++y) for(int x=4;x<24;++x) {
                auto a=pixels.getColor(x,y), b=pixels.getColor(x+52,y);
                redBefore += a.r>0.8f && a.g<0.1f;
                redAfter += b.r>0.8f && b.g<0.1f;
                textMatches &= std::abs(a.r-b.r)<=2.f/255 && std::abs(a.g-b.g)<=2.f/255 &&
                    std::abs(a.b-b.b)<=2.f/255 && std::abs(a.a-b.a)<=2.f/255;
            }
            check("text survives both sides of Shader",redBefore>0 && redAfter>0 && textMatches);
        }
        disableDepthTest();
        // Loads alone must preserve batching when matrices/textures agree.
        // Node transforms have their own matrix-driven command boundaries.
        resetStyle(); font_.drawString("A",0,0);
        const int before=sgl_num_commands();
        for(int i=1;i<=32;++i) { resetStyle(); font_.drawString("A",float(i*12),0); }
        check("consecutive same-matrix text batches across style resets",sgl_num_commands()==before);
    }
public:
    void setup() override {
        setFps(VSYNC);
        check("synthetic font loads",font_.load(fs::path(__FILE__).parent_path()/"../../renderTargetState/src/fixture.ttf",16).ok());
        if (benchmark) {
            check("benchmark shader loads", shader_.load(node_pipeline_mesh_shader_desc));
            mesh_.addVertex(0,0,0).addVertex(4,0,0).addVertex(0,4,0);
            for (int i=0; i<3; ++i) mesh_.addNormal(0,0,1);
            material_.setEmissive(0.25f,0.5f,0.75f);
        }
        // One glyph per node fits the default immediate vertex budget.
        for(int i=0;i<3000;++i) {
            auto node=std::make_shared<DrawNode>([this] { font_.drawString("A",0,0); });
            node->setPos(float(i%100)*6,float(i/100)*8);
            tree_->addChild(node);
        }
        after_=events().afterFrame.listen([this] {
            if(!benchmark) return;
            if(frame_>30) frameTimes_.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart_).count());
            if(frame_==30+measuredFrames) {
                std::vector<double> counts(commands_.begin(),commands_.end());
                std::printf("BENCH mode=%s draws=3000 frames=%d submit_median_ms=%.6f submit_p95_ms=%.6f frame_median_ms=%.6f frame_p95_ms=%.6f draw_commands_median=%.0f\n",
                    benchmarkMode,measuredFrames,median(treeTimes_),p95(treeTimes_),median(frameTimes_),p95(frameTimes_),median(counts));
                completed=true; exitApp();
            }
        });
    }
    void draw() override {
        if(!benchmark) { correctness(); completed=true; exitApp(); return; }
        frameStart_=std::chrono::steady_clock::now();
        clear(0,0,0); setupScreenOrtho(); disableDepthTest();
        const int before=sgl_num_commands();
        const auto started=std::chrono::steady_clock::now();
        if (std::strcmp(benchmarkMode,"text")==0) tree_->drawChildren();
        else if (std::strcmp(benchmarkMode,"shader")==0) {
            shader_.begin();
            for (int i=0; i<3000; ++i) {
                const float x=-1.f+float(i%100)*0.02f, y=-1.f+float(i/100)*0.06f;
                triangle_[0]={x,y,0,0,0,1,0,0,1};
                triangle_[1]={x+0.01f,y,0,0,0,1,0,0,1};
                triangle_[2]={x,y+0.03f,0,0,0,1,0,0,1};
                shader_.submitVertices(triangle_,3,PrimitiveType::Triangles);
            }
            shader_.end();
        } else {
            setMaterial(material_);
            for (int i=0; i<3000; ++i) {
                pushMatrix(); translate(float(i%100)*6,float(i/100)*8); mesh_.draw(); popMatrix();
            }
            clearMaterial();
        }
        const auto ended=std::chrono::steady_clock::now();
        ++frame_;
        if(frame_>30) {
            treeTimes_.push_back(std::chrono::duration<double,std::milli>(ended-started).count());
            const auto& target=internal::currentTarget();
            commands_.push_back(sgl_num_commands()-before + int(target.deferredShaderDraws.size()) + int(target.deferredPbrDraws.size()));
        }
    }
};
}
TC_CORE_TEST_MAIN(int argc,char** argv) {
    if(argc==1) { std::puts("SKIP: use --gpu-check or --benchmark [frames] [text|shader|pbr]"); return 0; }
    benchmark=std::strcmp(argv[1],"--benchmark")==0;
    if(benchmark && argc>2) measuredFrames=std::max(1,std::atoi(argv[2]));
    if(benchmark && argc>3) benchmarkMode=argv[3];
    if(benchmark && std::strcmp(benchmarkMode,"text") && std::strcmp(benchmarkMode,"shader") && std::strcmp(benchmarkMode,"pbr")) {
        std::puts("FAIL: benchmark mode must be text, shader, or pbr"); return 1;
    }
    WindowSettings settings; settings.setSize(640,256); settings.sampleCount=1;
    return tcCoreTest::runDisplayTest<PipelineApp>(settings,failures,[] { check("suite completed",completed); });
}
