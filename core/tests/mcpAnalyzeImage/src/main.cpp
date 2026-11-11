#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>

using namespace tc;
using namespace std;
namespace {
int failures=0;
bool completed=false;
string evidenceDirectory;
void check(const char* name,bool ok) {
    printf("%-65s %s\n",name,ok?"PASS":"FAIL");
    if(!ok) ++failures;
}
json decode(const string& response) {
    const auto r=json::parse(response);
    return json::parse(r.at("result").at("content").at(0).at("text").get<string>());
}
string request(const string& tool,const json& args=json::object()) {
    return mcp::internal::Server::instance().processMessage(json({{"jsonrpc","2.0"},{"id",1},{"method","tools/call"},
        {"params",{{"name",tool},{"arguments",args}}}}).dump());
}
json call(const string& tool,const json& args=json::object()) {return decode(request(tool,args));}
json analyze(const json& source,const json& ops,json save=nullptr) {
    return call("tc_analyze_image",{{"source",source},{"ops",ops},{"save",save}});
}
json op(const char* name) {return {{"op",name}};}
bool approxEq(double a,double b) {return abs(a-b)<1e-6;}
json named(const json& list,const char* key,const char* name) {
    for(const auto& e:list.at(key)) if(e.at("name")==name) return e;
    return nullptr;
}
void headlessChecks() {
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    const auto schema=json::parse(mcp::internal::Server::instance().processMessage(
        json{{"jsonrpc","2.0"},{"id",1},{"method","tools/list"}}.dump()));
    for(const auto& t:schema["result"]["tools"]) if(t["name"]=="tc_analyze_image") {
        check("MCP exposes source, ops, optional nullable save only",
            t["inputSchema"]["properties"].size()==3 &&
            t["inputSchema"]["properties"]["ops"]["type"]=="array" &&
            t["inputSchema"]["properties"]["save"]["type"]==json::array({"string","null"}));
    }
    bool stencilListed = false;
    for (const auto& t : schema["result"]["tools"]) if (t["name"] == "tc_get_stencil") {
        stencilListed = true;
        check("stencil schema accepts name/index and optional preview width",
            t["inputSchema"]["properties"].size() == 2 &&
            t["inputSchema"]["properties"]["fbo"]["type"] == json::array({"string","integer"}) &&
            t["inputSchema"]["required"] == json::array({"fbo"}));
    }
    check("stencil tool listed at ReadOnly", stencilListed);
    check("unknown stencil Fbo rejected", call("tc_get_stencil", {{"fbo","missing"}})["status"] == "error");
    check("window stencil target rejected clearly", call("tc_get_stencil", {{"window",1}})["message"].get<string>().find("window/main") != string::npos);
    check("missing stencil target rejected", call("tc_get_stencil")["status"] == "error");
#ifndef __EMSCRIPTEN__
    // Width is validated before the Fbo lookup, so the message names width.
    auto widthRejected = [&](const json& width) {
        const auto r = call("tc_get_stencil", {{"fbo","missing"},{"width",width}});
        return r["status"] == "error" && r["message"].get<string>().find("width") != string::npos;
    };
    check("invalid stencil width rejected", widthRejected(0) && widthRejected(-1) &&
          widthRejected("8") && widthRejected(uint64_t(1) << 40));
#endif
    const auto dir=filesystem::temp_directory_path()/ ("tc-analyze-"+to_string(getSystemTimeMicros()));
    filesystem::create_directories(dir);
    setDataPathRoot(dir);
    Image image;
    image.setDebugName("known");
    auto& p=image.getPixels();
    p.allocate(4,3,4);
    for(int y=0;y<3;++y) for(int x=0;x<4;++x) p.setColor(x,y,Color(0,0,1,1));
    p.setColor(1,0,Color(1,0,0,1)); p.setColor(2,0,Color(1,0,0,1)); p.setColor(1,1,Color(1,0,0,1));
    json source={{"image","known"}};
    json r=analyze(source,json::array({{{"op","pixel"},{"x",1},{"y",0}}}));
    check("pixel and source size/format/color space",r["results"][0]["color"]==json::array({1,0,0,1}) &&
        r["width"]==4&&r["height"]==3&&r["format"]=="RGBA8"&&r["colorSpace"]=="sRGB");
    r=analyze(source,json::array({{{"op","count"},{"min",{0.9,0,0}},{"max",{1,0.1,0.1}}}}));
    const auto count=r["results"][0];
    check("count bbox and centroid",count["count"]==3&&count["bbox"]==json::array({1,0,2,2})&&
        approxEq(count["centroid"][0],4.0/3)&&approxEq(count["centroid"][1],1.0/3));
    r=analyze(source,json::array({{{"op","count"},{"color",{1,0,0}},{"tolerance",0.0},{"rect",{0,0,4,1}}}}));
    check("count color+tolerance honors rect",r["results"][0]["count"]==2&&r["results"][0]["centroid"]==json::array({1.5,0}));
    r=analyze(source,json::array({{{"op","count"},{"color",{0,1,0}},{"tolerance",0.0}}}));
    check("empty count has null bbox/centroid",r["results"][0]["count"]==0&&r["results"][0]["bbox"].is_null()&&r["results"][0]["centroid"].is_null());
    r=analyze(source,json::array({{{"op","histogram"},{"bins",2},{"rect",{0,0,4,1}}}}));
    check("per-channel histogram with rect",r["results"][0]["histogram"]==json::array({json::array({2,2}),json::array({4,0}),json::array({2,2}),json::array({0,4})}));
    r=analyze(source,json::array({op("stats")}));
    check("stats mean/min/max",approxEq(r["results"][0]["mean"][0],0.25)&&approxEq(r["results"][0]["mean"][2],0.75)&&
        r["results"][0]["min"]==json::array({0,0,0,1})&&r["results"][0]["max"]==json::array({1,0,1,1}));
    r=analyze(source,json::array({{{"op","grid"},{"cols",2},{"rows",1},{"rect",{0,0,4,1}}}}));
    check("grid averages cells in rect",r["results"][0]["colors"]==json::array({json::array({json::array({0.5,0,0.5,1}),json::array({0.5,0,0.5,1})})}));
    r=analyze(source,json::array({{{"op","line"},{"x0",0},{"y0",0},{"x1",3},{"y1",0},{"rect",{1,0,2,1}}}}));
    check("line includes endpoints and clips to rect",r["results"][0]["colors"]==json::array({json::array({1,0,0,1}),json::array({1,0,0,1})}));
    r=analyze(source,json::array({op("stats")}),"nested/画像.unknown");
    check("save creates parents and screenshot extension fallback",filesystem::exists(dir/"nested/画像.unknown.png"));
    r=analyze({{"path","nested/画像.unknown.png"}},json::array({op("stats")}));
    check("saved path source round trip",approxEq(r["results"][0]["mean"][0],0.25));
    p.setColor(3,2,Color(1,1,1,1));
    r=analyze(source,json::array({{{"op","diff"},{"path","nested/画像.unknown.png"},{"threshold",0.02},{"save","diff/output"}}}));
    check("diff count/bbox/max difference",r["results"][0]["count"]==1&&r["results"][0]["bbox"]==json::array({3,2,1,1})&&r["results"][0]["maxDifference"]==1);
    Pixels diff; check("diff image written",bool(diff.load(dir/"diff/output.png"))&&diff.getWidth()==4);
    r=analyze(source,json::array({{{"op","diff"},{"path","nested/画像.unknown.png"},{"threshold",1.0},{"rect",{0,0,3,2}}}}));
    check("diff rect and empty bbox",r["results"][0]["count"]==0&&r["results"][0]["bbox"].is_null()&&r["results"][0]["maxDifference"]==0);
    Image hdr; hdr.setDebugName("hdr"); hdr.getPixels().allocate(1,1,4,PixelFormat::F32);
    float* f=hdr.getPixels().getDataF32(); f[0]=2.5f;f[1]=-0.5f;f[2]=1;f[3]=1;
    r=analyze({{"image","hdr"}},json::array({{{"op","pixel"},{"x",0},{"y",0}},op("stats"),{{"op","histogram"},{"bins",2}}}));
    check("float values pass through with linear metadata",r["format"]=="float"&&r["colorSpace"]=="linear"&&
        r["results"][0]["color"]==json::array({2.5,-0.5,1,1})&&r["results"][1]["max"][0]==2.5&&r["results"][2]["histogram"][0]==json::array({0,1}));
    const auto old=named(call("tc_list_images"),"images","known");
    r=analyze({{"image",to_string(old["index"].get<uint64_t>())}},json::array({op("stats")}));
    check("string index source works",r["width"]==4);
    image.setDebugName("123");
    r=analyze({{"image","123"}},json::array({op("stats")}));
    check("numeric debug name is resolved as a name",r["width"]==4);
    image.setDebugName("known");
    {
        Image second, third;
        second.setDebugName("known"); third.setDebugName("known");
        string indices;
        const auto images=call("tc_list_images");
        for (const auto& entry : images["images"]) if (entry["name"]=="known") {
            if (!indices.empty()) indices += ", ";
            indices += to_string(entry["index"].get<uint64_t>());
        }
        r=analyze(source,json::array({op("stats")}));
        check("ambiguous Image name reports all matching indices",
            r["status"]=="error" && r["message"]==
                "debug name \"known\" is ambiguous (indices " + indices + "); use an index");
        r=analyze({{"image",old["index"]}},json::array({op("stats")}));
        check("explicit index resolves an ambiguous Image name",r["width"]==4);
    }
    {
        Fbo first, second, third;
        const string duplicate="bloom\"pass";
        first.setDebugName(duplicate); second.setDebugName(duplicate); third.setDebugName(duplicate);
        string indices;
        const auto fbos=call("tc_list_fbos");
        for (const auto& entry : fbos["fbos"]) if (entry["name"]==duplicate) {
            if (!indices.empty()) indices += ", ";
            indices += to_string(entry["index"].get<uint64_t>());
        }
        auto stencil = call("tc_get_stencil", {{"fbo",duplicate}});
        check("stencil shares ambiguous name handling", stencil["status"] == "error" &&
            stencil["message"].get<string>().find("use an index") != string::npos);
        r=analyze({{"fbo",duplicate}},json::array({op("stats")}));
        check("ambiguous Fbo name reports indices and escapes the name",
            r["status"]=="error" && r["message"]==
                "debug name " + json(duplicate).dump() + " is ambiguous (indices " + indices + "); use an index");
    }
    Image moved(std::move(image));
    check("Image move transfers debug name",moved.getDebugName()=="known"&&image.getDebugName().empty());
    check("moved-from Image is absent",call("tc_list_images")["images"].size()==2);
    r=analyze({{"image",old["index"]}},json::array({op("stats")}));
    check("Image index follows move",r["width"]==4);
    Image assigned; assigned.setDebugName("replace"); assigned=std::move(moved);
    check("Image move assignment removes old destination",call("tc_list_images")["images"].size()==2&&assigned.getDebugName()=="known");
    uint64_t deadIndex=0;
    { Image dead;dead.setDebugName("dead");deadIndex=named(call("tc_list_images"),"images","dead")["index"]; }
    check("destroyed Image disappears",named(call("tc_list_images"),"images","dead").is_null());
    check("destroyed index returns error",analyze({{"image",deadIndex}},json::array({op("stats")}))["status"]=="error");
    { Fbo a; a.setDebugName("fbo-move"); Fbo b(std::move(a));
      check("Fbo move removes shell and transfers name",call("tc_list_fbos")["fbos"].size()==1&&b.getDebugName()=="fbo-move"&&a.getDebugName().empty());
      Fbo c; c=std::move(b);check("Fbo assignment removes shell",call("tc_list_fbos")["fbos"].size()==1); }
    check("destroyed Fbo disappears",call("tc_list_fbos")["fbos"].empty());
#ifdef __EMSCRIPTEN__
    { Fbo web;web.setDebugName("web-unavailable");
      auto unsupported=analyze({{"fbo","web-unavailable"}},json::array({op("stats")}));
      check("web Fbo source reports unavailable readback",unsupported["status"]=="error"&&
          unsupported["message"].get<string>().find("unavailable on web")!=string::npos); }
#endif
    Image unnamed;
    check("unnamed Image listed",call("tc_list_images")["images"].back()["name"]=="");
    check("unknown source rejected",analyze({{"texture","known"}},json::array({op("stats")}))["status"]=="error");
    check("multiple selectors rejected",analyze({{"image","known"},{"path","a.png"}},json::array({op("stats")}))["status"]=="error");
#ifndef __EMSCRIPTEN__
    // These op errors are reported by throwing; the web build has C++
    // exception catching disabled (a throw aborts), and there is no MCP
    // server on the web, so they are checked on native builds only.
    check("out-of-bounds rect rejected",analyze({{"image","known"}},json::array({{{"op","stats"},{"rect",{3,2,2,1}}}}))["status"]=="error");
    check("invalid histogram bins rejected",analyze({{"image","known"}},json::array({{{"op","histogram"},{"bins",0}}}))["status"]=="error");
#endif
    Image gray;
    gray.setDebugName("gray");gray.getPixels().allocate(1,1,2);
    gray.getPixels().getData()[0]=128;gray.getPixels().getData()[1]=64;
    r=analyze({{"image","gray"}},json::array({{{"op","pixel"},{"x",0},{"y",0}}}));
    check("gray+alpha preserves alpha and linear metadata",r["colorSpace"]=="linear"&&approxEq(r["results"][0]["color"][3],64.0/255));
    r=analyze({{"image","hdr"}},json::array({{{"op","count"},{"min",{2,-1,0}},{"max",{3,0,1}}}}));
    check("float count matches unclamped values",r["results"][0]["count"]==1&&r["results"][0]["bbox"]==json::array({0,0,1,1}));
    r=analyze({{"image","known"}},json::array({{{"op","line"},{"x0",3},{"y0",2},{"x1",1},{"y1",0}}}));
    check("reverse diagonal line includes both ends",r["results"][0]["colors"].size()==3&&r["results"][0]["colors"][0]==json::array({1,1,1,1})&&r["results"][0]["colors"][2]==json::array({1,0,0,1}));
    check("no implicit image files",distance(filesystem::directory_iterator(dir),filesystem::directory_iterator())==2);
    filesystem::remove_all(dir);
}
#ifndef __EMSCRIPTEN__
class CaptureApp : public App {
    Fbo fbo_,hdr_,moved_,changing_,integer_,gray_,stencil_,stencilMoved_,msaa_;
    uint64_t stencilIndex_ = 0;
    vector<function<mcp::internal::ReplyThunk()>> stencilPending_;
    bool rgba16Renderable_ = false;
    TextureFormat alternate_ = TextureFormat::RGBA16;
    EventListener after_;
    vector<function<mcp::internal::ReplyThunk()>> pending_;
    void verifyStencil(const string& response, int width, int height) {
        const auto content = json::parse(response).at("result").at("content");
        check("stencil returns MCP PNG image and text metadata", content.size() == 2 &&
            content[0]["type"] == "image" && content[0]["mimeType"] == "image/png" && content[1]["type"] == "text");
        const auto meta = json::parse(content[1].at("text").get<string>());
        check("stencil metadata describes actual source and capture", meta == json({
            {"width",width},{"height",height},{"sourceWidth",16},{"sourceHeight",16},
            {"format","R8"},{"encoding","png"},{"origin","top-left"},
            {"interpretation",width == 16 ? "exact" : "preview"},
            {"frame",getFrameCount()},{"target",{{"fbo",stencilIndex_}}}}));
        const auto bytes = fromBase64(content[0].at("data").get<string>());
        check("stencil PNG is 8-bit grayscale", bytes.size() > 25 && bytes[24] == 8 && bytes[25] == 0);
        Pixels decoded;
        check("returned stencil PNG decodes", bool(decoded.loadFromMemory(bytes.data(), int(bytes.size()))));
        bool exact = decoded.getWidth() == width && decoded.getHeight() == height && decoded.getChannels() == 4;
        if (exact) for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
            for (int c = 0; c < 4; ++c)
                exact &= decoded.getData()[(y * width + x) * 4 + c] ==
                    (c == 3 ? 255 : (x * 16 / width) + 16 * (y * 16 / height));
        check("PNG bytes equal stencil pattern with top-left nearest sampling", exact);
        if (width == 16 && exact && !evidenceDirectory.empty()) {
            Pixels enlarged;
            enlarged.allocate(256,256,1);
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x)
                enlarged.getData()[y * 256 + x] = decoded.getData()[((y / 16) * 16 + x / 16) * 4];
            check("returned PNG nearest-neighbor PR evidence saved", bool(enlarged.save(filesystem::path(evidenceDirectory)/"00_tc_get_stencil.png")));
        }
    }
public:
    void setup() override {
        fbo_.allocate(8,8);fbo_.setDebugName("round-trip");
        hdr_.allocate(8,8,1,TextureFormat::RGBA32F);hdr_.setDebugName("float-round-trip");
        changing_.allocate(8,8);changing_.setDebugName("changing-format");
        rgba16Renderable_ = sg_query_pixelformat(SG_PIXELFORMAT_RGBA16).render;
        // RG8 is a GLES3 renderable byte format. Float formats would select
        // MCP's float readback path instead of exercising conversion to RGBA8.
        if (!rgba16Renderable_) alternate_ = TextureFormat::RG8;
        check("alternate conversion format is renderable and non-RGBA8",
            alternate_ != TextureFormat::RGBA8 && sg_query_pixelformat(toSokolFormat(alternate_)).render);
        integer_.setDebugName("integer-round-trip");
        integer_.allocate(8,8,1,TextureFormat::RGBA16);
        if (!rgba16Renderable_) {
            printf("SKIP: RGBA16 Fbo analysis returns converted RGBA8 colors (RGBA16 render unsupported)\n");
            check("unsupported RGBA16 allocation is rejected", !integer_.isAllocated());
        }

        gray_.allocate(16,16,1,TextureFormat::R8);
        stencil_.allocate(16,16); stencil_.setDebugName("stencil-pattern");
        msaa_.allocate(16,16,4); msaa_.setDebugName("stencil-msaa");
        stencilIndex_ = named(call("tc_list_fbos"),"fbos","stencil-pattern")["index"];
        after_=events().afterFrame.listen([this]() {
            if(pending_.empty()) return;
            auto first=decode(pending_[0]()());
            check("named Fbo captures final pass after frame",first["results"][0]["color"]==json::array({0,1,0,1}));
            auto second=decode(pending_[1]()());
            check("float Fbo preserves HDR values",second["format"]=="float"&&approxEq(second["results"][0]["color"][0],2.5));
            auto dead=decode(pending_[2]()());check("destroyed deferred source returns error",dead["status"]=="error");
            auto win=decode(pending_[3]()());
            // Windows may widen a small titled window (see #849).
            check("window source captures completed frame",
                win["width"]==getFramebufferWidth()&&win["height"]==getFramebufferHeight()&&
                win["results"][0]["color"]==json::array({1,0,0,1}));
            auto changed=decode(pending_[4]()());
            check("deferred capture converts changed format to RGBA8",changed["format"]=="RGBA8"&&
                changed["results"][0]["color"]==json::array({0,1,0,1}));
            if (rgba16Renderable_) {
                auto integer=decode(pending_[5]()());
                check("RGBA16 Fbo analysis returns converted RGBA8 colors",integer["format"]=="RGBA8"&&
                    integer["results"][0]["color"]==json::array({0,0,1,1}));
            }
            verifyStencil(stencilPending_[0]()(),16,16);
            verifyStencil(stencilPending_[1]()(),7,7);
            verifyStencil(stencilPending_[2]()(),16,16); // Never upscale.
            auto msaa = decode(stencilPending_[3]()());
            check("MSAA stencil rejected without an image", msaa["status"] == "error" &&
                msaa["message"].get<string>().find("MSAA") != string::npos);
            auto disappeared = decode(stencilPending_[4]()());
            check("destroyed deferred stencil source returns error", disappeared["status"] == "error");
            stencilPending_.clear();
            pending_.clear(); completed=true;exitApp();
        });
    }
    void draw() override {
        clear(1,0,0,1);
        auto defer=[this](json source) {
            string response=request("tc_analyze_image",{{"source",source},{"ops",json::array({{{"op","pixel"},{"x",0},{"y",0}}})}});
            check("GPU source response is deferred",response.empty());
            pending_.push_back(std::move(mcp::internal::deferralState().envelope));
        };
        auto deferStencil = [this](json args) {
            check("stencil response is deferred", request("tc_get_stencil",args).empty());
            stencilPending_.push_back(std::move(mcp::internal::deferralState().envelope));
        };
        deferStencil({{"fbo","stencil-pattern"}});
        deferStencil({{"fbo",stencilIndex_},{"width",7}});
        deferStencil({{"fbo",to_string(stencilIndex_)},{"width",32}});
        deferStencil({{"fbo","stencil-msaa"}});
        { Fbo dead; dead.setDebugName("stencil-dead"); deferStencil({{"fbo","stencil-dead"}}); }
        // Same gray-to-stencil fixture as stencilRoundTrip: all 256 byte values.
        gray_.begin(0,0,0); setupScreenOrtho(); setBlendMode(BlendMode::Disabled);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            setColor((x + 16 * y) / 255.0f); drawRect(float(x),float(y),1,1);
        }
        gray_.end();
        check("known pattern written to owned stencil", internal::FboStencilAccess::writeGray(gray_,stencil_));
        stencilMoved_ = std::move(stencil_);
        defer({{"fbo","round-trip"}});
        fbo_.begin(1,0,0,1); fbo_.end();fbo_.begin(0,1,0,1);fbo_.end();
        moved_=std::move(fbo_); // Deferred lookup must follow the new address.
        defer({{"fbo","float-round-trip"}});hdr_.begin(2.5f,0,0,1);hdr_.end();
        {Fbo dead;dead.setDebugName("pending-dead");defer({{"fbo","pending-dead"}});}
        defer({{"window",internal::WindowRequestAccess::creationId(*getWindow())}});
        defer({{"fbo","changing-format"}});
        changing_.allocate(8,8,1,alternate_);
        changing_.begin(0,1,0,1);changing_.end();
        if (rgba16Renderable_) {
            integer_.begin(0,0,1,1);integer_.end();
            defer({{"fbo","integer-round-trip"}});
        }
    }
};
#endif
}
TC_CORE_TEST_MAIN(int argc,char** argv) {
    headlessChecks();
#ifndef __EMSCRIPTEN__
    if(argc>1 && strcmp(argv[1],"--gpu-check")==0) {
        if (argc > 2) evidenceDirectory = argv[2];
        WindowSettings settings;settings.setSampleCount(1);settings.setSize(32,32);settings.setHighDpi(false);settings.setSwapInterval(0);
        return tcCoreTest::runDisplayTest<CaptureApp>(settings, failures, [] {
            check("GPU test completed",completed);
        });
    }
#else
    (void)argc;(void)argv;
#endif
    return failures?1:0;
}
