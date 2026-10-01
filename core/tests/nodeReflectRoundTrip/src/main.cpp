// =============================================================================
// core/tests/nodeReflectRoundTrip — reflection of derived values (#287).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants of TC_DERIVED, with Node's globalPos (derived from
// pos) as the main case:
// - reflectToJson() leaves derived keys out: a save has "pos" and no
//   "globalPos"; restoring it gives the exact saved pos, also after the parent
//   moved, into a node with no parent yet, and under a parent scaled to 0.
// - reflectToJson(obj, true) includes derived keys (the live view).
// - A derived key alone is written through its setter (globalPos moves the
//   node in world space).
// - When one write carries both, the canonical key wins (pos).
// - JsonWriteReflector::derived lists derived member paths, nested ones too.
// - MCP: tc_get_node_tree shows globalPos and names it under "derived"; the
//   read -> edit pos -> write back flow of tc_set_node_members applies the
//   edited pos; globalPos alone moves the node.
// =============================================================================

#include <TrussC.h>

#include <cmath>
#include <cstdio>
#include <string>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

static string str(const Vec3& v) {
    char b[96];
    snprintf(b, sizeof(b), "(%g, %g, %g)", v.x, v.y, v.z);
    return b;
}
static bool exact(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool near(const Vec3& a, const Vec3& b) {
    return fabs(a.x - b.x) < 1e-4f && fabs(a.y - b.y) < 1e-4f && fabs(a.z - b.z) < 1e-4f;
}
static bool contains(const vector<string>& v, const string& s) {
    for (auto& e : v) if (e == s) return true;
    return false;
}
static int indexOf(const vector<string>& v, const string& s) {
    for (size_t i = 0; i < v.size(); i++) if (v[i] == s) return (int)i;
    return -1;
}

// A free type with a derived member, nested in a reflected node, to check the
// nested path in JsonWriteReflector::derived and isDerived() in a group.
struct Span {
    float start = 0;
    float length = 1;
    float getEnd() const { return start + length; }
    void setEnd(float e) { length = e - start; }
};
TC_REFLECT_FREE(Span) {
    TC_DERIVED(end, getEnd, setEnd)
    TC_VALUE(start)
    TC_VALUE(length)
}

class SpanNode : public Node {
public:
    Span span;
    TC_REFLECT(SpanNode, Node) {
        TC_VALUE(span)
    }
};

// Records which visited names were inside the derived scope.
struct DerivedProbe : Reflector {
    vector<string> derived, plain;
    bool rec(const char* n) { (isDerived() ? derived : plain).push_back(n); return false; }
    bool visit(const char* n, float&) override { return rec(n); }
    bool visit(const char* n, int&) override { return rec(n); }
    bool visit(const char* n, bool&) override { return rec(n); }
    bool visit(const char* n, std::string&) override { return rec(n); }
    bool visit(const char* n, Vec2&) override { return rec(n); }
    bool visit(const char* n, Vec3&) override { return rec(n); }
    bool visit(const char* n, Color&) override { return rec(n); }
};

static void testJsonRoundTrip() {
    auto parent = make_shared<Node>();
    parent->setPos(100, 0, 0);
    auto child = make_shared<Node>();
    parent->addChild(child);
    child->setPos(10, 0, 0);

    Json snap = reflectToJson(*child);
    check("save: pos is written", snap.contains("pos"));
    check("save: globalPos is not written", !snap.contains("globalPos"), snap.dump());

    Json live = reflectToJson(*child, true);
    check("includeDerived: globalPos is written", live.contains("globalPos"));
    if (live.contains("globalPos")) {
        Vec3 g(live["globalPos"][0].get<float>(), live["globalPos"][1].get<float>(),
               live["globalPos"][2].get<float>());
        check("includeDerived: globalPos value is the world position", near(g, Vec3(110, 0, 0)), str(g));
    }

    // 1. Parent moved before restore.
    parent->setPos(200, 0, 0);
    child->setPos(0, 0, 0);
    auto r1 = reflectFromJson(*child, snap);
    check("restore after parent moved: pos is exact (10,0,0)",
          exact(child->getPos(), Vec3(10, 0, 0)), str(child->getPos()));
    check("restore after parent moved: world position is (210,0,0)",
          near(child->getGlobalPos(), Vec3(210, 0, 0)), str(child->getGlobalPos()));
    check("restore: globalPos not reported as applied", !contains(r1.applied, "globalPos"));

    // 2. Restore into a fresh node with no parent, then attach it.
    auto c2 = make_shared<Node>();
    reflectFromJson(*c2, snap);
    check("restore into parentless node: pos is exact (10,0,0)",
          exact(c2->getPos(), Vec3(10, 0, 0)), str(c2->getPos()));
    parent->addChild(c2);
    check("then addChild(): world position is (210,0,0)",
          near(c2->getGlobalPos(), Vec3(210, 0, 0)), str(c2->getGlobalPos()));

    // 3. Edit only pos in the saved JSON.
    Json edited = snap;
    edited["pos"] = {50, 0, 0};
    reflectFromJson(*child, edited);
    check("edited pos in JSON is applied (50,0,0)",
          exact(child->getPos(), Vec3(50, 0, 0)), str(child->getPos()));

    // 4. Parent scaled to 0.
    parent->setScale(0, 0, 0);
    child->setPos(0, 0, 0);
    reflectFromJson(*child, snap);
    check("restore under a parent scaled to 0: pos is exact (10,0,0)",
          exact(child->getPos(), Vec3(10, 0, 0)), str(child->getPos()));
    parent->setScale(1, 1, 1);

    // 5. globalPos alone moves the node in world space.
    child->setPos(0, 0, 0);
    auto r5 = reflectFromJson(*child, Json{{"globalPos", {250, 0, 0}}});
    check("globalPos alone: applied", contains(r5.applied, "globalPos"));
    check("globalPos alone: world position is (250,0,0)",
          near(child->getGlobalPos(), Vec3(250, 0, 0)), str(child->getGlobalPos()));
    check("globalPos alone: pos is (50,0,0)", near(child->getPos(), Vec3(50, 0, 0)), str(child->getPos()));

    // 6. Both written: pos wins.
    auto r6 = reflectFromJson(*child, Json{{"pos", {7, 0, 0}}, {"globalPos", {999, 0, 0}}});
    check("both written: pos wins (7,0,0)", exact(child->getPos(), Vec3(7, 0, 0)), str(child->getPos()));
    check("both written: globalPos applied before pos",
          indexOf(r6.applied, "globalPos") >= 0 &&
          indexOf(r6.applied, "globalPos") < indexOf(r6.applied, "pos"));

    // Saved JSON from before this change (it has globalPos) still loads; pos wins.
    Json old = {{"pos", {10, 0, 0}}, {"globalPos", {110, 0, 0}}, {"visible", true}};
    child->setPos(0, 0, 0);
    reflectFromJson(*child, old);
    check("JSON that contains globalPos still loads: pos is exact (10,0,0)",
          exact(child->getPos(), Vec3(10, 0, 0)), str(child->getPos()));
}

static void testDerivedMarker() {
    auto n = make_shared<Node>();
    DerivedProbe p;
    n->reflectMembers(p);
    check("isDerived(): globalPos is visited as derived", contains(p.derived, "globalPos"));
    check("isDerived(): pos is not derived", contains(p.plain, "pos") && !contains(p.derived, "pos"));
    check("isDerived(): scope closed after the derived value", p.derived.size() == 1);

    auto sp = make_shared<SpanNode>();
    SpanNode& s = *sp;
    s.span.start = 2;
    s.span.length = 3;
    JsonWriteReflector w;
    s.reflectMembers(w);
    check("nested derived: path listed as span.end", contains(w.derived, "span.end"));
    check("nested derived: Node's globalPos listed", contains(w.derived, "globalPos"));
    check("nested derived: not written by default",
          w.members.contains("span") && !w.members["span"].contains("end") &&
          w.members["span"].contains("start"));

    Json live = reflectToJson(s, true);
    check("nested derived: written with includeDerived",
          live.contains("span") && live["span"].contains("end") && live["span"]["end"].get<float>() == 5.0f);

    reflectFromJson(s, Json{{"span", {{"end", 10}}}});
    check("nested derived alone: setter runs (length 8)", s.span.length == 8.0f);
    reflectFromJson(s, Json{{"span", {{"end", 100}, {"length", 4}}}});
    check("nested derived with canonical: canonical wins (length 4)", s.span.length == 4.0f);
}

// --- MCP tools on a headless App -------------------------------------------

static Json callTool(const string& name, const Json& args) {
    Json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                {"params", {{"name", name}, {"arguments", args}}}};
    string reply = mcp::Server::instance().processMessage(req.dump());
    try {
        Json j = Json::parse(reply);
        return Json::parse(j.at("result").at("content").at(0).at("text").get<string>());
    } catch (...) {
        return Json();
    }
}

class McpApp : public App {
public:
    shared_ptr<Node> parent, child;
    void setup() override {
        parent = make_shared<Node>();
        parent->setPos(100, 0, 0);
        addChild(parent);
        child = make_shared<Node>();
        parent->addChild(child);
        child->setPos(10, 0, 0);
    }
    void update() override {
        const uint64_t id = child->getInstanceId();
        Json tree = callTool("tc_get_node_tree", Json{{"id", id}, {"depth", 0}});
        Json node = tree.value("tree", Json());
        Json members = node.value("members", Json());
        check("MCP get: status ok", tree.value("status", "") == "ok", tree.dump());
        check("MCP get: members show globalPos", members.contains("globalPos"), members.dump());
        check("MCP get: globalPos is named under \"derived\"",
              node.contains("derived") && node["derived"].is_array() &&
              contains(node["derived"].get<vector<string>>(), "globalPos"), node.dump());

        // Read -> edit pos -> write the whole object back (globalPos is stale).
        members["pos"] = {50, 0, 0};
        Json set = callTool("tc_set_node_members", Json{{"id", id}, {"members", members}});
        check("MCP set: status ok", set.value("status", "") == "ok", set.dump());
        check("MCP set: edited pos is applied (50,0,0) despite the stale globalPos",
              exact(child->getPos(), Vec3(50, 0, 0)), str(child->getPos()));
        check("MCP set: reply members show globalPos",
              set.contains("members") && set["members"].contains("globalPos"));

        // globalPos alone moves the node in world space.
        callTool("tc_set_node_members", Json{{"id", id}, {"members", {{"globalPos", {300, 0, 0}}}}});
        check("MCP set: globalPos alone moves the node (world 300)",
              near(child->getGlobalPos(), Vec3(300, 0, 0)), str(child->getGlobalPos()));
        check("MCP set: globalPos alone gives pos (200,0,0)",
              near(child->getPos(), Vec3(200, 0, 0)), str(child->getPos()));

        requestExit();
    }
};

int main() {
    testJsonRoundTrip();
    testDerivedMarker();

    mcp::registerInspectionTools();
    mcp::registerControlTools();
    runHeadlessApp<McpApp>(HeadlessSettings().setFps(1000));

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
