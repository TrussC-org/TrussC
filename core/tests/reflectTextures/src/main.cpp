// =============================================================================
// core/tests/reflectTextures — reflection of image-like values (HasTexture).
//
// Guards the image channel of Reflector:
// - TC_VALUE(fbo) / TC_VALUE(image) reach Reflector::visit(name, HasTexture&)
//   with the member itself, and a Reflector written before the overload keeps
//   compiling and ignores images.
// - A save (reflectToJson / nodeToJson without includeDerived) has no key for
//   an image, and loading it back changes nothing; a key for an image in the
//   source is never applied (reported as read-only, not unknown).
// - The live view (includeDerived, what the MCP node tools use) reports
//   {width, height, format, allocated}, never pixels.
//
// Without arguments it runs headless: no GPU, so the images are unallocated
// (allocated=false). `--gpu-check` (display-test, Xvfb works) repeats the
// view checks with allocated images, including a reallocation.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace std;
using namespace tc;

namespace {

int g_fail = 0;
void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

// A node with plain values and two image-like members.
class Holder : public Node {
public:
    float speed = 1.5f;
    Fbo fbo;
    Image image;
    TC_REFLECT(Holder, Node) {
        TC_VALUE(speed)
        TC_VALUE(fbo)
        TC_VALUE(image)
    }
};

// Records the images it is shown.
struct ImageProbe : Reflector {
    vector<string> names;
    vector<HasTexture*> images;
    vector<string> plain;
    bool readOnlyDuringImage = false;
    bool visit(const char* n, float&) override { plain.push_back(n); return false; }
    bool visit(const char* n, int&) override { plain.push_back(n); return false; }
    bool visit(const char* n, bool&) override { plain.push_back(n); return false; }
    bool visit(const char* n, std::string&) override { plain.push_back(n); return false; }
    bool visit(const char* n, Vec2&) override { plain.push_back(n); return false; }
    bool visit(const char* n, Vec3&) override { plain.push_back(n); return false; }
    bool visit(const char* n, Color&) override { plain.push_back(n); return false; }
    bool visit(const char* n, HasTexture& v) override {
        names.push_back(n);
        images.push_back(&v);
        return false;
    }
};

// A Reflector written before the image overload existed.
struct LegacyReflector : Reflector {
    int plain = 0;
    bool visit(const char*, float&) override { ++plain; return false; }
    bool visit(const char*, int&) override { ++plain; return false; }
    bool visit(const char*, bool&) override { ++plain; return false; }
    bool visit(const char*, std::string&) override { ++plain; return false; }
    bool visit(const char*, Vec2&) override { ++plain; return false; }
    bool visit(const char*, Vec3&) override { ++plain; return false; }
    bool visit(const char*, Color&) override { ++plain; return false; }
};

bool hasInfoKeys(const Json& j) {
    return j.is_object() && j.contains("width") && j.contains("height") &&
           j.contains("format") && j.contains("allocated") && j.size() == 4;
}

void testVisitor() {
    Holder h;
    ImageProbe probe;
    h.reflectMembers(probe);
    check("visitor receives both images", probe.names == vector<string>{"fbo", "image"});
    check("the Fbo arrives as the member itself",
          probe.images.size() == 2 && dynamic_cast<Fbo*>(probe.images[0]) == &h.fbo);
    check("the Image arrives as the member itself",
          probe.images.size() == 2 && dynamic_cast<Image*>(probe.images[1]) == &h.image);
    bool speedSeen = false;
    for (auto& n : probe.plain) if (n == "speed") speedSeen = true;
    check("plain values are still visited as before", speedSeen);

    LegacyReflector legacy;
    h.reflectMembers(legacy);
    check("a Reflector without the image overload ignores images", legacy.plain > 0);
}

void testJson() {
    Holder h;
    h.speed = 7.0f;

    Json saved = reflectToJson(h);
    check("save: plain value is written", saved.contains("speed") && saved["speed"] == 7.0f);
    check("save: no key for the Fbo", !saved.contains("fbo"), saved.dump());
    check("save: no key for the Image", !saved.contains("image"), saved.dump());
    Json nodeSaved = nodeToJson(h, 0);
    check("nodeToJson save: no image keys",
          !nodeSaved["members"].contains("fbo") && !nodeSaved["members"].contains("image"),
          nodeSaved.dump());

    Json live = reflectToJson(h, true);
    check("live view: Fbo has exactly {width,height,format,allocated}", hasInfoKeys(live["fbo"]), live.dump());
    check("live view: Image has exactly {width,height,format,allocated}", hasInfoKeys(live["image"]), live.dump());
    check("live view: unallocated Fbo reports allocated=false, no format",
          live["fbo"]["allocated"] == false && live["fbo"]["format"] == "none" &&
          live["fbo"]["width"] == 0 && live["fbo"]["height"] == 0, live["fbo"].dump());

    // Load: the saved data restores the plain value, and a source that does
    // carry image keys (a client echoing the live view) changes no image.
    Holder loaded;
    JsonReadReflector r = reflectFromJson(loaded, saved);
    check("load: plain value restored", loaded.speed == 7.0f);
    check("load: nothing reported unknown or read-only",
          r.unknownKeys().empty() && r.readOnly.empty() && r.skipped.empty());

    Holder echoed;
    JsonReadReflector r2 = reflectFromJson(echoed, live);
    check("load of the live view: plain value applied", echoed.speed == 7.0f);
    check("load of the live view: image keys are read-only, not unknown",
          r2.unknownKeys().empty() && r2.readOnly.size() == 2, Json(r2.readOnly).dump());
    bool imageApplied = false;
    for (auto& k : r2.applied) if (k == "fbo" || k == "image") imageApplied = true;
    check("load of the live view: image keys are never applied",
          !imageApplied && !echoed.fbo.isAllocated());

    // Round trip: save -> load -> save is stable.
    check("round trip: save is unchanged", reflectToJson(loaded) == saved);
}

// MCP: the tool reads a node with the live view.
Json callTool(const string& name, const Json& args) {
    Json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                {"params", {{"name", name}, {"arguments", args}}}};
    string reply = mcp::internal::Server::instance().processMessage(req.dump());
    try {
        Json j = Json::parse(reply);
        return Json::parse(j.at("result").at("content").at(0).at("text").get<string>());
    } catch (...) {
        return Json();
    }
}

// Reads the holder through tc_get_node_tree and checks the image info.
// expectAllocated: what the images are expected to report.
void checkMcp(Holder& h, bool expectAllocated, int w, int h2, const char* fmt) {
    Json tree = callTool("tc_get_node_tree", Json{{"id", h.getInstanceId()}, {"depth", 0}});
    Json members = tree.value("tree", Json()).value("members", Json());
    check("MCP get: status ok", tree.value("status", "") == "ok", tree.dump());
    check("MCP get: Fbo info has width/height/format/allocated", hasInfoKeys(members["fbo"]), members.dump());
    check("MCP get: Image info has width/height/format/allocated", hasInfoKeys(members["image"]), members.dump());
    check("MCP get: Fbo allocated flag", members["fbo"].value("allocated", !expectAllocated) == expectAllocated);
    check("MCP get: Fbo size and format",
          members["fbo"].value("width", -1) == w && members["fbo"].value("height", -1) == h2 &&
          members["fbo"].value("format", "") == string(fmt), members["fbo"].dump());
    check("MCP get: images are not listed as derived",
          !tree["tree"].contains("derived") ||
          tree["tree"]["derived"].dump().find("fbo") == string::npos);

    // Writing the whole live view back must leave the images alone and report
    // the image keys as read-only.
    Json set = callTool("tc_set_node_members", Json{{"id", h.getInstanceId()}, {"members", members}});
    check("MCP set: echoing the live view is accepted", set.value("status", "") == "ok", set.dump());
    check("MCP set: image keys are reported read-only",
          set.contains("readOnly") && set["readOnly"].dump().find("fbo") != string::npos, set.dump());
    check("MCP set: no image key is reported unknown", !set.contains("unknown") ||
          set["unknown"].dump().find("fbo") == string::npos, set.dump());
}

class HeadlessMcpApp : public App {
public:
    shared_ptr<Holder> holder;
    void setup() override {
        holder = make_shared<Holder>();
        addChild(holder);
    }
    void update() override {
        checkMcp(*holder, false, 0, 0, "none");
        exitApp();
    }
};

// --- --gpu-check: real, allocated images -----------------------------------
bool gpuFinished = false;
class GpuApp : public App {
public:
    shared_ptr<Holder> holder;
    void setup() override {
        holder = make_shared<Holder>();
        addChild(holder);
        holder->fbo.allocate(64, 32);
        holder->image.allocate(8, 4);
    }
    void update() override {
        Holder& h = *holder;
        check("GPU: Fbo is allocated", h.fbo.isAllocated());
        Json live = reflectToJson(h, true);
        check("GPU live view: Fbo info", hasInfoKeys(live["fbo"]) && live["fbo"]["allocated"] == true &&
              live["fbo"]["width"] == 64 && live["fbo"]["height"] == 32 && live["fbo"]["format"] == "RGBA8",
              live["fbo"].dump());
        check("GPU live view: Image info", hasInfoKeys(live["image"]) && live["image"]["allocated"] == true &&
              live["image"]["width"] == 8 && live["image"]["height"] == 4, live["image"].dump());
        check("GPU save: still no image keys",
              !reflectToJson(h).contains("fbo") && !reflectToJson(h).contains("image"));
        checkMcp(h, true, 64, 32, "RGBA8");

        h.fbo.allocate(96, 48, 1, TextureFormat::RGBA16F);
        Json re = reflectToJson(h, true);
        check("GPU live view: reallocated Fbo follows size and format",
              re["fbo"]["width"] == 96 && re["fbo"]["height"] == 48 && re["fbo"]["format"] == "RGBA16F",
              re["fbo"].dump());
        h.fbo.clear();
        Json cleared = reflectToJson(h, true);
        check("GPU live view: cleared Fbo reports allocated=false",
              cleared["fbo"]["allocated"] == false && cleared["fbo"]["format"] == "none");
        gpuFinished = true;
        exitApp();
    }
};

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    testVisitor();
    testJson();
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    mcp::setToolLevel(mcp::ToolLevel::Control);

    if (argc >= 2 && strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        settings.setSwapInterval(0);
        return tcCoreTest::runDisplayTest<GpuApp>(settings, g_fail, [] {
            check("completed all GPU checks", gpuFinished);
        });
    }

    Window receiver;
    internal::WindowRequestAccess::bindForTests(receiver, 101);
    receiver.setApp<HeadlessMcpApp>(); internal::applyPendingAppForTests(receiver);
    receiver.events().update.notify();
    internal::WindowRequestAccess::teardown(receiver);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
