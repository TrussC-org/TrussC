// Real-GL regression for #340, shared by the surfaceless addon probe.
#include <climits>
namespace {
int bufferCallbacks = 0;
void bufferMarker(const ImDrawList*, const ImDrawCmd*) { ++bufferCallbacks; }
void bufferUi(int primitives, float thickness = 1.0f) {
    for (int window = 0; window < 2; ++window) {
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({128,128});
        ImGui::Begin(window == 0 ? "buffer back" : "buffer heavy", nullptr,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration);
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled({1,1}, {8,8}, IM_COL32_WHITE);
        if (window == 1) {
            std::vector<ImVec2> points(15000);
            for (size_t i = 0; i < points.size(); ++i) points[i] = {float(i % 100), float(i % 97)};
            for (int i = 0; i < primitives; ++i)
                list->AddPolyline(points.data(), int(points.size()), IM_COL32_WHITE, 0, thickness);
        }
        if (window == 1) {
            if (primitives >= 3) check("fixture exceeds both default capacities", list->VtxBuffer.Size > 65536 && list->IdxBuffer.Size > 3 * 65536);
            else check("index-only fixture fits vertices and exceeds indices", list->VtxBuffer.Size < 65536 && list->IdxBuffer.Size > 3 * 65536);
        }
        list->AddCallback(bufferMarker, nullptr);
        ImGui::End();
    }
    auto* front = ImGui::GetForegroundDrawList();
    front->AddRectFilled({110,110}, {120,120}, IM_COL32(255,0,255,255));
    front->AddCallback(bufferMarker, nullptr);
}
void bufferGrowRegression() {
    int notices = 0, errors = 0;
    std::string lastNotice;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.message.find("grew the buffers") != std::string::npos) {
            check("growth uses Notice", event.level == LogLevel::Notice);
            ++notices; lastNotice = event.message;
        }
        if (event.level >= LogLevel::Error) ++errors;
    });
    Fbo target; target.allocate(128,128);
    auto render = [&](ImGuiManager& manager, int primitives, float thickness = 1.0f) {
        bufferCallbacks = 0;
        target.begin(0,0,0,0);
        { auto scope = manager.scoped(); check("growth frame accepted", bool(scope)); bufferUi(primitives, thickness); }
        target.end(); internal::endGpuFrame();
        check("all lists, including the foreground, were rendered", bufferCallbacks == 3);
        Pixels pixels; pixels.allocate(128,128,4);
        check("growth frame readback", target.readPixels(pixels.getData()));
        bool foregroundVisible = false;
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
            const auto color = pixels.getColor(x,y);
            if (color.r == 1 && color.g == 0 && color.b == 1 && color.a == 1) foregroundVisible = true;
        }
        check("last draw list's marker is visible in GPU readback", foregroundVisible);
    };
    ImGuiManager first, second;
    first.setupOffscreen(); second.setupOffscreen(0);
    render(first, 3);
    check("first >65536-vertex frame grows once with actionable counts", notices == 1 &&
        lastNotice.find("had 65536") != std::string::npos &&
        lastNotice.find("Call imguiSetup(<vertices>) to start larger.") != std::string::npos);
    render(first, 3); render(first, 9);
    check("repeated and larger growth emit no second Notice", notices == 1);
    render(second, 3);
    check("another context keeps its own default capacity and Notice", notices == 2 && lastNotice.find("had 65536") != std::string::npos);
    ImGuiManager configured; configured.setupOffscreen(200000);
    render(configured, 3);
    check("explicit initial capacity avoids the first allocation", notices == 2);
    render(configured, 9);
    check("explicit initial capacity is honoured", notices == 3 && lastNotice.find("had 200000") != std::string::npos);
    ImGuiManager negative; negative.setupOffscreen(-1);
    render(negative, 3);
    check("negative initial capacity uses the default", notices == 4 && lastNotice.find("had 65536") != std::string::npos);
    imguiSetup(200000);
    render(ImGuiManager::instance(), 3);
    check("imguiSetup(int) configures the window manager", notices == 4);
    render(ImGuiManager::instance(), 9);
    check("window setup honours the requested capacity", notices == 5 && lastNotice.find("had 200000") != std::string::npos);
    imguiShutdown();
    imguiSetup(-1);
    render(ImGuiManager::instance(), 3);
    check("window setup below one uses the default", notices == 6 && lastNotice.find("had 65536") != std::string::npos);
    imguiShutdown();
    ImGuiManager indexHeavy; indexHeavy.setupOffscreen();
    render(indexHeavy, 1, 1.5f);
    check("index demand alone grows buffers", notices == 7 && lastNotice.find("had 65536") != std::string::npos);
    check("large frames emit no errors", errors == 0);

    // Inject staging allocation failures after setup, leaving the small back
    // list drawable. Check recovery on the same context on the next frame.
    struct AllocationState { int failAt = 0, calls = 0, errors = 0; std::vector<size_t> requests; } allocation;
    simgui_desc_t desc = {};
    desc.allocator.user_data = &allocation;
    desc.allocator.alloc_fn = [](size_t bytes, void* data) -> void* {
        auto& state = *static_cast<AllocationState*>(data);
        ++state.calls; state.requests.push_back(bytes);
        return state.failAt > 0 && state.calls == state.failAt ? nullptr : std::malloc(bytes);
    };
    desc.allocator.free_fn = [](void* ptr, void*) { std::free(ptr); };
    desc.logger.user_data = &allocation;
    desc.logger.func = [](const char*, uint32_t level, uint32_t item, const char* message, uint32_t, const char*, void* data) {
        if (level == 1 && item == SIMGUI_LOGITEM_BUFFER_OVERFLOW && message &&
            std::string(message).find("part of the UI was not drawn this frame.") != std::string::npos)
            ++static_cast<AllocationState*>(data)->errors;
    };
    const auto previous = simgui_tc_get_context();
    const auto backend = simgui_tc_make_context(&desc);
    simgui_tc_set_context(backend);
    ImGui::GetIO().IniFilename = nullptr;
    for (int failAt : {1, 2, -1, 0}) {
        // Leave only one GPU slot: vbuf succeeds but ibuf cannot be created.
        std::vector<sg_buffer> reserved;
        if (failAt == -1) {
            while (true) {
                auto buffer = sg_alloc_buffer();
                if (!buffer.id) break;
                reserved.push_back(buffer);
            }
            check("GPU pool exhaustion fixture reserved slots", !reserved.empty());
            sg_destroy_buffer(reserved.back()); reserved.pop_back();
        }
        allocation.failAt = failAt; allocation.calls = 0; allocation.requests.clear(); bufferCallbacks = 0;
        target.begin(0,0,0,0);
        simgui_frame_desc_t frame = {}; frame.width = frame.height = 128; frame.delta_time = 1.0/60.0; frame.dpi_scale = 1;
        check("allocation failure frame accepted", d::beginTargetFrame(backend, frame));
        bufferUi(3); d::renderTargetFrame(backend);
        target.end(); internal::endGpuFrame();
        check("failure preserves old buffers; next successful frame recovers", bufferCallbacks == (failAt ? 1 : 3));
        if (failAt == 0) check("staging doubles to fit and preserves the 1:3 ratio",
            allocation.requests.size() == 2 && allocation.requests[0] == 131072 * sizeof(ImDrawVert) &&
            allocation.requests[1] == 131072 * 3 * sizeof(ImDrawIdx));
        for (auto buffer : reserved) sg_destroy_buffer(buffer);
    }
    check("each failed frame emits exactly one useful Error", allocation.errors == 3);
    allocation.calls = 0; bufferCallbacks = 0;
    target.begin(0,0,0,0);
    simgui_frame_desc_t oversized = {}; oversized.width = oversized.height = 128; oversized.delta_time = 1.0/60.0; oversized.dpi_scale = 1;
    check("unrepresentable-size frame accepted", d::beginTargetFrame(backend, oversized));
    bufferUi(3); ImGui::Render();
    auto* firstList = ImGui::GetDrawData()->CmdLists[0];
    const int savedSize = firstList->VtxBuffer.Size;
    firstList->VtxBuffer.Size = INT_MAX; // metadata only: no huge allocation
    d::renderTargetFrame(backend);
    firstList->VtxBuffer.Size = savedSize;
    target.end(); internal::endGpuFrame();
    check("unrepresentable byte offsets report one Error without copying", allocation.errors == 4 && allocation.calls == 0 && bufferCallbacks == 0);
    d::destroyTargetContext(backend); simgui_tc_set_context(previous);
}
}
