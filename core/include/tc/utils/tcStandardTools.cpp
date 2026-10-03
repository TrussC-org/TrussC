// =============================================================================
// tcStandardTools.cpp - the standard MCP tool sets
// =============================================================================
// registerInspectionTools() / registerControlTools() are defined here, not
// inline in tcStandardTools.h, so every handler they register is code in
// TrussC.lib, i.e. in the host executable under hot reload. Apps call
// registerControlTools() from setup() -- guest code -- and on Windows a guest
// DLL compiles its own copy of every header-inline function and variable: an
// inline handler would read the guest's copy of the input dispatch pointers
// (internal::appMousePressedFunc, ...), which only the host sets, and the
// injected input would never reach the App (#249). Out of line, the handlers
// read the core loop's own state on every platform.
// =============================================================================

#include <TrussC.h>

namespace trussc {
namespace mcp {

void registerInspectionTools() {

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

    tool("tc_list_windows", "List open windows (index 0 = main; use the index as the 'window' arg of tc_get_screenshot / tc_save_screenshot). Each secondary window has 'occluded': true while the OS reports it hidden (minimized, covered, on another Space, or the screen locked or off); it then renders no frames and cannot be captured.")
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

    tool("tc_get_screenshot", "Screenshot as Base64 PNG/JPEG. Defaults to full-resolution PNG; pass width for a downscaled monitoring thumbnail (aspect preserved, never upscales) and format 'jpg' for small payloads. Cheap to poll at any settings: only the framebuffer readback touches the frame loop — downscale + encode run on the HTTP worker thread, so polling does not stutter the app. A secondary window is captured only while visible: one tc_list_windows reports occluded fails at once, and one that renders no frame within 5 s fails then.")
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
                bool grabbed = trussc::grabScreen(*px);
                return [px, grabbed, format, reqWidth, quality]() -> json {
                    if (!grabbed) {
                        return json{{"status", "error"}, {"message", "Failed to grab screen"}};
                    }
                    return detail::imageContentResult(detail::pixelsToImageJson(*px, format, reqWidth, quality));
                };
            }, ctx->isMain ? nullptr : ctx);
            return json(nullptr);  // ignored — deferred result is sent instead
        });

    tool("tc_save_screenshot", "Save screenshot to file. A secondary window is captured only while visible: one tc_list_windows reports occluded fails at once, and one that renders no frame within 5 s fails then.")
        .arg<std::string>("path", "File path")
        .arg<int>("window", "Window index from tc_list_windows (default 0 = main; a secondary window must be visible)", false)
        .bind([resolveWindowCtx](const json& args) -> json {
            // JSON strings are UTF-8 — convert explicitly (fs::path(string)
            // would interpret them in the ACP on Windows)
            const std::string path = args.at("path").get<std::string>();
            const int windowIdx = args.value("window", 0);
            if (windowIdx == 0) {
                // Main window: the classic queued path (drained after present)
                const auto destination = trussc::internal::resolveScreenshotPath(
                    trussc::internal::utf8ToPath(path));
                if (trussc::saveScreenshot(destination)) {
                    return json{{"status", "ok"}, {"path", trussc::internal::pathToUtf8(destination)}};
                }
                return json{{"status", "error"}, {"message", "Failed to save screenshot"}};
            }
            // Secondary window: capture inside that window's own tick, right
            // after its present(), where its drawable is current (#243)
            json err;
            auto* ctx = resolveWindowCtx(windowIdx, err);
            if (!ctx) return err;
            mcp::deferToolResultUntilAfterFrame([windowIdx, path]() -> json {
                const auto destination = trussc::internal::resolveScreenshotPath(
                    trussc::internal::utf8ToPath(path));
                bool ok = trussc::internal::captureWindowToFile(destination);
                if (ok) return json{{"status", "ok"}, {"path", trussc::internal::pathToUtf8(destination)}, {"window", windowIdx}};
                return json{{"status", "error"}, {"message", "Failed to capture window " + std::to_string(windowIdx)}};
            }, ctx);
            return json(nullptr);  // deferred result is sent instead
        });

    tool("tc_get_status", "App-published ops status: values registered via mcp::status()/mcp::statusGraph() plus the names of mcp::statusImage() images. mode 'graph' means the value wants to be plotted over time. Empty when the app publishes nothing. Supervisors discover this tool via tools/list and forward the payload to their monitoring server.")
        .bind(std::function<json()>([]() -> json {
            json values = json::array();
            for (auto& e : detail::statusRegistry()) {
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
            for (auto& e : detail::statusImageRegistry()) images.push_back(e.name);
            return json{{"values", values}, {"images", images}};
        }));

    tool("tc_get_alerts", "Drain operator alerts raised by the app via mcp::alert(). Returns and CLEARS the pending list, so exactly one consumer receives each alert. Empty 'alerts' array when nothing is pending. Supervisors (anchorbolt start) poll this on the health cadence and forward entries to their notification sinks.")
        .bind(std::function<json()>([]() -> json {
            std::deque<json> drained;
            {
                std::lock_guard<std::mutex> lock(detail::alertMutex());
                drained.swap(detail::alertQueue());
            }
            json arr = json::array();
            for (auto& a : drained) arr.push_back(std::move(a));
            return json{{"alerts", arr}};
        }));

    tool("tc_get_status_image", "Fetch an app-published image registered via mcp::statusImage(), downscaled + JPEG-encoded like tc_get_screenshot (pixel grab on the main loop, encode on the HTTP worker — no frame stutter).")
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
            for (auto& e : detail::statusImageRegistry()) {
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
                    return detail::imageContentResult(detail::pixelsToImageJson(*px, "jpg", reqWidth, quality));
                };
            });
            // The getter is the registering app's code (a hot reload guest's):
            // unloading it answers this reply with an error instead of running it.
            mcp::detail::setDeferralOwner(getterOwner);
            return json(nullptr);  // ignored — deferred result is sent instead
        });

    tool("tc_get_health", "Lightweight liveness snapshot: fps (measured average), frame count, uptime seconds, window size, TrussC version, pid, process RSS bytes, main-thread queue backlog, sokol-tracked bytes. Cheap enough to poll — reads counters only, touches no GPU state. pid lets a supervisor confirm it is talking to ITS child (port collisions); rssBytes is the number to graph for leak hunting; mainQueuePending is how many runOnMainThread / Deliver::Main calls this frame's drain started with (a number that keeps growing means workers queue faster than the app runs them).")
        .bind(std::function<json()>([]() -> json {
            return json{{"status", "ok"},
                        {"fps", trussc::getFps()},
                        {"frameCount", trussc::getFrameCount()},
                        // Process uptime: the framework clock, which
                        // resetElapsedTimeCounter() does not touch (#229).
                        {"uptimeSec", trussc::internal::getUptimeSeconds()},
                        {"width", trussc::getWindowWidth()},
                        {"height", trussc::getWindowHeight()},
                        {"version", trussc::getVersion()},
                        {"pid", detail::currentPid()},
                        {"rssBytes", detail::processRssBytes()},
                        {"mainQueuePending", trussc::internal::getMainThreadQueuePending()},
                        {"memoryBytes", trussc::getSokolMemoryBytes()}};
        }));

    tool("tc_get_audio_state", "Audio engine diagnostics (read-only; never starts the engine): running; playingSounds {slot, path, streaming, position/duration s, volume, pan, speed, loop, paused, level = peak of the playback's output}, master peak / RMS (linear, before clipping) and clipped-sample count, plays dropped since startup by reason (polyphonyLimit = every playback slot busy, streamLimit = a stream's maxPolyphony, decoderError, notRunning = no device), audio-thread cpuUsage / cpuUsagePeak (audio-thread time / audio time; 1.0 = a callback took as long as the audio it produced), and the output / input devices. Pass devices=false to skip the device enumeration (slow on some backends) when polling.")
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
                playingSounds.push_back({{"slot", v.slot}, {"path", trussc::internal::pathToUtf8(v.path)},
                                  {"streaming", v.streaming},
                                  {"position", v.position}, {"duration", v.duration},
                                  {"volume", v.volume}, {"pan", v.pan}, {"speed", v.speed},
                                  {"loop", v.loop}, {"paused", v.paused},
                                  {"level", v.level}});
            }

            auto& mic = trussc::getMicInput();
            json r{{"status", "ok"},
                   {"running", engine.isInitialized()},
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

    tool("tc_start_recording", "Start recording the window to a video file (the screenshot's video counterpart). Omit path for a timestamped file in the data dir; give duration for a fixed-length clip that auto-stops and finalizes itself.")
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
            bool ok = trussc::startRecording(trussc::internal::utf8ToPath(path), settings);
            json r{{"status", ok ? "ok" : "error"},
                   {"path", trussc::internal::pathToUtf8(trussc::recordingPath())},
                   {"fps", settings.fps},
                   {"codec", trussc::videoCodecName(settings.codec)}};
            if (settings.duration > 0.0f) r["duration"] = settings.duration;
            return r;
        });

    tool("tc_stop_recording", "Stop the current recording and finalize the file. Wins over a pending fixed duration (finalizes immediately at the current length); a no-op if nothing is recording.")
        .bind(std::function<json()>([]() -> json {
            if (!trussc::isRecording()) {
                // Harmless no-op (e.g. a fixed-duration recording already
                // auto-stopped, or nothing was started). Not an error.
                return json{{"status", "ok"}, {"recording", false},
                            {"message", "not recording"}};
            }
            std::string path = trussc::internal::pathToUtf8(trussc::recordingPath());
            int frames = trussc::recordingFrameCount();
            float fps  = trussc::internal::globalScreenRecorder().writer().getFps();
            trussc::stopRecording();
            json r{{"status", "ok"}, {"recording", false},
                   {"path", path}, {"frames", frames}};
            if (fps > 0.0f) r["length"] = frames / fps;   // measured output length (s)
            return r;
        }));

    // --- Node tree tools ---

    tool("tc_get_node_tree", "Dump the node tree as JSON: per node {type, name, id, members (reflected, rotation in degrees, colors 0-1), derived, mods, children}. \"derived\" names members computed from another member (e.g. globalPos from pos): editable, but not saved, and when a write carries both, the canonical one (pos) wins. Where depth cuts children off, childCount marks how many were omitted — drill in with another call passing that node's id")
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

    tool("tc_get_selected_node", "Get the currently selected node (type, name, id, reflected members), or null if nothing is selected")
        .bind(std::function<json()>([]() -> json {
            Node* n = getSelectedNode();
            if (!n) {
                return json{{"status", "ok"}, {"selected", nullptr}};
            }
            return json{{"status", "ok"}, {"selected", nodeToJson(*n, 0, true)}};
        }));
}

void registerControlTools() {

    // Registering the control surface IS the opt-in to input injection /
    // scene mutation. Mark it so isDebuggerEnabled() reflects reality.
    detail::isDebuggerEnabled().store(true);

    tool("tc_quit", "Quit the application gracefully")
        .bind(std::function<json()>([]() -> json {
            sapp_request_quit();
            return json{{"status", "ok"}};
        }));

    // --- Mouse Tools ---

    tool("tc_mouse_move", "Move mouse cursor (with a button held, emits a drag)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button held during the move (0:left, 1:right, 2:middle; omit or -1 for a plain move)", false)
        .bind([](const json& a) -> json {
            // json bind, not the typed one — the typed bind requires every
            // declared arg, which contradicted "button" being optional.
            float x = a.at("x").get<float>();
            float y = a.at("y").get<float>();
            int button = a.value("button", -1);
            // If button is pressed, treat as drag
            if (button >= 0) {
                internal::MouseEventRaw args;
                args.pos = args.globalPos = Vec2(x, y);
                args.button = button;
                MouseDragEventArgs dragArgs = internal::toDragArgs(args);
                events().mouseDragged.notify(dragArgs);

                if (::trussc::internal::appMouseDraggedFunc)
                    ::trussc::internal::appMouseDraggedFunc(args);
            } else {
                internal::MouseEventRaw args;
                args.pos = args.globalPos = Vec2(x, y);
                MouseMoveEventArgs moveArgs = internal::toMoveArgs(args);
                events().mouseMoved.notify(moveArgs);

                if (::trussc::internal::appMouseMovedFunc)
                    ::trussc::internal::appMouseMovedFunc(args);
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
    tool("tc_mouse_press", "Press and hold a mouse button (start of a drag; pair with tc_mouse_move + tc_mouse_release)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(a.at("x").get<float>(), a.at("y").get<float>());
            args.button = a.value("button", 0);
            args.syncLegacy();

            ::trussc::internal::currentWindowContext().mouseX = args.pos.x;
            ::trussc::internal::currentWindowContext().mouseY = args.pos.y;

            events().mousePressed.notify(args);
            if (::trussc::internal::appMousePressedFunc)
                ::trussc::internal::appMousePressedFunc(args);

            return json{{"status", "ok"}};
        });

    tool("tc_mouse_release", "Release a mouse button (end of a drag)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(a.at("x").get<float>(), a.at("y").get<float>());
            args.button = a.value("button", 0);
            args.syncLegacy();

            ::trussc::internal::currentWindowContext().mouseX = args.pos.x;
            ::trussc::internal::currentWindowContext().mouseY = args.pos.y;

            events().mouseReleased.notify(args);
            if (::trussc::internal::appMouseReleasedFunc)
                ::trussc::internal::appMouseReleasedFunc(args);

            return json{{"status", "ok"}};
        });

    tool("tc_mouse_click", "Click mouse button (optionally with modifier keys held)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .arg<bool>("shift", "Hold Shift", false)
        .arg<bool>("ctrl", "Hold Ctrl", false)
        .arg<bool>("alt", "Hold Alt", false)
        .arg<bool>("super", "Hold Cmd/Super", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(a.at("x").get<float>(), a.at("y").get<float>());
            args.button = a.value("button", 0);
            args.shift = a.value("shift", false);
            args.ctrl  = a.value("ctrl", false);
            args.alt   = a.value("alt", false);
            args.super = a.value("super", false);
            args.syncLegacy();

            // Press
            events().mousePressed.notify(args);
            if (::trussc::internal::appMousePressedFunc)
                ::trussc::internal::appMousePressedFunc(args);

            // Release (fresh consumed flag — the press consumer may differ)
            MouseEventArgs rel = args;
            rel.consumed = false;
            events().mouseReleased.notify(rel);
            if (::trussc::internal::appMouseReleasedFunc)
                ::trussc::internal::appMouseReleasedFunc(rel);

            return json{{"status", "ok"}};
        });

    tool("tc_mouse_scroll", "Scroll mouse wheel")
        .arg<float>("dx", "Horizontal scroll delta")
        .arg<float>("dy", "Vertical scroll delta")
        .bind<float, float>([](float dx, float dy) {
            ScrollEventArgs args;
            args.pos = args.globalPos = Vec2(::trussc::internal::currentWindowContext().mouseX, ::trussc::internal::currentWindowContext().mouseY);
            args.scroll = Vec2(dx, dy);
            args.syncLegacy();
            events().mouseScrolled.notify(args);
            if (::trussc::internal::appMouseScrolledFunc)
                ::trussc::internal::appMouseScrolledFunc(args);
            return json{{"status", "ok"}};
        });

    // --- Key Tools ---

    // The modifier flags are shorthand for pressing/releasing the modifier
    // KEY ITSELF (the LEFT_* keycode), like a real keyboard does — so both
    // e.shift on the callback and isShiftPressed() in update() see it. Hold a
    // modifier across several keys by pressing its keycode explicitly
    // (tc_key_press 340), then plain presses inherit it.
    tool("tc_key_press",
         "Press a key (updates the held-key set that isKeyPressed() reads). "
         "A modifier flag also presses that modifier's own key first (shift -> keycode 340, "
         "ctrl -> 341, alt -> 342, super -> 343) and reports it in modifiersPressed; "
         "pair it with the same flag on tc_key_release, or hold a modifier across several "
         "keys by pressing/releasing its keycode explicitly")
        .arg<int>("key", "Key code (sokol_app keycode; letters are uppercase ASCII: 'A'=65..'Z'=90)")
        .arg<bool>("shift", "Hold Shift for this press", false)
        .arg<bool>("ctrl", "Hold Ctrl for this press", false)
        .arg<bool>("alt", "Hold Alt for this press", false)
        .arg<bool>("super", "Hold Super/Command for this press", false)
        .bind([](const json& a) -> json {
            const int key = a.at("key").get<int>();

            // Press the requested modifiers first (skip any already held —
            // whichever side, so an explicit RIGHT_SHIFT hold is respected).
            json pressedMods = json::array();
            auto holdMod = [&](const char* flag, bool alreadyHeld, int code) {
                if (a.value(flag, false) && !alreadyHeld) {
                    detail::injectKeyDown(code);
                    pressedMods.push_back(code);
                }
            };
            holdMod("shift", isShiftPressed(),   SAPP_KEYCODE_LEFT_SHIFT);
            holdMod("ctrl",  isControlPressed(), SAPP_KEYCODE_LEFT_CONTROL);
            holdMod("alt",   isAltPressed(),     SAPP_KEYCODE_LEFT_ALT);
            holdMod("super", isSuperPressed(),   SAPP_KEYCODE_LEFT_SUPER);

            detail::injectKeyDown(key);

            return json{{"status", "ok"},
                        {"modifiersPressed", pressedMods},
                        {"heldKeys", detail::heldKeysJson()}};
        });

    tool("tc_key_release",
         "Release a key (updates the held-key set that isKeyPressed() reads). "
         "A modifier flag also releases that modifier's own key afterwards "
         "(shift -> keycode 340, ctrl -> 341, alt -> 342, super -> 343), "
         "mirroring tc_key_press")
        .arg<int>("key", "Key code (sokol_app keycode; letters are uppercase ASCII: 'A'=65..'Z'=90)")
        .arg<bool>("shift", "Also release Shift after this key", false)
        .arg<bool>("ctrl", "Also release Ctrl after this key", false)
        .arg<bool>("alt", "Also release Alt after this key", false)
        .arg<bool>("super", "Also release Super/Command after this key", false)
        .bind([](const json& a) -> json {
            const int key = a.at("key").get<int>();

            // The key goes up first — while the modifiers are still held, so
            // this event still carries them (as it does on a real keyboard).
            detail::injectKeyUp(key);

            json releasedMods = json::array();
            auto dropMod = [&](const char* flag, int code) {
                if (a.value(flag, false) && isKeyPressed(code)) {
                    detail::injectKeyUp(code);
                    releasedMods.push_back(code);
                }
            };
            dropMod("shift", SAPP_KEYCODE_LEFT_SHIFT);
            dropMod("ctrl",  SAPP_KEYCODE_LEFT_CONTROL);
            dropMod("alt",   SAPP_KEYCODE_LEFT_ALT);
            dropMod("super", SAPP_KEYCODE_LEFT_SUPER);

            return json{{"status", "ok"},
                        {"modifiersReleased", releasedMods},
                        {"heldKeys", detail::heldKeysJson()}};
        });

    // --- Node Tools ---

    tool("tc_select_node", "Select a node by instance id (0 clears the selection); drives the same selection an inspector shows")
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

    tool("tc_set_node_members", "Set reflected members of a node — or one of its mods — from a JSON object (same encoding as tc_get_node_tree: Vec3 [x,y,z], Color [r,g,b,a], rotation in degrees, enums by label string). A derived member (e.g. globalPos) is applied before its canonical member (pos), so when both are given the canonical one wins")
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

} // namespace mcp
} // namespace trussc
