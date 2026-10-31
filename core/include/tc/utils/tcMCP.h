#pragma once

// =============================================================================
// tcMCP.h - Model Context Protocol (MCP) Server Implementation
// =============================================================================
// HTTP transport via cpp-httplib. Requests arrive on a worker thread and are
// forwarded to the main (GL) thread through ThreadChannel + promise so that
// tool handlers can safely access graphics state.
// =============================================================================

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <utility>
#include <sstream>
#include <future>
#include <thread>
#include <atomic>
#include <mutex>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <optional>
#include <type_traits>
#include "tcTeardown.h"

// JSON support
#include "../../nlohmann/json.hpp"
using json = nlohmann::json;

#include "tcAnnotations.h"
#include "tcLog.h"
#include "tcThreadChannel.h"
#include "tcVersion.h"
#include "tcMcpLimits.h"
namespace trussc::mcp::internal {
// Retain only the outstanding screenshot slot through native completion.
class WindowCaptureAdmission {
public:
    struct Ownership {
        std::shared_ptr<void> credit;
    };
    explicit WindowCaptureAdmission(std::shared_ptr<void> credit)
        : previous_(current()), credit_(std::move(credit)) { current() = this; }
    ~WindowCaptureAdmission() { current() = previous_; }
    WindowCaptureAdmission(const WindowCaptureAdmission&) = delete;
    WindowCaptureAdmission& operator=(const WindowCaptureAdmission&) = delete;
    static std::shared_ptr<Ownership> retain() {
        auto credit = current() ? current()->credit_ : std::shared_ptr<void>{};
        if (!credit) return {};
        return std::make_shared<Ownership>(Ownership{std::move(credit)});
    }
private:
    static WindowCaptureAdmission*& current();
    WindowCaptureAdmission* previous_;
    std::shared_ptr<void> credit_;
};

// Native adapters and delayed-completion fixtures share this terminal seam.
// Resource cleanup and consumer destruction precede admission release/signaling.
template<class Job, class Readback, class Cleanup>
void finishCaptureJob(Job& job, const Readback& readback, Cleanup&& cleanup) {
    auto consume = std::move(job.completion);
    try { consume(readback); }
    catch (...) { /* cleanup also runs after a failed consumer */ }
    consume = {};
    cleanup();
    job.admission.reset();
    auto terminal = std::move(job.finished);
    if (terminal) terminal();
}

// Timeout never waits for a consumer already copying its result. A later
// callback skips allocation; an in-progress consumer keeps the job's admission.
class CaptureCompletionGate {
public:
    template<class F> void complete(F&& consume) {
        if (!abandoned_.load()) consume();
    }
    void abandon() { abandoned_.store(true); }
private:
    std::atomic<bool> abandoned_{false};
};
}
#include "../app/tcExit.h"

#ifndef __EMSCRIPTEN__
namespace httplib { class Server; struct Request; struct Response; }
#endif

namespace trussc {
namespace mcp {

// Tools are registered independently of the level granted by the host.
/// Required tool grant: Public < ReadOnly < Control.
enum class ToolLevel {
    Public,   // Health only for built-in tools; user tools default to Public.
    ReadOnly, // Observe the app (captures may write to disk).
    Control   // Operate the app (input, scene changes, quit).
};

// Defined in tcMCP.cpp: one host setting shared with hot reload guests.
// The level API and list/call checks apply on every platform.
/// Set the host grant; ReadOnly/Control opens loopback HTTP unless the environment closes it.
void setToolLevel(ToolLevel level);
/// Return the current host grant (shared across hot reloads).
ToolLevel getToolLevel();

namespace internal {
class Server;
void registerStandardTools(Server& server);
// Host-owned build/start policy, also shared with hot reload guests.
bool isDevBuild();
bool healthOnly();
bool& healthOnlyEndpoint();
bool& explicitlyOpened();
bool environmentClosesMcp();
void startConfiguredServer(const char* opener);
void initializeMcp();
std::string markedWindowTitle(const std::string&);
inline const char* toolLevelName(ToolLevel level) {
    switch (level) {
        case ToolLevel::Public: return "Public";
        case ToolLevel::ReadOnly: return "ReadOnly";
        case ToolLevel::Control: return "Control";
    }
    return "Public";
}
} // namespace internal

// ---------------------------------------------------------------------------
// Deferred tool responses
// ---------------------------------------------------------------------------
// Some tools (e.g. tc_get_screenshot) need state that only exists AFTER present()
// — during the frame, drawing is still deferred and a readback would be blank.
// Such a handler calls deferToolResultUntilAfterFrame(): processHttpQueue()
// stashes the request instead of answering it, and drainDeferredResponses()
// (invoked from an afterFrame listener) runs the producer at the safe readback
// point and unblocks the waiting HTTP worker. All state below is touched only
// on the main thread inside processHttpQueue()/handleToolsCall().
//
// A deferral can name a TARGET window (an opaque WindowContext*): its producer
// then runs right after THAT window's present(), inside its own tick, where
// its drawable is the current one (a secondary window's readback is only valid
// there — #243). The main window drains untargeted entries from its afterFrame
// listener; each secondary window drains its own from its tick.

namespace internal {

// Replies preserve valid UTF-8 and replace malformed bytes with U+FFFD.
inline std::string dumpReply(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

inline std::string allocationErrorReply(const json& id = nullptr) {
    return std::string(R"({"jsonrpc":"2.0","id":)") + dumpReply(id) +
        R"(,"error":{"code":-32603,"message":"MCP allocation failed"}})";
}
inline std::string makeResult(const json& id, const json& result) {
    if (id.is_discarded()) return ""; // Missing id (notification), distinct from null.
    json res = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
    return dumpReply(res);
}

// Rvalue results transfer the completed arrays into the envelope once.
inline std::string makeResult(const json& id, json&& result) {
    if (id.is_discarded()) return "";
    json res = json::object();
    res["jsonrpc"] = "2.0";
    res["id"] = id;
    res["result"] = std::move(result);
    return dumpReply(res);
}

// Explicit admission refusals are JSON-RPC errors, before any tool work is queued.
struct RequestRejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline std::string toolError(const json& id, const std::string& message) {
    return makeResult(id, {{"content", {{{"type", "text"}, {"text", message}}}},
                           {"isError", true}});
}

// The promise carries a THUNK, not the reply string: the blocked HTTP worker
// executes it (future.get()()) to obtain the reply. For ordinary tools the
// thunk just returns a string built on the main thread; two-stage tools (see
// deferToolResultTwoStage) put their heavy encode work inside the thunk so it
// runs on the HTTP worker — off the frame loop — while main only did the
// readback.
using ReplyThunk = std::function<std::string()>;
inline void completeResponse(const std::shared_ptr<std::promise<ReplyThunk>>& promise, ReplyThunk thunk) {
    try { promise->set_value(std::move(thunk)); }
    catch (const std::future_error& e) {
        if (e.code() != std::make_error_code(std::future_errc::promise_already_satisfied)) throw;
    }
}

// Always resolve terminal responses, including when building an error fails.
template<class Builder>
inline void completeTerminalResponse(const std::shared_ptr<std::promise<ReplyThunk>>& promise,
                                     Builder&& build, const json& id = nullptr) {
    try {
        auto reply = build();
        completeResponse(promise, [reply = std::move(reply)] { return reply; });
    } catch (...) {
        completeResponse(promise, [id] { return allocationErrorReply(id); });
    }
}

inline bool hasInvalidRequestId(const json& request) {
    auto id = request.find("id");
    // MCP 2024-11-05 permits strings and integers, never null or floats.
    return id != request.end() && !(id->is_string() || id->is_number_integer());
}

// Shared by normal dispatch and shutdown; no alternate unrestricted parser.
inline json parseRequest(const std::string& body) {
    const auto limits = getAdmissionLimits();
    checkAdmission(body.size(), limits.bodyBytes, AdmissionStore::Body);
    const json::parser_callback_t callback = [limits](int depth, json::parse_event_t event, json&) {
        // Bound (security): reject at the configured callback depth and the legacy structural boundary before recursive DOM consumers.
        const bool opensContainer = event == json::parse_event_t::object_start || event == json::parse_event_t::array_start;
        // The former parser guard had a 64-level floor; retain its boundary
        // (including empty containers) and the callback's lower-limit behavior.
        const auto structuralLimit = (std::max)(size_t{64}, limits.jsonDepth);
        if (opensContainer && size_t(depth) >= structuralLimit) {
            admissionRejected(AdmissionStore::Working, structuralLimit, size_t(depth) + 1, "levels");
            throw AdmissionError();
        }
        if (depth >= 0 && size_t(depth) > limits.jsonDepth) {
            admissionRejected(AdmissionStore::Working, limits.jsonDepth, size_t(depth), "levels");
            throw AdmissionError();
        }
        return true;
    };
    return json::parse(body, callback);
}

// Retain a screenshot slot through completion and release the worker payload.
inline ReplyThunk carryCaptureAdmission(ReplyThunk work, std::shared_ptr<void> admission) {
    return [work = std::move(work), admission = std::move(admission)]() mutable {
        auto terminalLease = std::move(admission);
        // Destroy captured Pixels before returning their credit, on normal
        // completion and exception unwinding, even if the thunk stays alive.
        auto terminalWork = std::exchange(work, ReplyThunk{});
        return terminalWork();
    };
}
struct DeferredResponse {
    json id = nullptr;  // request id retained for deferred error replies
    std::shared_ptr<std::promise<ReplyThunk>> response;  // unblocks the HTTP worker
    std::function<ReplyThunk()> makeEnvelope;            // main stage → worker thunk
    const void* target = nullptr;                        // explicit WindowContext to run in
    std::chrono::steady_clock::time_point deadline;      // give up without a frame after this
    // Registration owner of the code the producer runs (DeferralState::owner):
    // a hot reload guest generation, or null for host code. When that owner
    // is removed, removeRegistrationsOwnedBy() answers the entry with
    // a tool error instead of running its producer, which may reach the App
    // about to be deleted (a guest tool capturing `this`, a status-image
    // getter). A host tool's deferral is not affected.
    const void* owner = nullptr;
    bool ownerRetired = false; // inert before capture destruction can reenter a drain
};

// A deferral whose window renders no frame in this time (minimized,
// hidden, throttled very low, closed) is answered with an error instead of
// leaving the HTTP worker blocked.
// Bound (liveness): expire a targeted capture after five seconds without an available frame.
inline constexpr std::chrono::seconds kTargetedDeferralTimeout{5};

struct DeferralState {
    std::shared_ptr<::trussc::internal::AppOwner> appOwner;
    json id = nullptr;  // copied into DeferredResponse when queued
    bool requested = false;                  // set by deferToolResultUntilAfterFrame()
    std::function<json()> produce;           // tool content producer (runs fully on main)
    bool twoStageRequested = false;          // set by deferToolResultTwoStage()
    std::function<std::function<json()>()> produceTwoStage;  // main stage → worker stage
    const void* target = nullptr;            // explicit WindowContext after admission
    bool hasEnvelope = false;                // set by handleToolsCall(), read by processHttpQueue()
    std::function<ReplyThunk()> envelope;    // JSON-RPC reply builder (main part)
    // Registration owner of the code the deferred producer runs: the called
    // tool's owner, set by handleToolsCall(); a host tool that runs code
    // someone else registered (tc_get_status_image a status-image getter)
    // names that code's owner with setDeferralOwner().
    const void* owner = nullptr;
};
// The MCP state below is one per process, so it is defined non-inline in
// tcMCP.cpp: a hot reload guest on Windows would otherwise get its own copy of
// each, and a tool handler it runs would defer into a DeferralState the host
// never reads (#249; docs/ARCHITECTURE.md, "One instance per process").
DeferralState& deferralState();
void resetDeferralState() noexcept;
std::function<ReplyThunk()> retainDeferredCode(std::function<ReplyThunk()>,
                                            std::shared_ptr<::trussc::internal::AppOwner>, json id = nullptr);
const void* resolveDeferredTarget(const void* targetWindow);
void retireDeferredTarget(const void* targetWindow) noexcept;

// Call after deferring, in a tool handler whose deferred producer runs code
// registered under another owner (see DeferralState::owner).
inline void setDeferralOwner(const void* owner) { deferralState().owner = owner; }

// Pending replies. Guard every access with deferredResponsesMutex(): the main
// thread drains them, but std::exit on an HTTP worker answers them from
// stopHttpServer() on that worker. Whoever takes an entry out of the list
// answers it, outside the lock, so each promise is set exactly once.
std::vector<DeferredResponse>& deferredResponses();
std::mutex& deferredResponsesMutex();

} // namespace internal

// Compatibility query for apps and supervisors that detect input control.
inline bool isDebuggerEnabled() {
    return getToolLevel() >= ToolLevel::Control;
}

// Call inside a tool handler to defer its result until just after the next
// present(). `produce` returns the tool's content json (same shape a normal
// handler would return) and runs at that safe readback point. `targetWindow`
// (an explicit WindowContext*) picks whose present() runs the producer.
// Omission selects the owning App's Window, otherwise the sole open Window.
// Zero or ambiguous unowned Windows fail here, before a reply is deferred.
inline void deferToolResultUntilAfterFrame(std::function<json()> produce,
                                           const void* targetWindow = nullptr) {
    internal::deferralState().appOwner = ::trussc::internal::currentAppOwner();
    targetWindow = internal::resolveDeferredTarget(targetWindow);
    internal::deferralState().requested = true;
    internal::deferralState().produce = std::move(produce);
    internal::deferralState().target = targetWindow;
}

// Two-stage variant for tools whose result is expensive to build (e.g.
// tc_get_thumbnail): `mainStage` runs at the afterFrame safe point on the MAIN
// thread — do the GPU readback there and nothing else — and the closure it
// returns runs on the HTTP worker thread that is already sitting blocked on
// this reply. Put the heavy work (downscale, encode) in that closure and the
// frame loop never pays for it.
// `targetWindow`: as for deferToolResultUntilAfterFrame().
inline void deferToolResultTwoStage(std::function<std::function<json()>()> mainStage,
                                    const void* targetWindow = nullptr) {
    internal::deferralState().appOwner = ::trussc::internal::currentAppOwner();
    targetWindow = internal::resolveDeferredTarget(targetWindow);
    internal::deferralState().twoStageRequested = true;
    internal::deferralState().produceTwoStage = std::move(mainStage);
    internal::deferralState().target = targetWindow;
}

namespace internal {

// A shared_ptr deleter must not build temporary JSON trees: their noexcept
// destructors can allocate. Keep error serialization on the HTTP worker, as
// for other reply thunks, and publish an allocation-free fallback on failure.
inline void completeRetiredResponse(DeferredResponse& entry) noexcept {
    try {
        completeResponse(entry.response, [id = std::move(entry.id)] {
            return toolError(id, "the app code behind this reply was unloaded by a hot reload "
                                 "before the reply was produced");
        });
    } catch (...) {
        try {
            completeResponse(entry.response, +[]() -> std::string { throw AdmissionError(); });
        } catch (...) {} // A dropped promise/producer releases its captures.
    }
}

// Run the deferred main stages aimed at an explicit WindowContext and unblock
// their HTTP workers. Call right after that Window's present(). Only the main
// part of each envelope runs here; the returned thunk executes on the HTTP
// worker. A null target performs process deadline maintenance only. Non-presenting ticks use
// frameAvailable=false: retry producers later, but still answer expired
// requests without attempting a GPU readback.
inline void drainDeferredResponses(const void* targetWindow = nullptr,
                                   bool frameAvailable = true,
                                   std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
                                   bool expireAllTargets = true) {
    std::vector<internal::DeferredResponse> ready;
    std::vector<internal::DeferredResponse> expired;
    {
        std::lock_guard<std::mutex> lock(internal::deferredResponsesMutex());
        auto& list = internal::deferredResponses();
        if (list.empty()) return;
        std::vector<internal::DeferredResponse> keep;
        // Allocate every destination before moving a producer out of pending.
        // On failure leave all promises/producers intact for the next frame.
        try {
            ready.reserve(list.size());
            expired.reserve(list.size());
            keep.reserve(list.size());
        } catch (const std::bad_alloc&) {
            return;
        }
        for (auto& d : list) {
            if (frameAvailable && d.target == targetWindow) ready.push_back(std::move(d));
            else if (((expireAllTargets && !targetWindow) || d.target == targetWindow) && now >= d.deadline) expired.push_back(std::move(d));
            else keep.push_back(std::move(d));
        }
        list.swap(keep);
    }
    // Answer outside the lock: a worker woken here may call std::exit, whose
    // stopHttpServer() takes the lock to answer what is still pending.
    for (auto& d : ready) {
        if (d.ownerRetired) {
            completeRetiredResponse(d);
            continue;
        }
        try {
            internal::completeResponse(d.response, d.makeEnvelope());
        } catch (const std::bad_alloc&) {
            completeResponse(d.response, [id = std::move(d.id)] { return allocationErrorReply(id); });
        } catch (const AdmissionError&) {
            completeTerminalResponse(d.response, [&] {
                return toolError(d.id, "MCP admission limit exceeded");
            }, d.id);
        } catch (const std::exception& e) {
            completeTerminalResponse(d.response, [&] {
                return toolError(d.id, std::string("deferred response failed: ") + e.what());
            }, d.id);
        } catch (...) {
            completeTerminalResponse(d.response, [&] {
                return toolError(d.id, "deferred response failed");
            }, d.id);
        }
    }
    for (auto& d : expired) {
        completeTerminalResponse(d.response, [&] {
            return toolError(d.id,
                "the window rendered no frame within 5 s (minimized, hidden or closed?)");
        }, d.id);
    }
}

inline bool hasDeferredResponses() {
    std::lock_guard<std::mutex> lock(internal::deferredResponsesMutex());
    return !internal::deferredResponses().empty();
}

inline bool hasDeferredResponsesForTarget(const void* target) {
    std::lock_guard<std::mutex> lock(internal::deferredResponsesMutex());
    for (const auto& response : internal::deferredResponses()) if (response.target == target) return true;
    return false;
}

} // namespace internal

// ---------------------------------------------------------------------------
// Types & Interfaces
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Registration owners (#227)
// ---------------------------------------------------------------------------
// While an owner is set, every tool / resource / status entry registered is
// tagged with it, and removeRegistrationsOwnedBy() takes them all out again.
// Every App has an independent owner, including its complete constructor,
// pending request, callbacks and lazy addon registrations. Ending or cancelling
// it retires that owner without affecting sibling Apps. Runtime registrations
// made without an App owner are permanent until their service removes them.
// Both accessors are defined in tcMCP.cpp: the host sets the owner and a guest
// registers under it, so they must see the same one on every platform.
namespace internal {
const void*& registrationOwner();
// Registries defined elsewhere (the status registries of tcStandardTools.h,
// in tcMCP.cpp) hook their own cleanup in here the first time they are used.
std::vector<std::function<void(const void*)>>& ownerCleanupHooks();
inline void setRegistrationOwner(const void* owner) { registrationOwner() = owner; }
// Owners whose registrations are being retired right now, innermost first
// (stack-linked, so marking one allocates nothing). Defined in tcMCP.cpp with
// the other registry state. Main thread only, like registration itself.
struct RetiringOwner {
    const void* owner;
    RetiringOwner* prev;
};
RetiringOwner*& retiringOwners();
inline bool ownerRetiring(const void* owner) {
    if (!owner) return false;
    for (auto* node = retiringOwners(); node; node = node->prev)
        if (node->owner == owner) return true;
    return false;
}
// Reporting state of refusals (host-owned, defined in tcMCP.cpp like the other
// registry state). A log listener may register again (a warning counter that
// publishes a status), and under a retiring owner that is refused and reported
// too, which would recurse through the listener without end.
struct RefusalReport {
    bool reporting = false;   // a refusal Warning is being logged right now
    size_t suppressed = 0;    // refusals made inside that logging, not logged one by one
};
RefusalReport& refusalReport();
// A registration under a retiring owner is refused: a capture destroyed during
// retirement can register again, and what it publishes would outlive the App it
// captures. The refusal is reported with a Warning naming the entry. Logging is
// allocation-failure safe (logAllocationFailure), so this holds on the
// retirement path too.
inline bool refuseRetiringRegistration(const char* kind, const std::string& name) {
    auto app = ::trussc::internal::currentAppOwner();
    if (!ownerRetiring(registrationOwner()) && !(app && (app->ended || app->retiring))) return false;
    auto& report = refusalReport();
    if (report.reporting) {   // refused from a log listener: counted, summarized below
        ++report.suppressed;
        return true;
    }
    // Bound (reentrancy): while `reporting` is held, a refusal made by a log
    // listener only increments a counter, so one outer refusal causes at most two
    // log calls (the Warning and one summary of the refusals it provoked), and
    // the guard is dropped before returning. Refusals provoked by the summary's
    // own listener calls are counted but not logged: the listener is their cause.
    report.reporting = true;
    struct Release {
        RefusalReport& report;
        ~Release() { report.reporting = false; report.suppressed = 0; }
    } release{report};
    logWarning("MCP") << kind << " '" << name << "' was not registered: its owner was unloading";
    if (const size_t nested = report.suppressed) {
        report.suppressed = 0;
        logWarning("MCP") << nested << " more registrations were refused: its owner was unloading";
    }
    return true;
}
inline void removeRegistrationsOwnedBy(const void* owner);   // after Server
} // namespace internal

/// A named argument in a tool input schema.
struct ToolArg {
    std::string name;
    std::string type; // "string", "int", "float", "boolean", "object", "array"
    std::string description;
    bool required = true;
};

/// Tool metadata, required level and main-thread handler.
class Tool {
public:
    std::string name;
    std::string description;
    ToolLevel level = ToolLevel::Public;
    std::vector<ToolArg> args;
    std::function<json(const json&)> handler;
    const void* owner = nullptr;   // see registrationOwner()

    json getSchema() const {
        json schema = {
            {"type", "object"},
            {"properties", json::object()},
            {"required", json::array()}
        };

        for (const auto& arg : args) {
            schema["properties"][arg.name] = {
                {"type", arg.type},
                {"description", arg.description}
            };
            // Nullable file path used by tc_analyze_image's optional save.
            if (arg.type == "string|null")
                schema["properties"][arg.name]["type"] = json::array({"string", "null"});
            if (arg.required) {
                schema["required"].push_back(arg.name);
            }
        }
        return schema;
    }
};

/// URI-addressed resource metadata and text producer.
class Resource {
public:
    std::string uri;
    std::string name;
    std::string mimeType;
    std::string description;
    std::function<std::string()> handler; // Returns content (text or base64)
    const void* owner = nullptr;   // see registrationOwner()
};

// ---------------------------------------------------------------------------
// MCP Server Core
// ---------------------------------------------------------------------------

namespace internal {

// Input handlers use this only for validation before delivering any events.
class InputArgumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Server {
public:
    // The one registry every tool / resource goes into. Defined in tcMCP.cpp,
    // so a hot reload guest registers into the host's server (#249).
    static Server& instance();

    // --- Registration API ---

    void registerTool(const Tool& tool) {
        if (internal::refuseRetiringRegistration("tool", tool.name)) return;
        // Last registration wins. Warn, because silently replacing a standard
        // tool (they register before setup()) is a hard bug to spot.
        if (tools_.count(tool.name)) {
            logWarning("MCP") << "tool '" << tool.name << "' re-registered; previous handler replaced";
        }
        Tool prepared = tool;
        prepared.owner = internal::registrationOwner();
        prepared.handler = ::trussc::internal::appOwnedCallback(tool.handler);
        tools_.insert_or_assign(tool.name, std::move(prepared));
    }

    void registerResource(const Resource& res) {
        if (internal::refuseRetiringRegistration("resource", res.uri)) return;
        Resource prepared = res;
        prepared.owner = internal::registrationOwner();
        prepared.handler = ::trussc::internal::appOwnedCallback(res.handler);
        resources_.insert_or_assign(res.uri, std::move(prepared));
    }

    bool hasTool(const std::string& name) const { return tools_.count(name) != 0; }

    // Drop every tool / resource registered under `owner` (non-null).
    // Reentrancy-safe and allocation-free: erasing an entry destroys its
    // captures, and a capture can run arbitrary code (the last strong reference
    // to an App, whose deleter may retire registrations again). So each match
    // is extracted from the map first, and its captures die only after the map
    // is consistent and no iterator is held; the scan then restarts, because a
    // nested retirement may have erased any other entry.
    void removeOwnedBy(const void* owner) {
        if (!owner) return;
        for (;;) {
            auto it = std::find_if(tools_.begin(), tools_.end(),
                [owner](const auto& entry) { return entry.second.owner == owner; });
            if (it == tools_.end()) break;
            auto retired = tools_.extract(it);
        }   // captures destroyed here
        for (;;) {
            auto it = std::find_if(resources_.begin(), resources_.end(),
                [owner](const auto& entry) { return entry.second.owner == owner; });
            if (it == resources_.end()) break;
            auto retired = resources_.extract(it);
        }
    }

    // --- Message Processing (returns JSON-RPC response string) ---

    std::string processMessage(const std::string& rawMessage, bool http = false, json* requestId = nullptr) {
        json j;
        try {
            j = parseRequest(rawMessage);
            if (hasInvalidRequestId(j)) return makeError(json(nullptr), -32600, "Invalid Request");

            if (requestId && j.is_object() && j.contains("id")) *requestId = j["id"];
            // Validate JSON-RPC
            if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") {
                return makeError(json(nullptr), -32600, "Invalid JSON-RPC");
            }

            // Handle Request
            if (j.contains("method")) {
                return handleRequest(j);
            }
            // Handle Response (not implemented for server role)
            else if (j.contains("result") || j.contains("error")) {
                return ""; // Ignore
            }

            return "";

        } catch (const AdmissionError&) {
            if (http) throw;
            return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32000,\"message\":\"MCP admission limit exceeded\"}}";
        } catch (const std::bad_alloc&) {
            return allocationErrorReply(j.is_object() && j.contains("id") ? j["id"] : json(nullptr));
        } catch (const std::exception& e) {
            trussc::logError("MCP") << "JSON parse error: " << e.what();
            return makeError(json(nullptr), -32700, std::string("Parse error: ") + e.what());
        }
    }

private:
    std::map<std::string, Tool> tools_;
    std::map<std::string, Resource> resources_;

    std::string handleRequest(const json& req) {
        std::string method = req["method"];
        auto id = req.contains("id") ? req["id"] : json(json::value_t::discarded);

        if (method == "initialize") {
            return handleInitialize(req, id);
        } else if (method == "tools/list") {
            return handleToolsList(req, id);
        } else if (method == "tools/call") {
            return handleToolsCall(req, id);
        } else if (method == "resources/list") {
            return handleResourcesList(req, id);
        } else if (method == "resources/read") {
            return handleResourcesRead(req, id);
        } else {
            if (!id.is_discarded()) {
                return makeError(id, -32601, "Method not found: " + method);
            }
            return "";
        }
    }

    std::string handleInitialize(const json& req, const json& id) {
        json result = {
            {"protocolVersion", "2024-11-05"},
            {"server", {
                {"name", "TrussC App"},
                {"version", getVersion()}
            }},
            {"capabilities", {
                {"tools", {}},
                {"resources", {}}
            }}
        };
        return internal::makeResult(id, result);
    }

    std::string handleToolsList(const json& req, const json& id) {
        json toolList = json::array();
        for (const auto& [name, tool] : tools_) {
            if (tool.level > getToolLevel() || (internal::healthOnly() && name != "tc_get_health")) continue;
            json entry = json::object();
            entry["name"] = tool.name;
            entry["description"] = tool.description;
            entry["inputSchema"] = tool.getSchema();
            toolList.push_back(std::move(entry));
        }
        json result = json::object();
        result["tools"] = std::move(toolList);
        return internal::makeResult(id, std::move(result));
    }

    std::string handleToolsCall(const json& req, const json& id) {
        auto params = req["params"];
        std::string name = params["name"];
        auto args = params["arguments"];

        if (tools_.find(name) == tools_.end()) {
            return makeError(id, -32601, "Tool not found: " + name);
        }

        const auto granted = getToolLevel();
        if (internal::healthOnly() && name != "tc_get_health" && tools_.at(name).level == ToolLevel::Public) {
            return internal::toolError(id, "Tool '" + name +
                "' requires an explicit code opt-in, current level is Public (health only)");
        }
        if (tools_.at(name).level > granted) {
            return internal::toolError(id, "Tool '" + name + "' requires " +
                internal::toolLevelName(tools_.at(name).level) +
                ", current level is " + internal::toolLevelName(granted));
        }

        // Wrap a tool's content json into a full JSON-RPC result string.
        auto formatResult = [id](const json& content) -> std::string {
            json result;
            if (content.is_array() && content.size() > 0 && content[0].contains("type")) {
                result = {{"content", content}};
            } else {
                result = {{"content", {{
                    {"type", "text"},
                    {"text", internal::dumpReply(content)}
                }}}};
            }
            return internal::makeResult(id, result);
        };

        try {
            auto& ds = internal::deferralState();
            ds.requested = false;
            ds.twoStageRequested = false;
            ds.id = id;
            ds.owner = tools_[name].owner;   // the handler may name another (setDeferralOwner)

            // Execute tool handler (may call deferToolResultUntilAfterFrame()
            // or deferToolResultTwoStage())
            json content = tools_[name].handler(args);

            // Handler asked to produce its result after the next present().
            if (ds.requested) {
                auto produce = std::move(ds.produce);
                ds.requested = false;
                ds.hasEnvelope = true;
                // Everything runs on main at drain time; the worker thunk just
                // hands back the prebuilt string.
                ds.envelope = [formatResult, produce]() -> internal::ReplyThunk {
                    std::string reply = formatResult(produce());
                    return [reply]() { return reply; };
                };
                return std::string();  // processHttpQueue() stashes the reply
            }

            // Two-stage: main stage at drain time (readback), returned closure
            // — wrapped so the JSON-RPC formatting ALSO happens on the worker —
            // executes on the blocked HTTP worker thread.
            if (ds.twoStageRequested) {
                auto mainStage = std::move(ds.produceTwoStage);
                ds.twoStageRequested = false;
                ds.hasEnvelope = true;
                ds.envelope = [id, formatResult, mainStage]() -> internal::ReplyThunk {
                    std::function<json()> workerStage = mainStage();
                    return carryCaptureAdmission([id, formatResult, workerStage = std::move(workerStage)]() -> std::string {
                        try {
                            return formatResult(workerStage());
                        } catch (const std::bad_alloc&) {
                            return allocationErrorReply(id);
                        } catch (const std::exception& e) {
                            return internal::toolError(id, std::string("deferred worker stage failed: ") + e.what());
                        }
                    }, {});
                };
                return std::string();  // processHttpQueue() stashes the reply
            }

            return formatResult(content);

        } catch (const internal::RequestRejected& e) {
            return makeError(id, -32000, e.what());
        } catch (const InputArgumentError& e) {
            return makeError(id, -32602, e.what());
        } catch (const AdmissionError&) {
            return makeError(id, -32000, "MCP admission limit exceeded");
        } catch (const std::bad_alloc&) {
            return allocationErrorReply(id);
        } catch (const std::exception& e) {
            return internal::toolError(id, std::string("Tool execution error: ") + e.what());
        } catch (...) {
            return internal::toolError(id, "Tool execution error: unknown exception");
        }
    }

    std::string handleResourcesList(const json& req, const json& id) {
        json resList = json::array();
        for (const auto& [uri, res] : resources_) {
            if (internal::healthOnly()) continue;
            json entry = json::object();
            entry["uri"] = res.uri;
            entry["name"] = res.name;
            entry["description"] = res.description;
            entry["mimeType"] = res.mimeType.empty() ? nullptr : json(res.mimeType);
            resList.push_back(std::move(entry));
        }
        json result = json::object();
        result["resources"] = std::move(resList);
        return internal::makeResult(id, std::move(result));
    }

    std::string handleResourcesRead(const json& req, const json& id) {
        std::string uri = req["params"]["uri"];
        if (internal::healthOnly())
            return makeError(id, -32000, "Resources require an explicit code opt-in (health only)");

        if (resources_.find(uri) == resources_.end()) {
            return makeError(id, -32602, "Resource not found: " + uri);
        }

        try {
            std::string content = resources_[uri].handler();
            json resourceContent = {
                {"uri", uri},
                {"mimeType", resources_[uri].mimeType}
            };
            resourceContent["text"] = content;
            return internal::makeResult(id, {{"contents", {resourceContent}}});

        } catch (const std::exception& e) {
            return makeError(id, -32000, std::string("Resource read error: ") + e.what());
        }
    }

    std::string makeError(const json& id, int code, const std::string& message) {
        if (id.is_discarded()) return "";
        json res = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {
                {"code", code},
                {"message", message}
            }}
        };
        return internal::dumpReply(res);
    }
};

} // namespace internal

// ---------------------------------------------------------------------------
// HTTP Server (main thread synchronization via ThreadChannel + promise)
// ---------------------------------------------------------------------------

#ifndef __EMSCRIPTEN__

struct TC_PLATFORMS("macos,windows,linux,ios,android") McpRequest {
    TC_PLATFORMS("macos,windows,linux,ios,android") std::string body;
    // Carries a thunk the HTTP worker executes to obtain the reply string —
    // see internal::ReplyThunk. Normal replies are prebuilt (thunk just returns
    // them); two-stage tool replies do their heavy encode inside the thunk.
    TC_PLATFORMS("macos,windows,linux,ios,android") std::shared_ptr<std::promise<internal::ReplyThunk>> response;
};

namespace internal {

// HTTP server state, one per process (defined in tcMCP.cpp, see above).
ThreadChannel<McpRequest>& getHttpChannel();
void trackHttpPromise(const std::shared_ptr<std::promise<ReplyThunk>>& promise, const std::string& body,
                      std::shared_future<ReplyThunk> future);
void untrackHttpPromise(const std::shared_ptr<std::promise<ReplyThunk>>& promise);
void resetHttpChannel();
std::unique_ptr<httplib::Server>& getHttpServer();
std::unique_ptr<std::thread>& getHttpThread();
std::atomic<int>& getHttpPort();

// Retain the first successful opener even after later grants/explicit opt-ins.
struct HttpListenerConfig {
    int requestedPort = 0;
    std::string host;
    std::string opener;
};
HttpListenerConfig& httpListenerConfig();

// Bearer token required on /mcp requests. Empty = no auth (localhost default).
std::string& mcpAuthToken();

// Whether the server is bound to a loopback address (the Host check applies).
std::atomic<bool>& mcpLoopbackOnly();

inline std::string asciiLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string trimSpaces(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// "localhost:8080" / "127.0.0.1" / "[::1]:8080" -> is the host part loopback?
inline bool isLoopbackHostHeader(const std::string& hostHeader) {
    std::string h = asciiLower(trimSpaces(hostHeader));
    std::string name;
    if (!h.empty() && h[0] == '[') {                 // [v6]:port
        size_t close = h.find(']');
        if (close == std::string::npos) return false;
        name = h.substr(0, close + 1);
    } else {
        size_t colon = h.find(':');
        name = (colon == std::string::npos) ? h : h.substr(0, colon);
    }
    return name == "localhost" || name == "127.0.0.1" || name == "[::1]";
}

// Only the server's own origins. The server is for native MCP clients: it
// sends no CORS headers, so no web page can call it, neither directly nor
// through a dev-server proxy that forwards the page's Origin (#346).
inline bool isAllowedOrigin(const std::string& origin, int port) {
    std::string o = asciiLower(trimSpaces(origin));
    while (!o.empty() && o.back() == '/') o.pop_back();
    const std::string p = std::to_string(port);
    return o == "http://localhost:" + p || o == "http://127.0.0.1:" + p || o == "http://[::1]:" + p;
}

// "application/json", optionally with parameters ("; charset=utf-8").
inline bool isJsonContentType(const std::string& contentType) {
    std::string t = contentType.substr(0, contentType.find(';'));
    return asciiLower(trimSpaces(t)) == "application/json";
}

// Equality without an early exit: every byte of the longer input is visited
// and the differences are OR-ed together, so the time taken does not depend
// on where the inputs first differ. A length mismatch is folded into the
// result instead of returning early; the time still follows the longer
// length.
inline bool constantTimeEquals(std::string_view a, std::string_view b) {
    const size_t n = a.size() > b.size() ? a.size() : b.size();
    unsigned int diff = (a.size() == b.size()) ? 0u : 1u;
    for (size_t i = 0; i < n; ++i) {
        unsigned char x = i < a.size() ? (unsigned char)a[i] : 0;
        unsigned char y = i < b.size() ? (unsigned char)b[i] : 0;
        diff |= (unsigned int)(x ^ y);
    }
    return diff == 0;
}

// Authorization header value "Bearer <token>". The scheme is not secret and
// is checked first; the token part goes through constantTimeEquals().
inline bool bearerTokenMatches(std::string_view header, std::string_view token) {
    constexpr std::string_view scheme = "Bearer ";
    if (header.substr(0, scheme.size()) != scheme) return false;
    return constantTimeEquals(header.substr(scheme.size()), token);
}

void rejectRequest(httplib::Response& res, int status, const std::string& why);
bool checkRequest(const httplib::Request& req, httplib::Response& res, bool requireJson);

} // namespace internal

namespace internal {
// Serializes enqueue against close + drain. A worker arriving during shutdown
// receives a reply directly; it must never wait on an unserviceable promise.
std::mutex& httpQueueMutex();
inline std::string normalExitMessage() {
    const auto reason = trussc::internal::exitReason();
    return "the app is exiting normally" + (reason.empty() ? std::string() : " (reason=" + reason + ")");
}
inline std::string normalExitReply(const std::string& body) {
    auto request = parseRequest(body);
    if (hasInvalidRequestId(request)) return dumpReply(json{{"jsonrpc", "2.0"}, {"id", nullptr},
        {"error", {{"code", -32600}, {"message", "Invalid Request"}}}});
    if (request.is_object() && !request.contains("id")) return "";
    json reply{{"jsonrpc", "2.0"},
        {"error", {{"code", -32000}, {"message", normalExitMessage()}}}};
    // Move the depth-bounded ID, avoiding a recursive copy before serialization.
    reply["id"] = request.is_object() && request.contains("id")
        ? std::move(request["id"]) : json(nullptr);
    return dumpReply(reply);
}
inline void enqueueHttpRequest(McpRequest request, std::shared_future<ReplyThunk> future = {}) {
    {
        std::lock_guard<std::mutex> lock(httpQueueMutex());
        if (!getHttpChannel().isClosed()) {
            trackHttpPromise(request.response, request.body, std::move(future));
            getHttpChannel().send(std::move(request));
            return;
        }
    }
    completeTerminalResponse(request.response, [&] {
        return normalExitReply(request.body);
    });
}
} // namespace internal

// Start HTTP server.
//   port  : 0 = OS auto-assign, else fixed port
//   host  : "127.0.0.1" (default) keeps it loopback-only. The default is an
//           address, not "localhost": what "localhost" resolves to differs
//           between OSes, so the default is one address everywhere. Pass
//           "localhost" or "::1" for those, or "0.0.0.0" to expose on all
//           interfaces.
//   token : bearer token required on /mcp. MUST be non-empty when host is not
//           a loopback address — binding non-local without a token is refused
//           (fail-closed) so input injection is never silently network-exposed.
namespace internal {
void startHttpServerImpl(int port, const std::string& host,
                         const std::string& token, bool healthEndpoint,
                         const char* opener);
} // namespace internal

// The first opener chooses the listener's address and token. To choose them
// explicitly, call this before setToolLevel(). A later mismatched call logs
// one Error and changes nothing; it never restarts the listener. Matching
// parameters reuse it (0 repeats an original OS-assigned port request; the
// actual assigned port also matches). A matching explicit opt-in still opens
// Public app tools on an environment health-only listener.
TC_PLATFORMS("macos,windows,linux,ios,android") void startHttpServer(int port = 0, const std::string& host = "127.0.0.1",
                     const std::string& token = "");

// Stop HTTP server
TC_PLATFORMS("macos,windows,linux,ios,android") void stopHttpServer();

// Process HTTP request queue (call every frame from main thread)
TC_PLATFORMS("macos,windows,linux,ios,android") inline void processHttpQueue() {
    McpRequest req;
    while (internal::getHttpChannel().tryReceive(req)) {
        internal::resetDeferralState();
        struct ResetDeferral { ~ResetDeferral() { internal::resetDeferralState(); } } resetDeferral;
        json requestId;
        try {
            auto& ds = internal::deferralState();
            ds.hasEnvelope = false;
            ds.target = nullptr;
            ds.owner = nullptr;
            std::string result;
            try {
                result = internal::Server::instance().processMessage(req.body, true, &requestId);
            } catch (const internal::AdmissionError&) {
                internal::completeResponse(req.response, []() -> std::string { throw internal::AdmissionError(); });
                continue;
            }
            if (ds.hasEnvelope && result.empty()) {
                // Tool deferred its reply until after present(): stash the promise
                // and answer it from drainDeferredResponses(). The HTTP worker stays
                // blocked on its future a few ms longer (correct, not a hang).
                internal::DeferredResponse d;
                d.id = std::move(ds.id);
                d.response = req.response;
                d.makeEnvelope = internal::retainDeferredCode(std::move(ds.envelope), std::move(ds.appOwner), d.id);
                d.target = ds.target;
                d.deadline = std::chrono::steady_clock::now() + internal::kTargetedDeferralTimeout;
                d.owner = ds.owner;
                bool closed;
                {
                    std::lock_guard<std::mutex> lock(internal::deferredResponsesMutex());
                    // Stop closes the channel before draining this list. Checking
                    // under the list lock also covers a handler finishing later.
                    closed = internal::getHttpChannel().isClosed();
                    if (!closed) internal::deferredResponses().push_back(std::move(d));
                }
                if (closed) {
                    internal::completeTerminalResponse(d.response, [&] {
                        return internal::toolError(d.id, "the MCP server shut down before the reply was produced");
                    }, d.id);
                }
                ds.hasEnvelope = false;
                ds.target = nullptr;
                continue;
            }
            if (result.empty()) {
                // Return empty JSON-RPC response for notifications
                result = "{}";
            }
            internal::completeResponse(req.response, [result = std::move(result)]() { return result; });
        } catch (const std::bad_alloc&) {
            internal::deferralState().hasEnvelope = false;
            internal::completeResponse(req.response, [id = std::move(requestId)] { return internal::allocationErrorReply(id); });
        }
    }
}

// Get the actual HTTP port (0 if not started)
TC_PLATFORMS("macos,windows,linux,ios,android") inline int getHttpPort() {
    return internal::getHttpPort().load();
}

// Legacy no-op kept for source compatibility.
[[deprecated("use setToolLevel(ToolLevel::Control); will be removed in v1.0.0")]]
TC_PLATFORMS("macos,windows,linux,ios,android") inline void enableDebugger() {}

#endif // __EMSCRIPTEN__

namespace internal {
inline void removeRegistrationsOwnedBy(const void* owner) {
    if (!owner) return;
    // From here on registrations under `owner` are refused (see
    // refuseRetiringRegistration), so no capture destroyed below can publish a
    // new entry that survives the retirement.
    RetiringOwner retiring{owner, retiringOwners()};
    retiringOwners() = &retiring;
    struct RetiringReset {
        RetiringOwner& node;
        ~RetiringReset() { retiringOwners() = node.prev; }
    } retiringReset{retiring};
    ::trussc::internal::TeardownSteps steps;
    steps.run("MCP owner retirement", [&] {
        std::lock_guard<std::mutex> lock(deferredResponsesMutex());
        for (auto& entry : deferredResponses())
            if (entry.owner == owner) entry.ownerRetired = true;
    });
    steps.run("MCP owned tools", [&] { internal::Server::instance().removeOwnedBy(owner); });
    steps.run("MCP owner hooks", [&] {
        // Index-based: a hook's captures can run code that registers hooks.
        for (size_t i = 0; i < ownerCleanupHooks().size(); ++i)
            steps.run("MCP owner cleanup hook", [&] {
                auto hook = ownerCleanupHooks()[i];
                hook(owner);
            });
    });
    // Bound (retirement): one extra sweep of every container, not a loop. The
    // retiring-owner guard refuses new registrations for `owner`, so entries can
    // only disappear while the passes above run and one sweep leaves none; a
    // survivor would mean a bypass of the guard, which the tests pin.
    steps.run("MCP owned tools (final sweep)", [&] { internal::Server::instance().removeOwnedBy(owner); });
    steps.run("MCP owner hooks (final sweep)", [&] {
        for (size_t i = 0; i < ownerCleanupHooks().size(); ++i)
            steps.run("MCP owner cleanup hook", [&] {
                auto hook = ownerCleanupHooks()[i];
                hook(owner);
            });
    });
    // An App can now end in Window's shared_ptr deleter before a reload.
    // Remove every producer even if building an error reply fails. Moving one
    // entry out and erasing it needs no vector growth; preserve host FIFO and
    // release captures / complete promises outside the mutex.
    static_assert(std::is_nothrow_move_constructible_v<DeferredResponse> &&
                  std::is_nothrow_move_assignable_v<DeferredResponse>);
    steps.run("MCP owned deferrals", [&] {
        for (;;) {
            std::optional<DeferredResponse> owned;
            {
                std::lock_guard<std::mutex> lock(deferredResponsesMutex());
                auto& pending = deferredResponses();
                auto found = std::find_if(pending.begin(), pending.end(),
                    [owner](const auto& entry) { return entry.owner == owner; });
                if (found == pending.end()) break;
                owned.emplace(std::move(*found));
                pending.erase(found);
            }
            completeRetiredResponse(*owned);
        }
    });
}
} // namespace internal

// Whether a tool with this name is currently registered.
inline bool hasTool(const std::string& name) {
    return internal::Server::instance().hasTool(name);
}

// ---------------------------------------------------------------------------
// Argument Type Traits & Builder Helpers
// ---------------------------------------------------------------------------

template<typename T> struct TypeName { static constexpr const char* value = "string"; };
template<> struct TypeName<int> { static constexpr const char* value = "integer"; };
template<> struct TypeName<float> { static constexpr const char* value = "number"; };
template<> struct TypeName<double> { static constexpr const char* value = "number"; };
template<> struct TypeName<bool> { static constexpr const char* value = "boolean"; };
template<> struct TypeName<json> { static constexpr const char* value = "object"; };

class ToolBuilder;
namespace internal {
ToolBuilder coreTool(Server& server, const std::string& name, const std::string& desc);
}

// Tool Builder
class ToolBuilder {
public:
    ToolBuilder(const std::string& name, const std::string& desc) {
        tool_.name = name;
        tool_.description = desc;
        if (name.rfind("tc_", 0) == 0) {
            logError("MCP") << "tool '" << name << "' rejected: tc_ is reserved for core tools";
            accepted_ = false;
        }
    }

    /// Declare the grant required to expose this tool.
    ToolBuilder& level(ToolLevel value) {
        tool_.level = value;
        return *this;
    }

    /// Add a typed input argument; required defaults to true.
    template<typename T>
    ToolBuilder& arg(const std::string& name, const std::string& desc, bool required = true) {
        tool_.args.push_back({name, TypeName<T>::value, desc, required});
        return *this;
    }

    // Simple bind: function receives (const json& args)
    void bind(std::function<json(const json&)> func) {
        tool_.handler = func;
        registerTool();
    }

    // Typed bind helpers (up to 4 args for simplicity)

    // 0 args
    void bind(std::function<json()> func) {
        tool_.handler = [func](const json&) { return func(); };
        registerTool();
    }

    // 1 arg
    template<typename T1>
    void bind(std::function<json(T1)> func) {
        auto argName1 = tool_.args[0].name;
        tool_.handler = [func, argName1](const json& args) {
            return func(args.at(argName1).get<T1>());
        };
        registerTool();
    }

    // 2 args
    template<typename T1, typename T2>
    void bind(std::function<json(T1, T2)> func) {
        auto argName1 = tool_.args[0].name;
        auto argName2 = tool_.args[1].name;
        tool_.handler = [func, argName1, argName2](const json& args) {
            return func(args.at(argName1).get<T1>(), args.at(argName2).get<T2>());
        };
        registerTool();
    }

    // 3 args
    template<typename T1, typename T2, typename T3>
    void bind(std::function<json(T1, T2, T3)> func) {
        auto a1 = tool_.args[0].name;
        auto a2 = tool_.args[1].name;
        auto a3 = tool_.args[2].name;
        tool_.handler = [func, a1, a2, a3](const json& args) {
            return func(args.at(a1).get<T1>(), args.at(a2).get<T2>(), args.at(a3).get<T3>());
        };
        registerTool();
    }

private:
    friend ToolBuilder internal::coreTool(internal::Server&, const std::string&, const std::string&);
    ToolBuilder(internal::Server& server, const std::string& name, const std::string& desc)
        : server_(&server) {
        tool_.name = name;
        tool_.description = desc;
    }
    void registerTool() {
        if (accepted_) (server_ ? *server_ : internal::Server::instance()).registerTool(tool_);
    }
    internal::Server* server_ = nullptr;
    bool accepted_ = true;
    Tool tool_;
};

namespace internal {
inline ToolBuilder coreTool(Server& server, const std::string& name, const std::string& desc) {
    return ToolBuilder(server, name, desc);
}
} // namespace internal

// Resource Builder
class ResourceBuilder {
public:
    ResourceBuilder(const std::string& uri, const std::string& name) {
        res_.uri = uri;
        res_.name = name;
    }

    /// Set the resource description.
    ResourceBuilder& desc(const std::string& d) {
        res_.description = d;
        return *this;
    }

    /// Set the resource MIME type.
    ResourceBuilder& mime(const std::string& m) {
        res_.mimeType = m;
        return *this;
    }

    /// Register the main-thread resource text producer.
    void bind(std::function<std::string()> func) {
        res_.handler = func;
        internal::Server::instance().registerResource(res_);
    }

private:
    Resource res_;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Build a tool registration; tc_ is reserved, tcx_ is free. Registration never opens HTTP.
inline ToolBuilder tool(const std::string& name, const std::string& desc) {
    return ToolBuilder(name, desc);
}

/// Build a URI-addressed resource registration; finish with bind().
inline ResourceBuilder resource(const std::string& uri, const std::string& name) {
    return ResourceBuilder(uri, name);
}

} // namespace mcp
} // namespace trussc
