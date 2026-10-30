#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <set>
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#endif

using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

json request(const std::string& method, const json& params = json::object()) {
    return json::parse(mcp::internal::Server::instance().processMessage(
        json{{"jsonrpc", "2.0"}, {"id", "level-test"}, {"method", method},
             {"params", params}}.dump()));
}
std::set<std::string> listed() {
    std::set<std::string> names;
    const auto reply = request("tools/list");
    for (const auto& entry : reply["result"]["tools"])
        names.insert(entry["name"].get<std::string>());
    return names;
}
json call(const std::string& name) {
    return request("tools/call", {{"name", name}, {"arguments", json::object()}});
}
bool callable(const std::string& name) {
    const auto reply = call(name);
    return reply.contains("result") && !reply["result"].value("isError", false);
}

#ifndef __EMSCRIPTEN__
// Display-test modes exercise startup after the main App has run setup().
bool grantInSetup = false;
bool envSetupRan = false;
bool envUpdateRan = false;
mcp::ToolLevel expectedEnvGrant = mcp::ToolLevel::Public;
struct EnvGrantApp : App {
    void setup() override {
        envSetupRan = true;
        check("environment does not raise the code grant",
              mcp::getToolLevel() == expectedEnvGrant);
        check("screenshots require a code grant", bool(listed().count("tc_get_screenshot")) ==
              (expectedEnvGrant >= mcp::ToolLevel::ReadOnly));
        check("environment exposes Control only when code granted it",
              bool(listed().count("tc_mouse_click")) == (expectedEnvGrant == mcp::ToolLevel::Control));
        if (grantInSetup) mcp::setToolLevel(mcp::ToolLevel::Control);
        check("App::setup can retain or raise the environment grant",
              mcp::getToolLevel() == (grantInSetup ? mcp::ToolLevel::Control : expectedEnvGrant));
        check("Control exposure follows the final app grant",
              bool(listed().count("tc_mouse_click")) == mcp::isDebuggerEnabled());
        check("mainWindowTitle reflects build kind", mcp::internal::markedWindowTitle("MCP startup test") ==
              (mcp::internal::isDevBuild() ? "MCP startup test [MCP]" : "MCP startup test"));
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        if (sapp_x11_get_display()) {
            auto nativeTitle = [] {
                char* title = nullptr;
                auto* display = static_cast<Display*>(const_cast<void*>(sapp_x11_get_display()));
                const auto window = static_cast<::Window>(reinterpret_cast<uintptr_t>(sapp_x11_get_window()));
                XFetchName(display, window, &title);
                const std::string result = title ? title : "";
                if (title) XFree(title);
                return result;
            };
            check("initial native title reflects build kind", nativeTitle() ==
                  mcp::internal::markedWindowTitle("MCP startup test"));
            setWindowTitle("Retitled");
            check("native main window retains dev marker after retitle", nativeTitle() ==
                  mcp::internal::markedWindowTitle("Retitled"));
        }
#endif
    }
    void update() override {
        envUpdateRan = true;
        check("startup opens HTTP unless environment disables it", (mcp::getHttpPort() > 0) ==
              !mcp::internal::environmentClosesMcp());
        if (mcp::getToolLevel() == mcp::ToolLevel::Public)
            check("startup environment exposes health only", listed() == std::set<std::string>{"tc_get_health"});
        exitApp();
    }
};

int envStartup(const std::string& mode) {
    const char* envMcp = std::getenv("TRUSSC_MCP");
    check("startup test has TRUSSC_MCP=1 or is a dev build", mcp::internal::isDevBuild() ||
          (envMcp && std::string(envMcp) == "1"));
    if (mcp::internal::isDevBuild()) expectedEnvGrant = mcp::ToolLevel::Control;
    if (failures) return 1;
    grantInSetup = mode == "--env-control-setup";
    if (mode == "--env-control-before") {
        mcp::setToolLevel(mcp::ToolLevel::Control);
        expectedEnvGrant = mcp::ToolLevel::Control;
    }
    WindowSettings settings;
    settings.width = settings.height = 64;
    settings.title = "MCP startup test";
    return tcCoreTest::runDisplayTest<EnvGrantApp>(settings, failures, [] {
        check("environment startup and shutdown succeed", internal::appExitCode() == 0);
        check("environment test reached App::setup and update", envSetupRan && envUpdateRan);
    });
}
#endif

// Only this compatibility test deliberately calls deprecated APIs. Keep it
// compiling under TC_DEPRECATED_ERRORS=1 on each supported compiler.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
void aliases() {
    mcp::registerControlTools();
    check("control alias grants Control", mcp::getToolLevel() == mcp::ToolLevel::Control);
    check("debugger query reflects Control", mcp::isDebuggerEnabled());
    mcp::registerInspectionTools();
    check("inspection alias lowers to ReadOnly", mcp::getToolLevel() == mcp::ToolLevel::ReadOnly);
    check("debugger query reflects downgrade", !mcp::isDebuggerEnabled());
    check("inspection alias keeps tools registered", mcp::hasTool("tc_mouse_click"));
    mcp::registerDebuggerTools();
    check("older debugger alias grants Control", mcp::isDebuggerEnabled());
}
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifndef __EMSCRIPTEN__
    if (argc > 1) {
        const std::string mode = argv[1];
        if (mode == "--env-health" || mode == "--env-control-before" || mode == "--env-control-setup" || mode == "--dev-startup")
            return envStartup(mode);
    }
#endif
#ifndef __EMSCRIPTEN__
    // This mode tests levels only. Server opt-in is covered by mcpHttpGuard.
#ifdef _WIN32
    _putenv_s("TRUSSC_MCP", "0");
#else
    setenv("TRUSSC_MCP", "0", 1);
#endif
#endif
    // Run the same exposure contract on native and Web; only HTTP is native.
    check("initial grant is Public", mcp::getToolLevel() == mcp::ToolLevel::Public);
    // First use may be from a hot reload guest; core tools must remain host-owned.
    int owner;
    mcp::internal::setRegistrationOwner(&owner);
    check("Public lists only built-in health", listed() == std::set<std::string>{"tc_get_health"});
    check("core initialization preserves registration owner", mcp::internal::registrationOwner() == &owner);
    mcp::tool("guest_level_probe", "").bind([]() -> json { return true; });
    mcp::internal::setRegistrationOwner(nullptr);

    int invoked = 0;
    auto handler = [&]() -> json { ++invoked; return true; };
    mcp::tool("public_probe", "").bind(handler);
    mcp::tool("readonly_probe", "").level(mcp::ToolLevel::ReadOnly).bind(handler);
    mcp::tool("control_probe", "").level(mcp::ToolLevel::Control).bind(handler);
    mcp::tool("tcx_probe", "").bind(handler);
    int rejectedLogs = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("tc_ is reserved") != std::string::npos)
            ++rejectedLogs;
    });
    mcp::tool("tc_rejected", "").bind(handler);
    mcp::tool("tc_get_health", "replacement").bind(handler);
    check("reserved prefix logs errors without throwing", rejectedLogs == 2);
    check("reserved prefix is not registered", !mcp::hasTool("tc_rejected"));
    check("rejected replacement does not run", callable("tc_get_health") && invoked == 0);
    check("tcx prefix is allowed", callable("tcx_probe"));

    const std::set<std::string> controls = {"tc_quit", "tc_mouse_move", "tc_mouse_press",
        "tc_mouse_release", "tc_mouse_click", "tc_mouse_scroll", "tc_key_press",
        "tc_key_release", "tc_select_node", "tc_set_node_members"};
    mcp::setToolLevel(mcp::ToolLevel::Control);
    const auto all = listed();
    for (auto grant : {mcp::ToolLevel::Public, mcp::ToolLevel::ReadOnly, mcp::ToolLevel::Control,
                       mcp::ToolLevel::ReadOnly, mcp::ToolLevel::Public}) {
        mcp::setToolLevel(grant);
        check("getter returns current grant", mcp::getToolLevel() == grant);
        const auto names = listed();
        check("user tool defaults to Public", names.count("public_probe") && callable("public_probe"));
        for (const auto& name : all) {
            if (name.rfind("tc_", 0) != 0) continue;
            const auto required = name == "tc_get_health" ? mcp::ToolLevel::Public :
                controls.count(name) ? mcp::ToolLevel::Control : mcp::ToolLevel::ReadOnly;
            check(name.c_str(), bool(names.count(name)) == (required <= grant));
        }
        for (const auto& probe : {std::make_pair("readonly_probe", mcp::ToolLevel::ReadOnly),
                                 std::make_pair("control_probe", mcp::ToolLevel::Control)}) {
            const bool exposed = probe.second <= grant;
            check("list obeys level", bool(names.count(probe.first)) == exposed);
            const int before = invoked;
            check("call obeys level", callable(probe.first) == exposed);
            check("hidden handler never runs", invoked == before + (exposed ? 1 : 0));
        }
        check("debugger query follows current grant", mcp::isDebuggerEnabled() == (grant == mcp::ToolLevel::Control));
    }
    const auto hiddenReadOnly = call("readonly_probe");
    const json expectedReadOnlyResult = {
        {"isError", true}, {"content", json::array({{{"type", "text"},
            {"text", "Tool 'readonly_probe' requires ReadOnly, current level is Public"}}})}};
    check("hidden ReadOnly error retains id and names both levels", hiddenReadOnly == json{
        {"jsonrpc", "2.0"}, {"id", "level-test"}, {"result", expectedReadOnlyResult}});
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    const auto hidden = call("control_probe");
    const json expectedResult = {
        {"isError", true}, {"content", json::array({{{"type", "text"},
            {"text", "Tool 'control_probe' requires Control, current level is ReadOnly"}}})}};
    check("hidden error retains id and names both levels", hidden == json{
        {"jsonrpc", "2.0"}, {"id", "level-test"}, {"result", expectedResult}});
    check("ReadOnly core status callable", callable("tc_get_status"));
    check("ReadOnly core alerts callable", callable("tc_get_alerts"));
    mcp::tool("registered_later", "").level(mcp::ToolLevel::Control).bind(handler);
    check("register after setting level stays hidden", !listed().count("registered_later") && !callable("registered_later"));
    mcp::setToolLevel(mcp::ToolLevel::Control);
    check("raising level exposes existing registrations", listed().count("registered_later") && callable("registered_later"));
    mcp::tool("after_grant", "").level(mcp::ToolLevel::Control).bind(handler);
    check("register after grant is immediately exposed", listed().count("after_grant") && callable("after_grant"));
    mcp::internal::removeRegistrationsOwnedBy(&owner);
    check("reload cleanup drops guest tool", !mcp::hasTool("guest_level_probe"));
    check("reload cleanup preserves grant and core tools", mcp::getToolLevel() == mcp::ToolLevel::Control && mcp::hasTool("tc_get_health") && mcp::hasTool("tc_mouse_click"));
    aliases();
#ifndef __EMSCRIPTEN__
    check("TRUSSC_MCP=0 prevents HTTP even after code grants", mcp::getHttpPort() == 0 && !mcp::internal::getHttpServer());
#endif
    return failures ? 1 : 0;
}
