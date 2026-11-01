// =============================================================================
// tcStandardTools.cpp - the standard MCP tool sets
// =============================================================================
// Core handlers are defined in the host library so input dispatch and window
// state stay shared with hot reload guests. Registration happens once through
// Server::instance(); the granted level controls exposure on every request.
// =============================================================================

#include <TrussC.h>
#include <deque>
#include <mutex>
#include "tcStandardTools.h"
#include "tcAnalyzeImage.h"
#include "tcAudioTools.h"

#include "tcUtils.h"
#include "tcTime.h"
#include "../events/tcCoreEvents.h"
#include "../graphics/tcPixels.h"
#include "stb/stb_image_write.h"
#include <cmath>
#include <cstdlib>
#include <limits>

// Platform bits for internal::currentPid() / internal::processRssBytes()
#if defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif !defined(__EMSCRIPTEN__)
#include <fstream>
#include <unistd.h>
#endif

// Forward declaration for stbi_write_png_to_mem (missing in older stb_image_write.h headers)
extern "C" unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len);

namespace trussc {
namespace {
template<class T>
void writeMcpMembers(T& object, Json& out, bool includeDerived) {
    JsonWriteReflector writer;
    writer.includeDerived = includeDerived;
    object.reflectMembers(writer);
    if (!writer.members.empty()) out["members"] = std::move(writer.members);
    if (includeDerived && !writer.derived.empty()) out["derived"] = std::move(writer.derived);
}
}

Json nodeToJson(Node& node, int maxDepth, bool includeDerived) {
    Json j = Json::object();
    j["type"] = node.getTypeName();
    if (node.hasName()) j["name"] = node.getName();
    j["id"] = node.getInstanceId();
    j["members"] = Json::object();
    writeMcpMembers(node, j, includeDerived);
    auto mods = node.getMods();
    if (!mods.empty()) {
        Json jmods = Json::array();
        for (Mod* m : mods) {
            Json jm = Json::object();
            Mod& mod = *m;
            jm["type"] = shortTypeName(typeid(mod));
            writeMcpMembers(mod, jm, includeDerived);
            jmods.push_back(std::move(jm));
        }
        j["mods"] = std::move(jmods);
    }
    if (node.getChildCount() > 0) {
        if (maxDepth == 0) {
            j["childCount"] = node.getChildCount();
        } else {
            Json children = Json::array();
            for (auto& c : node.getChildren()) {
                children.push_back(nodeToJson(*c, maxDepth < 0 ? -1 : maxDepth - 1, includeDerived));
            }
            // Move — an lvalue assignment would deep-copy the whole subtree
            // JSON at every tree level (O(n^2) on deep chains).
            j["children"] = std::move(children);
        }
    }
    return j;
}

} // namespace trussc

namespace trussc::mcp::internal {

namespace {

int inputInteger(const json& a, const char* name, int minimum, int maximum) {
    auto it = a.find(name);
    if (it != a.end() && it->is_number_integer()) {
        if (it->is_number_unsigned()) {
            const auto value = it->get<uint64_t>();
            if ((minimum <= 0 || value >= static_cast<uint64_t>(minimum)) &&
                value <= static_cast<uint64_t>(maximum)) return static_cast<int>(value);
        } else {
            const auto value = it->get<int64_t>();
            if (value >= minimum && value <= maximum) return static_cast<int>(value);
        }
    }
    throw InputArgumentError(std::string(name) + " must be an integer in [" +
        std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
}

int inputKey(const json& a) {
    const int key = inputInteger(a, "key", SAPP_KEYCODE_SPACE, SAPP_KEYCODE_MENU);
    // Match the 120 defined nonzero codes; numeric gaps are not keys.
    if (key == SAPP_KEYCODE_SPACE || key == SAPP_KEYCODE_APOSTROPHE ||
        (key >= SAPP_KEYCODE_COMMA && key <= SAPP_KEYCODE_9) ||
        key == SAPP_KEYCODE_SEMICOLON || key == SAPP_KEYCODE_EQUAL ||
        (key >= SAPP_KEYCODE_A && key <= SAPP_KEYCODE_RIGHT_BRACKET) ||
        key == SAPP_KEYCODE_GRAVE_ACCENT ||
        (key >= SAPP_KEYCODE_WORLD_1 && key <= SAPP_KEYCODE_WORLD_2) ||
        (key >= SAPP_KEYCODE_ESCAPE && key <= SAPP_KEYCODE_END) ||
        (key >= SAPP_KEYCODE_CAPS_LOCK && key <= SAPP_KEYCODE_PAUSE) ||
        (key >= SAPP_KEYCODE_F1 && key <= SAPP_KEYCODE_F25) ||
        (key >= SAPP_KEYCODE_KP_0 && key <= SAPP_KEYCODE_KP_EQUAL) ||
        (key >= SAPP_KEYCODE_LEFT_SHIFT && key <= SAPP_KEYCODE_MENU)) return key;
    throw InputArgumentError("key must be a defined nonzero sokol_app key code");
}

void validateInputModifiers(const json& a) {
    for (const char* flag : {"shift", "ctrl", "alt", "super"}) {
        auto it = a.find(flag);
        if (it != a.end() && !it->is_boolean())
            throw InputArgumentError(std::string(flag) + " must be a boolean");
    }
}

float inputFloat(const json& a, const char* name) {
    auto it = a.find(name);
    if (it != a.end() && it->is_number()) {
        const double value = it->get<double>();
        // Bound (abi): MCP coordinates and scroll deltas must fit finite float event fields. Overflow returns JSON-RPC -32602 before callbacks or state changes.
        if (std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max())
            return static_cast<float>(value);
    }
    throw InputArgumentError(std::string(name) + " must be a finite float");
}

int inputMouseButton(const json& a, bool move = false) {
    if (!a.contains("button")) return move ? -1 : SAPP_MOUSEBUTTON_LEFT;
    return inputInteger(a, "button", move ? -1 : SAPP_MOUSEBUTTON_LEFT, SAPP_MOUSEBUTTON_MIDDLE);
}

} // namespace

void downscaleImage(const unsigned char* src, int srcW, int srcH, int channels,
                    std::vector<unsigned char>& dst, int dstW, int dstH) {
    if (dstW <= 0 || dstH <= 0 || channels <= 0) throw std::runtime_error("invalid image dimensions");
    dst.resize(size_t(dstW) * dstH * channels);
    for (int y = 0; y < dstH; ++y) {
        int sy0 = int(size_t(y) * srcH / dstH);
        int sy1 = std::max(int(size_t(y + 1) * srcH / dstH), sy0 + 1);
        for (int x = 0; x < dstW; ++x) {
            int sx0 = int(size_t(x) * srcW / dstW);
            int sx1 = std::max(int(size_t(x + 1) * srcW / dstW), sx0 + 1);
            uint64_t acc[4] = {0, 0, 0, 0};
            for (int sy = sy0; sy < sy1; ++sy) {
                const unsigned char* p = src + (size_t(sy) * srcW + sx0) * channels;
                for (int sx = sx0; sx < sx1; ++sx, p += channels)
                    for (int c = 0; c < channels; ++c) acc[c] += p[c];
            }
            uint64_t n = uint64_t(sy1 - sy0) * (sx1 - sx0);
            unsigned char* o = &dst[(size_t(y) * dstW + x) * channels];
            for (int c = 0; c < channels; ++c) o[c] = (unsigned char)(acc[c] / n);
        }
    }
}

json pixelsToImageJson(const trussc::Pixels& px, const std::string& format,
                       int reqWidth, int quality) {
    int srcW = px.getWidth(), srcH = px.getHeight();
    int ch   = px.getChannels();
    int dstW = (reqWidth > 0) ? std::min(reqWidth, srcW) : srcW;
    int dstH = std::max(1, (int)std::lround((double)srcH * dstW / srcW));

    const unsigned char* data = px.getData();
    std::vector<unsigned char> scaled;
    if (dstW != srcW) {
        downscaleImage(px.getData(), srcW, srcH, ch, scaled, dstW, dstH);
        data = scaled.data();
    }

    std::vector<unsigned char> out;
    if (format == "png") {
        int len = 0;
        std::unique_ptr<unsigned char, decltype(&std::free)> png(
            stbi_write_png_to_mem(data, 0, dstW, dstH, ch, &len), &std::free);
        // The C encoder returned safely; throw only here, after all C frames ended.
        if (!png) throw std::bad_alloc();
        out.assign(png.get(), png.get() + len);
    } else {
        stbi_write_jpg_to_func(
            [](void* ctx, void* d, int size) {
                auto* v = static_cast<std::vector<unsigned char>*>(ctx);
                auto* b = static_cast<unsigned char*>(d);
                v->insert(v->end(), b, b + size);
            },
            &out, dstW, dstH, ch, data, quality);
    }
    if (out.empty()) {
        return json{{"status", "error"}, {"message", format + " encode failed"}};
    }
    return json{{"mimeType", format == "png" ? "image/png" : "image/jpeg"},
                {"data", trussc::toBase64(out)},
                {"width", dstW},
                {"height", dstH}};
}

json imageContentResult(json img) {
    if (!img.contains("data")) return img;  // error object
    json meta = json::object();
    for (auto& key : {"width", "height"}) {
        if (img.contains(key)) meta[key] = img[key];
    }
    return json::array({
        json{{"type", "image"}, {"data", img["data"]}, {"mimeType", img["mimeType"]}},
        json{{"type", "text"}, {"text", meta.dump()}},
    });
}

void addStatusEntry(StatusEntry entry) {
    entry.owner = mcp::internal::registrationOwner();
    auto& reg = statusRegistry();
    for (auto& e : reg) {
        if (e.name == entry.name) { e = std::move(entry); return; }
    }
    reg.push_back(std::move(entry));
}

int64_t currentPid() {
#if defined(_WIN32)
    return (int64_t)GetCurrentProcessId();
#elif defined(__EMSCRIPTEN__)
    return 0;
#else
    return (int64_t)getpid();
#endif
}

int64_t processRssBytes() {
#if defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        return (int64_t)info.resident_size;
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (int64_t)pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__EMSCRIPTEN__)
    return 0;
#else
    // Linux: /proc/self/statm field 2 = resident pages
    std::ifstream in("/proc/self/statm");
    long total = 0, resident = 0;
    if (in >> total >> resident) {
        return (int64_t)resident * sysconf(_SC_PAGESIZE);
    }
    return 0;
#endif
}

} // namespace trussc::mcp::internal

namespace trussc::mcp {

void status(const std::string& name, std::function<double()> getter) {
    internal::addStatusEntry({name, [getter]() { return json(getter()); }, false});
}

void status(const std::string& name, std::function<std::string()> getter) {
    internal::addStatusEntry({name, [getter]() { return json(getter()); }, false});
}

void statusGraph(const std::string& name, std::function<double()> getter) {
    internal::addStatusEntry({name, [getter]() { return json(getter()); }, true});
}

void statusImage(const std::string& name, std::function<trussc::Pixels()> getter) {
    auto& reg = internal::statusImageRegistry();
    const void* owner = mcp::internal::registrationOwner();
    for (auto& e : reg) {
        if (e.name == name) { e.getter = getter; e.owner = owner; return; }
    }
    reg.push_back({name, getter, owner});
}

void alert(const std::string& msg) {
    trussc::logWarning("alert") << msg;
    std::lock_guard<std::mutex> lock(internal::alertMutex());
    auto& q = internal::alertQueue();
    q.push_back(json{{"at", trussc::getTimestampString("%Y-%m-%dT%H:%M:%S")},
                     {"text", msg}});
    if (q.size() > 100) q.pop_front();
}

void registerInspectionTools() { setToolLevel(ToolLevel::ReadOnly); }

} // namespace trussc::mcp

namespace trussc::mcp::internal {

void fillKeyModifiers(KeyEventArgs& args, int key, bool down) {
    auto heldAfter = [key, down](int code) {
        return code == key ? down : isKeyPressed(code);
    };
    args.shift = heldAfter(SAPP_KEYCODE_LEFT_SHIFT)   || heldAfter(SAPP_KEYCODE_RIGHT_SHIFT);
    args.ctrl  = heldAfter(SAPP_KEYCODE_LEFT_CONTROL) || heldAfter(SAPP_KEYCODE_RIGHT_CONTROL);
    args.alt   = heldAfter(SAPP_KEYCODE_LEFT_ALT)     || heldAfter(SAPP_KEYCODE_RIGHT_ALT);
    args.super = heldAfter(SAPP_KEYCODE_LEFT_SUPER)   || heldAfter(SAPP_KEYCODE_RIGHT_SUPER);
}

void injectKeyDown(int key) {
    KeyEventArgs args;
    args.key = key;
    fillKeyModifiers(args, key, true);
    ::trussc::internal::currentWindowContext().keysPressed.insert(key);
    events().keyPressed.notify(args);
}

void injectKeyUp(int key) {
    KeyEventArgs args;
    args.key = key;
    fillKeyModifiers(args, key, false);
    ::trussc::internal::currentWindowContext().keysPressed.erase(key);
    events().keyReleased.notify(args);
}

json heldKeysJson() {
    std::vector<int> held(::trussc::internal::currentWindowContext().keysPressed.begin(),
                          ::trussc::internal::currentWindowContext().keysPressed.end());
    std::sort(held.begin(), held.end());
    return json(held);
}

} // namespace trussc::mcp::internal

namespace trussc::mcp {

void registerControlTools() { setToolLevel(ToolLevel::Control); }

void registerDebuggerTools() { setToolLevel(ToolLevel::Control); }

} // namespace trussc::mcp

namespace trussc {
namespace mcp {

namespace internal {

void registerStandardTools(Server& server) {
    registerAudioTools(server);

    // Resolve the optional MCP "window" arg (0 = main window; 1..N = open
    // secondary windows in the order tc_list_windows reports). Returns the
    // WindowContext to capture from, or null on a bad index or a hidden
    // window (error filled in).
    // Resolved when the request is handled; the capture itself then runs in
    // that window's own tick (deferral target), see drainPendingScreenshots().
    auto resolveWindowCtx = [](int windowIdx, json& err) -> trussc::internal::WindowContext* {
        if (windowIdx == 0) return &trussc::internal::mainWindowContext();
        auto wins = trussc::internal::openWindows();
        if (windowIdx < 0 || (size_t)windowIdx > wins.size()) {
            err = json{{"status", "error"},
                       {"message", "window index out of range (0=main, 1.." +
                                   std::to_string(wins.size()) + " secondary)"}};
            return nullptr;
        }
        trussc::Window* win = wins[(size_t)windowIdx - 1];
        // The OS reports the window hidden, and a hidden secondary window
        // runs no tick, so a capture deferred to it could only time out after
        // kTargetedDeferralTimeout (#347). Only this direct signal fails fast;
        // a window that turns hidden later still gets the timeout.
        if (win->isOccluded()) {
            err = json{{"status", "error"},
                       {"message", "window " + std::to_string(windowIdx) +
                                   " is not visible (the OS reports it hidden: minimized, "
                                   "covered, on another Space, or the screen locked or "
                                   "off), so it renders no frames: make it visible and "
                                   "retry"}};
            return nullptr;
        }
        return &win->context();
    };

    coreTool(server, "tc_list_windows", "List open windows (index 0 = main; use the index as the 'window' arg of tc_get_screenshot / tc_save_screenshot). Each secondary window has 'occluded': true while the OS reports it hidden (minimized, covered, on another Space, or the screen locked or off); it then renders no frames and cannot be captured.")
        .level(ToolLevel::ReadOnly)
        .bind(std::function<json()>([]() -> json {
            json arr = json::array();
            // The main window keeps rendering while hidden: no occluded field.
            arr.push_back(json{{"index", 0}, {"main", true},
                               {"width", getWindowWidth()}, {"height", getWindowHeight()}});
            int i = 1;
            for (auto* w : trussc::internal::openWindows()) {
                arr.push_back(json{{"index", i++}, {"main", false},
                                   {"title", w->getTitle()},
                                   {"width", w->getWidth()}, {"height", w->getHeight()},
                                   {"occluded", w->isOccluded()}});
            }
            return json{{"windows", arr}};
        }));

    // Lists and source lookup use the same process-wide lifetime registry.
    auto listImages = []() -> json {
        json arr = json::array();
        for (const auto& e : trussc::internal::debugObjects(trussc::internal::DebugObjectKind::Image)) {
            const auto& p = static_cast<Image*>(e.object)->getPixels();
            arr.push_back({{"index",e.index},{"name",e.name},{"width",p.getWidth()},
                           {"height",p.getHeight()},{"format",p.isFloat()?"float":"RGBA8"}});
        }
        json result = json::object();
        result["images"] = std::move(arr);
        return result;
    };
    auto listFbos = []() -> json {
        json arr = json::array();
        for (const auto& e : trussc::internal::debugObjects(trussc::internal::DebugObjectKind::Fbo)) {
            const auto& f = *static_cast<Fbo*>(e.object);
            arr.push_back({{"index",e.index},{"name",e.name},{"width",f.getWidth()},
                           {"height",f.getHeight()},{"format",isFloatFormat(f.getTextureFormat())?"float":"RGBA8"}});
        }
        json result = json::object();
        result["fbos"] = std::move(arr);
        return result;
    };
    coreTool(server, "tc_list_images", "List live Images: index, name, width, height, format (RGBA8 or float).")
        .level(ToolLevel::ReadOnly)
        .bind(std::function<json()>(listImages));
    coreTool(server, "tc_list_fbos", "List live Fbos: index, name, width, height, format (RGBA8 or float).")
        .level(ToolLevel::ReadOnly)
        .bind(std::function<json()>(listFbos));

    Tool analyze;
    analyze.level = ToolLevel::ReadOnly;
    analyze.name = "tc_analyze_image";
    analyze.description = "Analyze pixels without returning an image. Source: exactly one of {window:index}, {fbo:name or index}, {image:name or index}, {path:file}. Returns width, height, format (RGBA8/float), colorSpace (sRGB/linear), results in ops order. Colors are RGBA, 0-1 for bytes; float values are unchanged. Ops: pixel(x,y)->color; histogram(bins)->histogram[RGBA][bin] (0-1, outliers in end bins); count(color,tolerance OR min,max)->count,bbox,centroid; stats()->mean,min,max; grid(cols,rows)->colors[row][col]; diff(path,threshold,save optional)->count,bbox,maxDifference; line(x0,y0,x1,y1)->colors. Every op accepts rect:[x,y,w,h], default whole image. Coordinates are top-left, line includes endpoints and clips to rect. Bbox is [x,y,w,h]; empty matches give null bbox/centroid. Diff uses max absolute RGBA difference, compares raw values and counts strictly above threshold. Save paths follow tc_save_screenshot; float file output is clamped to 0-1, analysis is unchanged. Fbo/window capture runs after frame completion. Web Fbo readback and iOS float Fbo readback return errors; non-float Fbo formats are converted to RGBA8 for byte readback.";
    analyze.args = {{"source","object","Image source (exactly one selector)",true},
                    {"save","string|null","Optional capture file path; omitted/null writes nothing",false},
                    {"ops","array","Ordered image operations and their arguments",true}};
    analyze.handler = [resolveWindowCtx](const json& args) -> json {
        auto error = [](const std::string& message) { return json{{"status","error"},{"message",message}}; };
        try {
            internal::validateAnalyzeArguments(args);
            const auto& source = args.at("source");
            if (!source.is_object() || source.size()!=1) return error("source must contain exactly one selector");
            if (source.contains("path")) {
                Pixels p;
                if (!internal::loadMcpImage(p, trussc::internal::utf8ToPath(source.at("path").get<std::string>()))) return error("failed to load source image");
                return internal::analyzeImage(p,args,(p.isFloat()||p.getChannels()<=2)?"linear":"sRGB");
            }
            if (source.contains("image") || source.contains("fbo")) {
                const bool fbo = source.contains("fbo");
                const auto kind = fbo ? trussc::internal::DebugObjectKind::Fbo : trussc::internal::DebugObjectKind::Image;
                const auto& selector = source.at(fbo?"fbo":"image");
                bool byIndex = selector.is_number_unsigned() || selector.is_number_integer();
                uint64_t index = 0;
                std::string name;
                if (byIndex) {
                    if (selector.is_number_integer() && selector.get<int64_t>()<0) return error("negative object index");
                    index=selector.get<uint64_t>();
                } else {
                    name=selector.get<std::string>();
                    if (name.empty()) return error("use an index for unnamed objects");
                }
                void* object = nullptr;
                std::string matchingIndices;
                size_t matches = 0;
                for (const auto& e : trussc::internal::debugObjects(kind)) {
                    if ((byIndex && e.index==index)||(!byIndex && e.name==name)) {
                        if (matches++) matchingIndices += ", ";
                        matchingIndices += std::to_string(e.index);
                        object=e.object; index=e.index;
                    }
                }
                if (matches>1) return error("debug name " + json(name).dump() +
                    " is ambiguous (indices " + matchingIndices + "); use an index");
                if (!object && !byIndex && name.find_first_not_of("0123456789")==std::string::npos) {
                    index=std::stoull(name);
                    for (const auto& e : trussc::internal::debugObjects(kind))
                        if (e.index==index) object=e.object;
                }
                if (!object) return error("image source no longer exists or was not found");
                if (!fbo) {
                    const auto& p=static_cast<Image*>(object)->getPixels();
                    return internal::analyzeImage(p,args,(p.isFloat()||p.getChannels()<=2)?"linear":"sRGB");
                }
#ifdef __EMSCRIPTEN__
                return error("Fbo readback is unavailable on web");
#else
                auto* f=static_cast<Fbo*>(object);
                // Fbo::readPixels converts every non-float format to RGBA8 bytes.
#if defined(__APPLE__) && TARGET_OS_IPHONE
                if (isFloatFormat(f->getTextureFormat())) return error("float Fbo readback is unavailable on iOS");
#endif
                (void)f;
                // Resolve again after the frame: destruction or a move must not
                // leave a dangling pointer captured in the deferred producer.
                mcp::deferToolResultTwoStage([index,args,error]() -> std::function<json()> {
                    Fbo* current=nullptr;
                    for(const auto& e:trussc::internal::debugObjects(trussc::internal::DebugObjectKind::Fbo))
                        if(e.index==index) current=static_cast<Fbo*>(e.object);
                    auto p=std::make_shared<Pixels>();
                    bool ok=false;
                    const char* colorSpace="sRGB";
                    if(current && current->isAllocated()) {
                        bool floating=isFloatFormat(current->getTextureFormat());
                        if (floating || channelCount(current->getTextureFormat())<=2) colorSpace="linear";
                        p->allocate(current->getWidth(),current->getHeight(),4,floating?PixelFormat::F32:PixelFormat::U8);
                        if(floating) {
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
                            int ch=channelCount(current->getTextureFormat());
                            Pixels raw;
                            raw.allocate(current->getWidth(),current->getHeight(),ch,PixelFormat::F32);
                            ok=current->readPixelsFloat(raw.getDataF32());
                            if(ok) {
                                const size_t count=size_t(p->getWidth())*p->getHeight();
                                for(size_t i=0;i<count;++i) for(int k=0;k<4;++k)
                                    p->getDataF32()[i*4+k]=k<ch?raw.getDataF32()[i*ch+k]:(k==3?1.0f:0.0f);
                            }
#endif
                        } else ok=current->readPixels(p->getData());
                    }
                    return [p,args,ok,error,colorSpace]() -> json {
                        if(!ok) return error("Fbo disappeared or readback failed");
                        return internal::analyzeImage(*p,args,colorSpace);
                    };
                });
                return nullptr;
#endif
            }
            if(source.contains("window")) {
                if (!source.at("window").is_number_integer()) return error("window must be an integer");
                json err;
                auto* ctx=resolveWindowCtx(source.at("window").get<int>(),err);
                if(!ctx) return err;
                mcp::deferToolResultTwoStage([args,error]() -> std::function<json()> {
                    auto p=std::make_shared<Pixels>();
                    bool ok=captureWindow(*p);
                    return [p,args,ok,error]() -> json {
                        if(!ok) return error("failed to grab screen");
                        return internal::analyzeImage(*p,args,"sRGB");
                    };
                },ctx->isMain?nullptr:ctx);
                return nullptr;
            }
            return error("unknown image source");
        } catch(const std::exception& e) { return error(e.what()); }
    };
    server.registerTool(analyze);

    coreTool(server, "tc_get_screenshot", "Screenshot as Base64 PNG/JPEG. Defaults to full-resolution PNG; pass width for a downscaled monitoring thumbnail (aspect preserved, never upscales) and format 'jpg' for small payloads. Cheap to poll at any settings: only the framebuffer readback touches the frame loop — downscale + encode run on the HTTP worker thread, so polling does not stutter the app. A secondary window is captured only while visible: one tc_list_windows reports occluded fails at once, and one that renders no frame within 5 s fails then.")
        .level(ToolLevel::ReadOnly)
        .arg<std::string>("format", "'png' (default, lossless) or 'jpg'", false)
        .arg<int>("width", "Target width in pixels, aspect preserved (clamped 16-4096; never upscales; omit = full resolution)", false)
        .arg<int>("quality", "JPEG quality 1-100 (default 75; ignored for png)", false)
        .arg<int>("window", "Window index from tc_list_windows (default 0 = main; a secondary window must be visible)", false)
        .bind([resolveWindowCtx](const json& args) -> json {
            std::string format = "png";
            if (args.contains("format") && args.at("format").is_string()) {
                format = args.at("format").get<std::string>();
                if (format != "png" && format != "jpg" && format != "jpeg") {
                    return json{{"status", "error"},
                                {"message", "format must be 'png' or 'jpg'"}};
                }
                if (format == "jpeg") format = "jpg";
            }
            int reqWidth = 0;  // 0 = full resolution
            int quality  = 75;
            if (args.contains("width") && args.at("width").is_number())
                reqWidth = std::clamp(args.at("width").get<int>(), 16, 4096);
            if (args.contains("quality") && args.at("quality").is_number())
                quality = std::clamp(args.at("quality").get<int>(), 1, 100);
            const int windowIdx = args.value("window", 0);

            json err;
            auto* ctx = resolveWindowCtx(windowIdx, err);
            if (!ctx) return err;

            // Two-stage deferral: the main stage runs right after the TARGET
            // window's present(), inside that window's own tick, where its
            // drawable is current (mid-frame the framebuffer is blank —
            // drawing is deferred; from another window's tick a secondary
            // window's back buffer is not the one read, #243). It only grabs
            // the pixels; the returned closure — downscale + encode + Base64 —
            // runs on the HTTP worker blocked on this reply.
            mcp::deferToolResultTwoStage([format, reqWidth, quality]() -> std::function<json()> {
                auto px = std::make_shared<trussc::Pixels>();
                bool grabbed = trussc::captureWindow(*px);
                return [px, grabbed, format, reqWidth, quality]() -> json {
                    if (!grabbed) {
                        return json{{"status", "error"}, {"message", "Failed to grab screen"}};
                    }
                    return internal::imageContentResult(internal::pixelsToImageJson(*px, format, reqWidth, quality));
                };
            }, ctx->isMain ? nullptr : ctx);
            return json(nullptr);  // ignored — deferred result is sent instead
        });

    coreTool(server, "tc_save_screenshot", "Save screenshot to file. A secondary window is captured only while visible: one tc_list_windows reports occluded fails at once, and one that renders no frame within 5 s fails then.")
        .level(ToolLevel::ReadOnly)
        .arg<std::string>("path", "File path")
        .arg<int>("window", "Window index from tc_list_windows (default 0 = main; a secondary window must be visible)", false)
        .bind([resolveWindowCtx](const json& args) -> json {
            // JSON strings are UTF-8 — convert explicitly (fs::path(string)
            // would interpret them in the ACP on Windows)
            const std::string path = args.at("path").get<std::string>();
            const int windowIdx = args.value("window", 0);
            json err;
            auto* ctx = resolveWindowCtx(windowIdx, err);
            if (!ctx) return err;
            auto credit = acquireScreenshotCredit();
            const auto destination = trussc::internal::resolveScreenshotPath(
                trussc::internal::utf8ToPath(path));
            std::filesystem::path prepared;
            if (!trussc::internal::prepareOutputPath(destination, prepared, "Screenshot"))
                return json{{"status", "error"}, {"message", "Failed to prepare screenshot destination"}};
            // Both windows use MCP deferral: the slot lives until
            // capture, expiration, owner cleanup or shutdown releases the job.
            // App-authored saveScreenshot() keeps its independent path queue.
            mcp::deferToolResultUntilAfterFrame([windowIdx, destination, credit]() -> json {
                WindowCaptureAdmission admission(credit);
                bool ok = trussc::internal::captureWindowToFile(destination);
                if (ok) return json{{"status", "ok"}, {"path", trussc::internal::pathToUtf8(destination)}, {"window", windowIdx}};
                return json{{"status", "error"}, {"message", "Failed to capture window " + std::to_string(windowIdx)}};
            }, ctx->isMain ? nullptr : ctx);
            return json(nullptr);  // deferred result is sent instead
        });

    coreTool(server, "tc_get_status", "App-published ops status: values registered via mcp::status()/mcp::statusGraph() plus the names of mcp::statusImage() images. mode 'graph' means the value wants to be plotted over time. Empty when the app publishes nothing. Supervisors discover this tool via tools/list and forward the payload to their monitoring server.")
        .level(ToolLevel::ReadOnly)
        .bind(std::function<json()>([]() -> json {
            json values = json::array();
            for (auto& e : internal::statusRegistry()) {
                json v;
                try {
                    v = e.getter();
                } catch (...) {
                    continue;  // a throwing getter drops its entry, not the tool
                }
                values.push_back({{"name", e.name},
                                  {"value", v},
                                  {"mode", e.graph ? "graph" : "status"}});
            }
            json images = json::array();
            for (auto& e : internal::statusImageRegistry()) images.push_back(e.name);
            return json{{"values", values}, {"images", images}};
        }));

    coreTool(server, "tc_get_alerts", "Drain operator alerts raised by the app via mcp::alert(). Returns and CLEARS the pending list, so exactly one consumer receives each alert. Empty 'alerts' array when nothing is pending. Supervisors (anchorbolt start) poll this on the health cadence and forward entries to their notification sinks.")
        .level(ToolLevel::ReadOnly)
        .bind(std::function<json()>([]() -> json {
            std::deque<json> drained;
            {
                std::lock_guard<std::mutex> lock(internal::alertMutex());
                drained.swap(internal::alertQueue());
            }
            json arr = json::array();
            for (auto& a : drained) arr.push_back(std::move(a));
            return json{{"alerts", arr}};
        }));

    coreTool(server, "tc_get_status_image", "Fetch an app-published image registered via mcp::statusImage(), downscaled + JPEG-encoded like tc_get_screenshot (pixel grab on the main loop, encode on the HTTP worker — no frame stutter).")
        .level(ToolLevel::ReadOnly)
        .arg<std::string>("name", "Image name as listed by tc_get_status")
        .arg<int>("width", "Target width in pixels, aspect preserved (default 512, clamped 16-4096; never upscales)", false)
        .arg<int>("quality", "JPEG quality 1-100 (default 75)", false)
        .bind([](const json& args) -> json {
            std::string name = args.value("name", "");
            int reqWidth = 512;
            int quality  = 75;
            if (args.contains("width") && args.at("width").is_number())
                reqWidth = std::clamp(args.at("width").get<int>(), 16, 4096);
            if (args.contains("quality") && args.at("quality").is_number())
                quality = std::clamp(args.at("quality").get<int>(), 1, 100);

            std::function<trussc::Pixels()> getter;
            const void* getterOwner = nullptr;
            for (auto& e : internal::statusImageRegistry()) {
                if (e.name == name) { getter = e.getter; getterOwner = e.owner; break; }
            }
            if (!getter) {
                return json{{"status", "error"},
                            {"message", "unknown image '" + name + "' (see tc_get_status)"}};
            }
            mcp::deferToolResultTwoStage([getter, reqWidth, quality]() -> std::function<json()> {
                auto px = std::make_shared<trussc::Pixels>();
                bool ok = true;
                try {
                    *px = getter();  // app getter runs on the main loop
                } catch (...) {
                    ok = false;
                }
                if (ok && (px->getWidth() <= 0 || px->getHeight() <= 0 || !px->getData())) ok = false;
                return [px, ok, reqWidth, quality]() -> json {
                    if (!ok) {
                        return json{{"status", "error"},
                                    {"message", "image getter returned no pixels"}};
                    }
                    return internal::imageContentResult(internal::pixelsToImageJson(*px, "jpg", reqWidth, quality));
                };
            });
            // The getter is the registering app's code (a hot reload guest's):
            // unloading it answers this reply with an error instead of running it.
            mcp::internal::setDeferralOwner(getterOwner);
            return json(nullptr);  // ignored — deferred result is sent instead
        });

    coreTool(server, "tc_get_health", "Lightweight liveness snapshot: fps (measured average), frame count, uptime seconds, window size, TrussC version, pid, process RSS bytes, main-thread queue backlog, sokol-tracked bytes. Cheap enough to poll — reads counters only, touches no GPU state. pid lets a supervisor confirm it is talking to ITS child (port collisions); rssBytes is the number to graph for leak hunting; mainQueuePending is how many runOnMainThread / Deliver::Main calls this frame's drain started with (a number that keeps growing means workers queue faster than the app runs them).")
        .level(ToolLevel::Public)
        .bind(std::function<json()>([]() -> json {
            return json{{"status", "ok"},
                        {"buildKind", internal::isDevBuild() ? "dev" : "default"},
                        {"toolLevel", internal::toolLevelName(getToolLevel())},
                        {"fps", trussc::getFps()},
                        {"frameCount", trussc::getFrameCount()},
                        // Process uptime: the framework clock, which
                        // resetElapsedTimeCounter() does not touch (#229).
                        {"uptimeSec", trussc::internal::getUptimeSeconds()},
                        {"width", trussc::getWindowWidth()},
                        {"height", trussc::getWindowHeight()},
                        {"version", trussc::getVersion()},
                        {"pid", internal::currentPid()},
                        {"rssBytes", internal::processRssBytes()},
                        {"mainQueuePending", trussc::internal::getMainThreadQueuePending()},
                        {"memoryBytes", trussc::getSokolMemoryBytes()}};
        }));

    coreTool(server, "tc_get_audio_state", "Audio engine diagnostics (read-only; never starts the engine): running; initFailure (last failed init reason, miniaudio result, backend and requested device; omitted after success); stalled (no callback finished for 250 ms or 4 periods; meters and voice levels read zero); underrunFrames (silent output frames per stream voice, excluding startup); voicesStoppedByReinit (decoder could not reopen at the new rate); playingSounds {slot, path, streaming, position/duration s, volume, pan, speed, loop, paused, level = peak of the playback's output}, master peak / RMS (linear, before clipping) and clipped-sample count, plays dropped since startup by reason (polyphonyLimit = the maxPolyphony cap reached, streamLimit = a stream's maxPolyphony, decoderError, notRunning = no device), audio-thread cpuUsage / cpuUsagePeak (audio-thread time / audio time; 1.0 = a callback took as long as the audio it produced), and the output / input devices. Pass devices=false to skip the device enumeration (slow on some backends) when polling.")
        .level(ToolLevel::ReadOnly)
        .arg<bool>("devices", "Enumerate playback / capture devices (default true)", false)
        .bind([](const json& args) -> json {
            bool enumerate = true;
            if (args.contains("devices") && args.at("devices").is_boolean())
                enumerate = args.at("devices").get<bool>();

            auto& engine = trussc::AudioEngine::getInstance();
            const trussc::AudioStats st = engine.getStats();
            const auto dev = trussc::internal::audioDeviceReport(enumerate);

            json playingSounds = json::array();
            for (const auto& v : engine.getPlayingSounds()) {
                playingSounds.push_back({{"slot", v.slot}, {"path", trussc::internal::pathToDisplayUtf8(v.path)},
                                  {"streaming", v.streaming},
                                  {"position", v.position}, {"duration", v.duration},
                                  {"volume", v.volume}, {"pan", v.pan}, {"speed", v.speed},
                                  {"loop", v.loop}, {"paused", v.paused},
                                  {"level", st.stalled ? 0.0f : v.level}});
            }

            auto& mic = trussc::getMicInput();
            json r{{"status", "ok"},
                   {"running", engine.isInitialized()},
                   {"stalled", st.stalled},
                   {"underrunFrames", st.underrunFrames},
                   {"voicesStoppedByReinit", st.voicesStoppedByReinit},
                   {"output", {{"device", dev.outputDevice},
                               {"default", dev.outputIsDefault},
                               {"backend", dev.backend},
                               {"sampleRate", engine.getSampleRate()},
                               {"channels", engine.getChannels()},
                               {"requestedBufferSize", engine.getBufferSize()},  // 0 = backend default; the granted size is periodFrames
                               {"periodFrames", dev.periodFrames},
                               {"deviceSampleRate", dev.deviceSampleRate},
                               {"deviceChannels", dev.deviceChannels},
                               {"maxPolyphony", engine.getMaxPolyphony()}}},
                   {"input", {{"running", mic.isRunning()},
                              {"device", mic.getDeviceName()},
                              {"sampleRate", mic.getSampleRate()}}},
                   {"playingSounds", playingSounds},
                   {"master", {{"peak", st.peak}, {"rms", st.rms},
                               {"clippedSamples", st.clippedSamples}}},
                   {"dropped", {{"total", st.droppedPlays},
                                {"polyphonyLimit", st.droppedPolyphonyLimit},
                                {"streamLimit", st.droppedStreamLimit},
                                {"decoderError", st.droppedDecoderError},
                                {"notRunning", st.droppedNotRunning}}},
                   {"thread", {{"cpuUsage", st.cpuUsage}, {"cpuUsagePeak", st.cpuUsagePeak}}}};
            if (st.initFailure != trussc::AudioInitFailure::None) {
                const char* reason = "noBackend";
                if (st.initFailure == trussc::AudioInitFailure::DeviceOpen) reason = "deviceOpen";
                if (st.initFailure == trussc::AudioInitFailure::DeviceStart) reason = "deviceStart";
                r["initFailure"] = {{"reason", reason}, {"result", st.initFailureResult},
                                    {"backend", st.initFailureBackend}, {"device", dev.initFailureDevice}};
            }
            if (dev.enumerated) {
                auto list = [](const std::vector<trussc::AudioDeviceInfo>& in) {
                    json a = json::array();
                    for (const auto& d : in) a.push_back({{"name", d.name}, {"default", d.isDefault}});
                    return a;
                };
                r["devices"] = {{"playback", list(dev.playbackDevices)},
                                {"capture", list(dev.captureDevices)}};
            }
            return r;
        });

    // --- Recording tools (native encoder, no ffmpeg) ---

    coreTool(server, "tc_start_recording", "Start recording the window to a video file (the screenshot's video counterpart). Omit path for a timestamped file in the data dir; give duration for a fixed-length clip that auto-stops and finalizes itself.")
        .level(ToolLevel::ReadOnly)
        .arg<std::string>("path", "Output file path (relative paths resolve to the data dir). Omit for recording-<timestamp>.mp4/.mov in the data dir.", false)
        .arg<float>("duration", "Fixed length in seconds; the file auto-stops & finalizes at that length. Omit or 0 = unlimited (tc_stop_recording to end).", false)
        .arg<float>("fps", "Target frame rate (default 60; ProMotion frames are decimated to it)", false)
        .arg<std::string>("codec", "h264 (default) | hevc | prores422 | prores4444 (.mov, macOS)", false)
        .bind([](const json& args) -> json {
            trussc::VideoRecordSettings settings;
            if (args.contains("fps") && args.at("fps").is_number()) {
                settings.fps = args.at("fps").get<float>();
            }
            if (args.contains("duration") && args.at("duration").is_number()) {
                settings.duration = args.at("duration").get<float>();
            }
            std::string codec = args.value("codec", std::string());
            if      (codec == "hevc")       settings.codec = trussc::VideoCodec::HEVC;
            else if (codec == "prores422")  settings.codec = trussc::VideoCodec::ProRes422;
            else if (codec == "prores4444") settings.codec = trussc::VideoCodec::ProRes4444;
            else if (!codec.empty() && codec != "h264") {
                return json{{"status", "error"}, {"message", "unknown codec: " + codec}};
            }
            // Default output: recording-<timestamp> in the data dir. ProRes is a
            // .mov container; everything else is .mp4. startRecording() resolves
            // the relative path via getDataPath(); recordingPath() returns it.
            std::string path = args.value("path", std::string());
            if (path.empty()) {
                bool isProRes = settings.codec == trussc::VideoCodec::ProRes422 ||
                                settings.codec == trussc::VideoCodec::ProRes4444;
                path = "recording-" + trussc::getTimestampString("%Y-%m-%d-%H-%M-%S")
                     + (isProRes ? ".mov" : ".mp4");
            }
            auto ok = trussc::startRecording(trussc::internal::utf8ToPath(path), settings);
            json r{{"status", ok ? "ok" : "error"},
                   {"path", trussc::internal::pathToUtf8(trussc::recordingPath())},
                   {"fps", settings.fps},
                   {"codec", trussc::videoCodecName(settings.codec)}};
            if (settings.duration > 0.0f) r["duration"] = settings.duration;
            return r;
        });

    coreTool(server, "tc_stop_recording", "Stop the current recording and finalize the file. Wins over a pending fixed duration (finalizes immediately at the current length); a no-op if nothing is recording.")
        .level(ToolLevel::ReadOnly)
        .bind(std::function<json()>([]() -> json {
            if (!trussc::isRecording()) {
                // Retain an automatic stop's finalization failure too.
                auto result = trussc::stopRecording();
                return json{{"status", result ? "ok" : "error"}, {"recording", false},
                            {"message", result ? "not recording" : result.message}};
            }
            std::string path = trussc::internal::pathToUtf8(trussc::recordingPath());
            int frames = trussc::recordingFrameCount();
            float fps  = trussc::internal::globalScreenRecorder().writer().getFps();
            auto result = trussc::stopRecording();
            json r{{"status", result ? "ok" : "error"}, {"recording", false},
                   {"path", path}, {"frames", frames}};
            if (!result) r["message"] = result.message;
            if (fps > 0.0f) r["length"] = frames / fps;   // measured output length (s)
            return r;
        }));

    // --- Node tree tools ---

    coreTool(server, "tc_get_node_tree", "Dump the node tree as JSON: per node {type, name, id, members (reflected, rotation in radians, derived rotationDeg in degrees, colors 0-1), derived, mods, children}. \"derived\" names members computed from another member (e.g. globalPos from pos, rotationDeg from rotation): editable, but not saved, and when a write carries both, the canonical one (pos or rotation) wins. Where depth cuts children off, childCount marks how many were omitted — drill in with another call passing that node's id")
        .level(ToolLevel::ReadOnly)
        .arg<int>("id", "Subtree root instance id (omit for the whole tree)", false)
        .arg<int>("depth", "Max child depth (omit for unlimited, 0 = the node only)", false)
        .bind([](const json& args) -> json {
            Node* root = getRootNode();
            if (!root) {
                return json{{"status", "error"}, {"message", "App is not running"}};
            }
            if (args.contains("id") && !args.at("id").is_null()) {
                uint64_t id = args.at("id").get<uint64_t>();
                root = root->findByInstanceId(id);
                if (!root) {
                    return json{{"status", "error"}, {"message", "No node with id " + std::to_string(id)}};
                }
            }
            int depth = -1;
            if (args.contains("depth") && args.at("depth").is_number()) {
                depth = args.at("depth").get<int>();
            }
            return json{{"status", "ok"}, {"tree", nodeToJson(*root, depth, true)}};
        });

    coreTool(server, "tc_get_selected_node", "Get the currently selected node (type, name, id, reflected members), or null if nothing is selected")
        .level(ToolLevel::ReadOnly)
        .bind(std::function<json()>([]() -> json {
            Node* n = getSelectedNode();
            if (!n) {
                return json{{"status", "ok"}, {"selected", nullptr}};
            }
            return json{{"status", "ok"}, {"selected", nodeToJson(*n, 0, true)}};
        }));

    coreTool(server, "tc_quit", "Quit the application gracefully")
        .level(ToolLevel::Control)
        .bind(std::function<json()>([]() -> json {
            trussc::internal::setExitReason("tc-quit");
            sapp_request_quit();
            return json{{"status", "ok"}};
        }));

    // --- Mouse Tools ---

    coreTool(server, "tc_mouse_move", "Move mouse cursor (with a button held, emits a drag)")
        .level(ToolLevel::Control)
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button held during the move (0:left, 1:right, 2:middle; omit or -1 for a plain move)", false)
        .bind([](const json& a) -> json {
            // json bind, not the typed one — the typed bind requires every
            // declared arg, which contradicted "button" being optional.
            float x = internal::inputFloat(a, "x");
            float y = internal::inputFloat(a, "y");
            int button = internal::inputMouseButton(a, true);
            // If button is pressed, treat as drag
            if (button >= 0) {
                trussc::internal::MouseEventRaw args;
                args.pos = args.globalPos = Vec2(x, y);
                args.button = button;
                MouseDragEventArgs dragArgs = trussc::internal::toDragArgs(args);
                events().mouseDragged.notify(dragArgs);
            } else {
                trussc::internal::MouseEventRaw args;
                args.pos = args.globalPos = Vec2(x, y);
                MouseMoveEventArgs moveArgs = trussc::internal::toMoveArgs(args);
                events().mouseMoved.notify(moveArgs);
            }

            // Update global mouse state
            ::trussc::internal::currentWindowContext().mouseX = x;
            ::trussc::internal::currentWindowContext().mouseY = y;

            return json{{"status", "ok"}};
        });

    // Split press/release. A drag gesture is press → tc_mouse_move(button) × N →
    // release; tc_mouse_click fires press+release back-to-back, so drag consumers
    // (e.g. EasyCam orbit) never see an open gesture. Anchoring the global
    // mouse position at press keeps the first drag delta sane.
    coreTool(server, "tc_mouse_press", "Press and hold a mouse button (start of a drag; pair with tc_mouse_move + tc_mouse_release)")
        .level(ToolLevel::Control)
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(internal::inputFloat(a, "x"), internal::inputFloat(a, "y"));
            args.button = internal::inputMouseButton(a);
            args.syncLegacy();

            ::trussc::internal::currentWindowContext().mouseX = args.pos.x;
            ::trussc::internal::currentWindowContext().mouseY = args.pos.y;

            events().mousePressed.notify(args);


            return json{{"status", "ok"}};
        });

    coreTool(server, "tc_mouse_release", "Release a mouse button (end of a drag)")
        .level(ToolLevel::Control)
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(internal::inputFloat(a, "x"), internal::inputFloat(a, "y"));
            args.button = internal::inputMouseButton(a);
            args.syncLegacy();

            ::trussc::internal::currentWindowContext().mouseX = args.pos.x;
            ::trussc::internal::currentWindowContext().mouseY = args.pos.y;

            events().mouseReleased.notify(args);


            return json{{"status", "ok"}};
        });

    coreTool(server, "tc_mouse_click", "Click mouse button (optionally with modifier keys held)")
        .level(ToolLevel::Control)
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .arg<bool>("shift", "Hold Shift", false)
        .arg<bool>("ctrl", "Hold Ctrl", false)
        .arg<bool>("alt", "Hold Alt", false)
        .arg<bool>("super", "Hold Cmd/Super", false)
        .bind([](const json& a) -> json {
            internal::validateInputModifiers(a);
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(internal::inputFloat(a, "x"), internal::inputFloat(a, "y"));
            args.button = internal::inputMouseButton(a);
            args.shift = a.value("shift", false);
            args.ctrl  = a.value("ctrl", false);
            args.alt   = a.value("alt", false);
            args.super = a.value("super", false);
            args.syncLegacy();

            // Press
            events().mousePressed.notify(args);


            // Release (fresh consumed flag — the press consumer may differ)
            MouseEventArgs rel = args;
            rel.consumed = false;
            events().mouseReleased.notify(rel);


            return json{{"status", "ok"}};
        });

    coreTool(server, "tc_mouse_scroll", "Scroll mouse wheel")
        .level(ToolLevel::Control)
        .arg<float>("dx", "Horizontal scroll delta")
        .arg<float>("dy", "Vertical scroll delta")
        .bind([](const json& a) -> json {
            const float dx = internal::inputFloat(a, "dx");
            const float dy = internal::inputFloat(a, "dy");
            ScrollEventArgs args;
            args.pos = args.globalPos = Vec2(::trussc::internal::currentWindowContext().mouseX, ::trussc::internal::currentWindowContext().mouseY);
            args.scroll = Vec2(dx, dy);
            args.syncLegacy();
            events().mouseScrolled.notify(args);

            return json{{"status", "ok"}};
        });

    // --- Key Tools ---

    // The modifier flags are shorthand for pressing/releasing the modifier
    // KEY ITSELF (the LEFT_* keycode), like a real keyboard does — so both
    // e.shift on the callback and isShiftPressed() in update() see it. Hold a
    // modifier across several keys by pressing its keycode explicitly
    // (tc_key_press 340), then plain presses inherit it.
    coreTool(server, "tc_key_press",
         "Press a key (updates the held-key set that isKeyPressed() reads). "
         "A modifier flag also presses that modifier's own key first (shift -> keycode 340, "
         "ctrl -> 341, alt -> 342, super -> 343) and reports it in modifiersPressed; "
         "pair it with the same flag on tc_key_release, or hold a modifier across several "
         "keys by pressing/releasing its keycode explicitly")
        .level(ToolLevel::Control)
        .arg<int>("key", "Defined nonzero sokol_app key code; letters are uppercase ASCII: 'A'=65..'Z'=90")
        .arg<bool>("shift", "Hold Shift for this press", false)
        .arg<bool>("ctrl", "Hold Ctrl for this press", false)
        .arg<bool>("alt", "Hold Alt for this press", false)
        .arg<bool>("super", "Hold Super/Command for this press", false)
        .bind([](const json& a) -> json {
            const int key = internal::inputKey(a);
            internal::validateInputModifiers(a);

            // Press the requested modifiers first (skip any already held —
            // whichever side, so an explicit RIGHT_SHIFT hold is respected).
            json pressedMods = json::array();
            auto holdMod = [&](const char* flag, bool alreadyHeld, int code) {
                if (a.value(flag, false) && !alreadyHeld) {
                    internal::injectKeyDown(code);
                    pressedMods.push_back(code);
                }
            };
            holdMod("shift", isShiftPressed(),   SAPP_KEYCODE_LEFT_SHIFT);
            holdMod("ctrl",  isControlPressed(), SAPP_KEYCODE_LEFT_CONTROL);
            holdMod("alt",   isAltPressed(),     SAPP_KEYCODE_LEFT_ALT);
            holdMod("super", isSuperPressed(),   SAPP_KEYCODE_LEFT_SUPER);

            internal::injectKeyDown(key);

            return json{{"status", "ok"},
                        {"modifiersPressed", pressedMods},
                        {"heldKeys", internal::heldKeysJson()}};
        });

    coreTool(server, "tc_key_release",
         "Release a key (updates the held-key set that isKeyPressed() reads). "
         "A modifier flag also releases that modifier's own key afterwards "
         "(shift -> keycode 340, ctrl -> 341, alt -> 342, super -> 343), "
         "mirroring tc_key_press")
        .level(ToolLevel::Control)
        .arg<int>("key", "Defined nonzero sokol_app key code; letters are uppercase ASCII: 'A'=65..'Z'=90")
        .arg<bool>("shift", "Also release Shift after this key", false)
        .arg<bool>("ctrl", "Also release Ctrl after this key", false)
        .arg<bool>("alt", "Also release Alt after this key", false)
        .arg<bool>("super", "Also release Super/Command after this key", false)
        .bind([](const json& a) -> json {
            const int key = internal::inputKey(a);
            internal::validateInputModifiers(a);

            // The key goes up first — while the modifiers are still held, so
            // this event still carries them (as it does on a real keyboard).
            internal::injectKeyUp(key);

            json releasedMods = json::array();
            auto dropMod = [&](const char* flag, int code) {
                if (a.value(flag, false) && isKeyPressed(code)) {
                    internal::injectKeyUp(code);
                    releasedMods.push_back(code);
                }
            };
            dropMod("shift", SAPP_KEYCODE_LEFT_SHIFT);
            dropMod("ctrl",  SAPP_KEYCODE_LEFT_CONTROL);
            dropMod("alt",   SAPP_KEYCODE_LEFT_ALT);
            dropMod("super", SAPP_KEYCODE_LEFT_SUPER);

            return json{{"status", "ok"},
                        {"modifiersReleased", releasedMods},
                        {"heldKeys", internal::heldKeysJson()}};
        });

    // --- Node Tools ---

    coreTool(server, "tc_select_node", "Select a node by instance id (0 clears the selection); drives the same selection an inspector shows")
        .level(ToolLevel::Control)
        .arg<int>("id", "Instance id from tc_get_node_tree (0 to clear)")
        .bind<int>([](int id) {
            if (id == 0) {
                setSelectedNode(nullptr);
                return json{{"status", "ok"}, {"selected", nullptr}};
            }
            Node* root = getRootNode();
            Node* n = root ? root->findByInstanceId(static_cast<uint64_t>(id)) : nullptr;
            if (!n) {
                return json{{"status", "error"}, {"message", "No node with id " + std::to_string(id)}};
            }
            setSelectedNode(n);
            return json{{"status", "ok"}, {"selected", nodeToJson(*n, 0, true)}};
        });

    coreTool(server, "tc_set_node_members", "Set reflected members of a node — or one of its mods — from a JSON object (same encoding as tc_get_node_tree: Vec3 [x,y,z], Color [r,g,b,a], rotation in radians, derived rotationDeg in degrees, enums by label string). A derived member (e.g. globalPos or rotationDeg) is applied before its canonical member (pos or rotation), so when both are given the canonical one wins")
        .level(ToolLevel::Control)
        .arg<int>("id", "Instance id from tc_get_node_tree")
        .arg<json>("members", "Member values to apply, e.g. {\"pos\":[10,20,0],\"visible\":true}")
        .arg<std::string>("mod", "Mod short type name (e.g. \"LayoutMod\") to target a mod attached to the node instead of the node itself", false)
        .bind([](const json& args) -> json {
            Node* root = getRootNode();
            uint64_t id = args.at("id").get<uint64_t>();
            Node* n = root ? root->findByInstanceId(id) : nullptr;
            if (!n) {
                return json{{"status", "error"}, {"message", "No node with id " + std::to_string(id)}};
            }

            JsonReadReflector r(args.at("members"));
            json after;
            if (args.contains("mod") && args.at("mod").is_string()) {
                std::string modName = args.at("mod").get<std::string>();
                Mod* mod = n->getModByTypeName(modName);
                if (!mod) {
                    return json{{"status", "error"},
                                {"message", "No mod \"" + modName + "\" on node " + std::to_string(id)}};
                }
                mod->reflectMembers(r);
                after = reflectToJson(*mod, true);
            } else {
                n->reflectMembers(r);
                after = reflectToJson(*n, true);
            }

            json result{
                {"status", "ok"},
                {"applied", r.applied},
                {"members", std::move(after)}
            };
            if (!r.skipped.empty()) result["skipped"] = r.skipped;
            if (!r.readOnly.empty()) result["readOnly"] = r.readOnly;
            auto unknown = r.unknownKeys();
            if (!unknown.empty()) result["unknown"] = unknown;
            return result;
        });

}

} // namespace internal

} // namespace mcp
} // namespace trussc
