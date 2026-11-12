// #1436: literal custom blend equations, state and warmed selection cost.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <unordered_set>
using namespace tc;
namespace {
int failures = 0;
bool completed = false;
void check(const char* label, bool passed) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", label);
    if (!passed) ++failures;
}
bool approxEq(Color a, Color b) {
    return std::abs(a.r-b.r) <= 3.f/255 && std::abs(a.g-b.g) <= 3.f/255 &&
        std::abs(a.b-b.b) <= 3.f/255 && std::abs(a.a-b.a) <= 3.f/255;
}
BlendDesc maximumDesc() {
    BlendDesc desc;
    desc.rgb = {BlendFactor::One, BlendFactor::One, BlendOp::Maximum};
    desc.alpha = {BlendFactor::Zero, BlendFactor::One};
    return desc;
}
constexpr BlendDesc premultiply(BlendDesc desc) { desc.premultiplySource = true; return desc; }
const BlendDesc descriptors[] = {BlendDesc::premultipliedAdd(), BlendDesc::addKeepAlpha(),
    BlendDesc::screenKeepAlpha(), maximumDesc(), premultiply(BlendDesc::premultipliedAdd()),
    premultiply(BlendDesc::screenKeepAlpha())};
const char* names[] = {"premultipliedAdd", "addKeepAlpha", "screenKeepAlpha", "Maximum",
    "premultipliedAdd + premultiplySource", "screenKeepAlpha + premultiplySource"};
constexpr int width = 48 * std::size(descriptors);
// Independent numeric oracle: convert source RGB only when explicitly requested.
Color expected(int preset, Color source, Color destination) {
    if (descriptors[preset].premultiplySource) {
        source.r*=source.a; source.g*=source.a; source.b*=source.a;
        preset = preset==4 ? 0 : 2;
    }
    if (preset == 0) return Color(source.r+destination.r, source.g+destination.g,
        source.b+destination.b, source.a+destination.a);
    if (preset == 1) return Color(source.r*source.a+destination.r, source.g*source.a+destination.g,
        source.b*source.a+destination.b, destination.a);
    if (preset == 2) return Color(source.r*(1-destination.r)+destination.r,
        source.g*(1-destination.g)+destination.g, source.b*(1-destination.b)+destination.b, destination.a);
    return Color(std::fmax(source.r,destination.r), std::fmax(source.g,destination.g),
        std::fmax(source.b,destination.b), destination.a);
}
void expect(Color actual, Color wanted, const char* label) {
    std::printf("PIXEL %s actual=%.5f,%.5f,%.5f,%.5f expected=%.5f,%.5f,%.5f,%.5f\n",
        label,actual.r,actual.g,actual.b,actual.a,wanted.r,wanted.g,wanted.b,wanted.a);
    check(label,approxEq(actual,wanted));
}
void valueChecks() {
    static_assert(sizeof(BlendDesc) == 7);
    check("presets leave premultiplySource off", !descriptors[0].premultiplySource &&
        !descriptors[1].premultiplySource && !descriptors[2].premultiplySource);
    check("default slot is straight Alpha", getCustomBlendDesc() == BlendDesc{});
    setCustomBlendDesc(descriptors[0]);
    check("storing slot does not activate Custom",getBlendMode()==BlendMode::Alpha && getCustomBlendDesc()==descriptors[0]);
    setBlendMode(BlendMode::Custom);
    pushStyle();
    setCustomBlendDesc(descriptors[1]);
    check("active Custom setter updates selection",getBlendMode()==BlendMode::Custom &&
        internal::currentTarget().selectedPipeline.customBlendId==internal::blendDescId(descriptors[1]));
    setBlendMode(BlendMode::Add);
    pushStyle(); resetStyle();
    check("resetStyle resets slot and mode",getBlendMode()==BlendMode::Alpha && getCustomBlendDesc()==BlendDesc{});
    popStyle();
    check("popStyle restores inactive slot",getBlendMode()==BlendMode::Add && getCustomBlendDesc()==descriptors[1]);
    popStyle();
    check("popStyle restores active slot",getBlendMode()==BlendMode::Custom && getCustomBlendDesc()==descriptors[0]);
    for (auto mode : {BlendMode::Alpha,BlendMode::Add,BlendMode::Multiply,BlendMode::Screen,
                      BlendMode::Subtract,BlendMode::Disabled,BlendMode::Custom}) {
        setBlendMode(mode);
        check("set/get mode symmetry and persistent slot",getBlendMode()==mode && getCustomBlendDesc()==descriptors[0] &&
            internal::currentTarget().customBlendId==internal::blendDescId(descriptors[0]));
    }
    int warnings = 0;
    auto listener=getLogger().onLog.listen([&](LogEventArgs& event) {
        if(event.level==LogLevel::Warning && event.message.find("invalid custom blend")!=std::string::npos) ++warnings;
    });
    BlendDesc invalid[] = {descriptors[0],descriptors[0],descriptors[0],descriptors[0],descriptors[0],descriptors[0]};
    invalid[0].rgb.src=BlendFactor(255); invalid[1].rgb.op=BlendOp(255);
    invalid[2].alpha.dst=BlendFactor::DstColor; invalid[3].alpha.src=BlendFactor::SrcColor;
    invalid[4].rgb={BlendFactor::SrcAlpha,BlendFactor::One,BlendOp::Maximum};
    invalid[5].alpha={BlendFactor::Zero,BlendFactor::One,BlendOp::Minimum};
    for(const auto& desc:invalid) {
        setCustomBlendDesc(desc);
        check("rejected descriptor retains mode, slot and selection",getBlendMode()==BlendMode::Custom &&
            getCustomBlendDesc()==descriptors[0] && internal::currentTarget().selectedPipeline.customBlendId==internal::blendDescId(descriptors[0]));
    }
    check("every invalid setter warns",warnings==6);
    // Exhaust the portable value domain and prove the compact identity is exact.
    std::unordered_set<uint32_t> ids;
    bool roundTrips=true, unique=true, mappings=true;
    const sg_blend_factor factors[]={SG_BLENDFACTOR_ZERO,SG_BLENDFACTOR_ONE,SG_BLENDFACTOR_SRC_COLOR,
        SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR,SG_BLENDFACTOR_SRC_ALPHA,SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SG_BLENDFACTOR_DST_COLOR,SG_BLENDFACTOR_ONE_MINUS_DST_COLOR,SG_BLENDFACTOR_DST_ALPHA,SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA};
    const sg_blend_op ops[]={SG_BLENDOP_ADD,SG_BLENDOP_SUBTRACT,SG_BLENDOP_REVERSE_SUBTRACT,SG_BLENDOP_MIN,SG_BLENDOP_MAX};
    for(int i=0;i<10;++i) mappings &= internal::blendFactor(BlendFactor(i))==factors[i];
    for(int i=0;i<5;++i) mappings &= internal::blendOp(BlendOp(i))==ops[i];
    for(int rs=0;rs<10;++rs) for(int rd=0;rd<10;++rd) for(int ro=0;ro<5;++ro)
    for(int as=0;as<10;++as) for(int ad=0;ad<10;++ad) for(int ao=0;ao<5;++ao) {
        BlendDesc d{{BlendFactor(rs),BlendFactor(rd),BlendOp(ro)},{BlendFactor(as),BlendFactor(ad),BlendOp(ao)}};
        if(!internal::validBlendDesc(d)) continue;
        for (bool convert : {false,true}) {
            d.premultiplySource=convert;
            const auto id=internal::blendDescId(d);
            unique &= ids.insert(id).second;
            roundTrips &= internal::blendDescFromId(id)==d;
        }
    }
    check("all portable descriptor identities include premultiplySource and round trip",unique && roundTrips && ids.size()==66440);
    check("all factor and operation mappings",mappings);
    setCustomBlendDesc(descriptors[5]);
    const auto convertedSelection=internal::currentTarget().selectedPipeline;
    pushStyle(); resetStyle(); popStyle();
    check("Style restores premultiplySource and its selection identity",getCustomBlendDesc()==descriptors[5] &&
        internal::currentTarget().selectedPipeline==convertedSelection);
    setCustomBlendDesc(descriptors[2]);
    check("flag alone changes selection identity",internal::currentTarget().selectedPipeline!=convertedSelection &&
        internal::PipelineSelectionId::immediate(convertedSelection)!=
            internal::PipelineSelectionId::immediate(internal::currentTarget().selectedPipeline));
    check("Custom tests depth without writing",!internal::pipeDesc2D(BlendMode::Custom,true,{}).depth.write_enabled &&
        internal::pipeDesc2D(BlendMode::Custom,true,{}).depth.compare==SG_COMPAREFUNC_LESS_EQUAL);
    resetStyle();
}
class ValueApp : public App {
public:
    void setup() override { valueChecks(); }
    void update() override { exitApp(); }
};
class CustomBlendApp : public App {
    Font font_;
    GlyphBatch glyphs_;
    Texture texture_;
    Fbo source_;
    Pixels textMask_;
    int frame_ = 0;
    std::vector<double> customTimes_, standardTimes_;
    static constexpr int draws = 4000, frames = 30;
    const Color sourceColor_{0.2f,0.1f,0.3f,0.25f};
    const Color destination_{0.1f,0.3f,0.2f,0.4f};
    void drawCases() {
        for(int i=0;i<int(std::size(descriptors));++i) {
            setCustomBlendDesc(descriptors[i]); setBlendMode(BlendMode::Custom);
            setColor(sourceColor_); drawRect(float(i*48),0,24,24);
            setColor(1); texture_.draw(float(i*48),24,24,24);
            source_.draw(float(i*48),48,24,24);
            // One text case per preset, including fractional/zero glyph coverage.
            setColor(sourceColor_); font_.drawString("A",float(i*48),80);
            drawBitmapString("A",float(i*48),104);
            setColor(sourceColor_.r,sourceColor_.g,sourceColor_.b,0); drawRect(float(i*48),128,24,24);
            glyphs_.draw(Mat4::translate(float(i*48),152,0));
        }
    }
    void checkCases(const Pixels& pixels, float scale, const char* target) {
        std::printf("TARGET %s\n",target);
        for(int i=0;i<int(std::size(descriptors));++i) {
            for(int row : {0,1,2,5}) {
                const int actualY=row==5 ? 136 : row*24+8;
                auto source=sourceColor_;
                if(row==2) { source.r*=source.a; source.g*=source.a; source.b*=source.a; }
                if(row==5) source.a=0;
                expect(pixels.getColor(int((i*48+8)*scale),int(actualY*scale)),expected(i,source,destination_),names[i]);
            }
            // The overwrite reference records shader coverage in alpha and
            // marks the entire emitted quad with white RGB, including alpha=0.
            for(int text=0;text<3;++text) {
                bool matches=true; int ink=0, transparent=0;
                for(int y=0;y<24;++y) for(int x=0;x<24;++x) {
                    const auto mask=textMask_.getColor(x,y+text*24);
                    auto source=sourceColor_; source.a*=mask.a;
                    const auto wanted=mask.r>0.5f ? expected(i,source,destination_) : destination_;
                    const auto c=pixels.getColor(int((i*48+x)*scale),int(((text==2 ? 152 : 80+text*24)+y)*scale));
                    matches &= approxEq(c,wanted);
                    ink+=mask.a>0;
                    if(mask.r>0.5f && mask.a==0) {
                        ++transparent;
                        if(descriptors[i].premultiplySource) matches &= approxEq(c,destination_);
                    }
                }
                check(text==0 ? "Font text follows literal custom blend and coverage" :
                    text==1 ? "bitmap text follows literal custom blend and coverage" :
                    "GlyphBatch follows draw-time custom blend and coverage", matches && ink>0 && transparent>0);
            }
        }
        for (auto pair : {std::pair{0,4}, std::pair{2,5}}) {
            const auto once=pixels.getColor(int((pair.first*48+8)*scale),int(56*scale));
            const auto twice=pixels.getColor(int((pair.second*48+8)*scale),int(56*scale));
            check("Fbo premultiplySource on is darker from double premultiplication",
                twice.r<once.r && twice.g<once.g && twice.b<once.b);
        }
    }
    void gpuChecks() {
        Fbo mask; mask.allocate(24,72);
        mask.begin(0,0,0,0); setupScreenOrtho();
        BlendDesc overwrite{{BlendFactor::One,BlendFactor::Zero},{BlendFactor::One,BlendFactor::Zero}};
        setCustomBlendDesc(overwrite); setBlendMode(BlendMode::Custom);
        setColor(1); font_.drawString("A",0,0); drawBitmapString("A",0,24);
        GlyphBatch maskGlyphs; maskGlyphs.setFont(font_); maskGlyphs.add("A",Color(1));
        maskGlyphs.draw(Mat4::translate(0,48,0)); mask.end();
        textMask_.allocate(24,72,4); check("text coverage reference",mask.readPixels(textMask_.getData()));
        // Alpha drawing stores premultiplied RGB; Custom uses those values once.
        source_.begin(0,0,0,0); setBlendMode(BlendMode::Alpha); setColor(sourceColor_);
        drawRect(0,0,24,24); source_.end();
        for(int samples : {1,4}) {
            Fbo fbo; fbo.allocate(width,184,samples);
            resetStyle(); setCustomBlendDesc(descriptors[1]); setBlendMode(BlendMode::Custom);
            fbo.begin(destination_.r,destination_.g,destination_.b,destination_.a);
            setupScreenOrtho(); disableDepthTest();
            check("Fbo entry inherits mode and slot",getBlendMode()==BlendMode::Custom && getCustomBlendDesc()==descriptors[1]);
            drawCases();
            fbo.end();
            check("Fbo return propagates mode and slot",getBlendMode()==BlendMode::Custom && getCustomBlendDesc()==descriptors[5]);
            Pixels pixels; pixels.allocate(width,184,4);
            check("Fbo readback",fbo.readPixels(pixels.getData()));
            checkCases(pixels,1,samples==1 ? "Fbo" : "MSAA Fbo");
        }
        resetStyle(); setupScreenOrtho(); disableDepthTest();
        clear(destination_); drawCases();
        Pixels screen; check("swapchain readback",grabScreen(screen));
        if(screen.isAllocated()) checkCases(screen,float(screen.getWidth())/width,"swapchain");
        resetStyle();
        setBlendMode(BlendMode::Custom); setCustomBlendDesc(descriptors[0]);
        const auto saved=internal::currentTarget().selectedPipeline;
        EasyCam camera; camera.begin(); camera.end();
        check("camera restores complete custom selection",internal::currentTarget().selectedPipeline==saved);
        Fbo nested; nested.allocate(8,8);
        nested.begin(); setBlendMode(BlendMode::Alpha); setCustomBlendDesc(descriptors[2]); nested.end();
        check("inactive Fbo slot propagates too",getBlendMode()==BlendMode::Alpha && getCustomBlendDesc()==descriptors[2]);
        setBlendMode(BlendMode::Custom);
        check("inactive Fbo slot retains packed identity",internal::currentTarget().selectedPipeline.customBlendId==internal::blendDescId(descriptors[2]));
        resetStyle();
    }

    double alternate(bool custom) {
        auto& target=internal::currentTarget();
        if(custom) setBlendMode(BlendMode::Custom);
        const auto started=std::chrono::steady_clock::now();
        for(int i=0;i<draws;++i) {
            if(custom) setCustomBlendDesc(descriptors[i%2 ? 5 : 2]);
            else setBlendMode(i%2 ? BlendMode::Alpha : BlendMode::Add);
            setColor(0.01f,0.02f,0.03f,0.05f);
            drawRect(float(i%192),float((i/192)%160),1,1);
        }
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        // Exercise Coverage/Fill variants and thousands of reset/reselect nodes.
        if(custom) for(int i=0;i<2000;++i) {
            resetStyle(); setCustomBlendDesc(descriptors[i%2 ? 5 : 2]); setBlendMode(BlendMode::Custom);
            font_.drawString("A",0,0);
            if(i<32) glyphs_.draw();
        }
        check("custom values stay outside the finite standard cache",std::none_of(
            target.resources().immediateCache.begin(),target.resources().immediateCache.end(),
            [](const auto& entry) { return entry.first.blend==BlendMode::Custom; }));
        return elapsed;
    }
public:
    void setup() override {
        setFps(VSYNC);
        check("font fixture loads",font_.load(fs::path(__FILE__).parent_path()/"../../renderTargetState/src/fixture.ttf",16).ok());
        const unsigned char texel[]={51,26,77,64};
        texture_.allocate(1,1,TextureFormat::RGBA8,TextureUsage::Dynamic); texture_.loadData(texel,1,1,4);
        source_.allocate(24,24);
        glyphs_.setFont(font_); glyphs_.add("A",sourceColor_);
    }
    void draw() override {
        if(frame_==0) { valueChecks(); gpuChecks(); }
        clear(0,0,0); setupScreenOrtho(); disableDepthTest();
        auto& resources=internal::currentTarget().resources();
        const auto lookups=resources.canonicalLookups, pipelines=resources.cache.size();
        const auto rawPipelines=resources.rawCache->cache.size();
        const bool custom=frame_%2==0;
        const double elapsed=alternate(custom);
        if(frame_>=2) {
            check("alternation has zero canonical lookups after warm-up",resources.canonicalLookups==lookups);
            check("alternation creates zero pipelines after warm-up",resources.cache.size()==pipelines && resources.rawCache->cache.size()==rawPipelines);
            (custom ? customTimes_ : standardTimes_).push_back(elapsed);
        }
        ++frame_;
        if(frame_==frames+2) {
            auto median=[](std::vector<double> values) { std::sort(values.begin(),values.end()); return values[values.size()/2]; };
            std::printf("BENCH draws=%d custom_median_ms=%.6f Add_Alpha_median_ms=%.6f ratio=%.3f canonical_lookups=%llu pipelines=%zu\n",
                draws,median(customTimes_),median(standardTimes_),median(customTimes_)/median(standardTimes_),
                static_cast<unsigned long long>(resources.canonicalLookups),resources.cache.size());
            completed=true; exitApp();
        }
    }
};
}
TC_CORE_TEST_MAIN(int argc,char** argv) {
    if(argc==1) { runHeadlessApp<ValueApp>(); return failures ? 1 : 0; }
    WindowSettings settings; settings.setSize(width,184); settings.setHighDpi(false); settings.sampleCount=argc>2 && std::strcmp(argv[2],"--msaa4")==0 ? 4 : 1;
    return tcCoreTest::runDisplayTest<CustomBlendApp>(settings,failures,[] { check("suite completed",completed); });
}
