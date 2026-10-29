// #340 representation regression: real ImGui, isolated sokol dummy backend.
// Keep these backend symbols local when linking the normal addon library.
#define SOKOL_API_DECL static
#define SOKOL_API_IMPL static
#define SOKOL_IMPL
#undef SOKOL_GLCORE
#define SOKOL_DUMMY_BACKEND
#define SOKOL_IMGUI_NO_SOKOL_APP
#include "sokol/sokol_gfx.h"
#include "imgui/imgui.h"
#include "sokol_imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
int failures = 0, checks = 0, callbacks = 0, errors = 0, panics = 0;
std::vector<size_t> requests;
void check(const char* name, bool ok) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
void marker(const ImDrawList*, const ImDrawCmd*) { ++callbacks; }
simgui_desc_t description(int capacity = 65536) {
    simgui_desc_t desc = {};
    desc.max_vertices = capacity;
    desc.allocator.alloc_fn = [](size_t bytes, void*) -> void* {
        requests.push_back(bytes);
        // Large blocks are only tested through setup/growth, never copied into.
        return std::malloc(bytes > 8 * 1024 * 1024 ? 1 : bytes);
    };
    desc.allocator.free_fn = [](void* ptr, void*) { std::free(ptr); };
    desc.logger.func = [](const char*, uint32_t level, uint32_t item, const char* message,
                          uint32_t, const char*, void*) {
        if (level == 0) ++panics;
        if (level == 1 && item == SIMGUI_LOGITEM_BUFFER_OVERFLOW && message) ++errors;
    };
    return desc;
}
}

int main() {
    sg_desc gfx = {};
    sg_setup(&gfx);
    const size_t limit = std::min(size_t(INT_MAX) / sizeof(ImDrawVert),
                                 size_t(INT_MAX) / (3 * sizeof(ImDrawIdx)));
    auto desc = description();
    simgui_setup(&desc);
    ImGui::GetIO().IniFilename = nullptr;
    for (bool indexOnly : {false, true}) {
        simgui_frame_desc_t frame = {};
        frame.width = frame.height = 128; frame.delta_time = 1.0 / 60.0; frame.dpi_scale = 1;
        simgui_new_frame(&frame);
        for (int i = 0; i < 2; ++i) {
            ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({128, 128});
            ImGui::Begin(i ? "large" : "small");
            ImGui::GetWindowDrawList()->AddCallback(marker, nullptr);
            ImGui::End();
        }
        ImGui::Render();
        auto* list = ImGui::GetDrawData()->CmdLists.back();
        const int oldVertices = list->VtxBuffer.Size, oldIndices = list->IdxBuffer.Size;
        if (indexOnly) list->IdxBuffer.Size = int(3 * limit + 1);
        else list->VtxBuffer.Size = int(limit + 1);
        const auto oldVbuf = _simgui.vbuf, oldIbuf = _simgui.ibuf;
        const auto oldVerticesPtr = _simgui.vertices.ptr, oldIndicesPtr = _simgui.indices.ptr;
        const int oldErrors = errors;
        requests.clear(); callbacks = 0;
        sg_pass pass = {};
        pass.swapchain.width = pass.swapchain.height = 128;
        pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
        pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        pass.swapchain.sample_count = 1;
        sg_begin_pass(&pass);
        simgui_render();
        sg_end_pass(); sg_commit();
        list->VtxBuffer.Size = oldVertices; list->IdxBuffer.Size = oldIndices;
        check(indexOnly ? "unrepresentable 1:3 capacity gives one Error before allocation" :
                          "unrepresentable vertex bytes give one Error before allocation",
              errors == oldErrors + 1 && requests.empty());
        check("failed growth preserves CPU/GPU buffers and renders the small list",
              _simgui.vbuf.id == oldVbuf.id && _simgui.ibuf.id == oldIbuf.id &&
              _simgui.vertices.ptr == oldVerticesPtr && _simgui.indices.ptr == oldIndicesPtr && callbacks == 1);
    }
    requests.clear();
    check("representable 70M-vertex request succeeds despite overflowing doubled size",
          _simgui_grow_buffers(70000000 * sizeof(ImDrawVert), 0));
    check("doubling is capped at the largest representable 1:3 capacity",
          requests.size() == 2 && requests[0] == limit * sizeof(ImDrawVert) &&
          requests[1] == limit * 3 * sizeof(ImDrawIdx) &&
          sg_query_buffer_size(_simgui.vbuf) == requests[0] &&
          sg_query_buffer_size(_simgui.ibuf) == requests[1]);
    simgui_shutdown();

    requests.clear();
    desc = description(int(limit));
    simgui_setup(&desc);
    check("largest representable initial size is accepted", requests.size() == 2 && panics == 0);
    simgui_shutdown();
    requests.clear();
    desc = description(int(limit + 1));
    simgui_setup(&desc);
    check("unrepresentable initial size is rejected before CPU/GPU allocation",
          requests.empty() && panics == 1 && !_simgui.imgui_ctx && !_simgui.vbuf.id && !_simgui.ibuf.id);
    sg_shutdown();
    std::printf("buffer-size regression: %d checks, %d failures; large staging requests allocate only 1 byte\n", checks, failures);
    return failures ? 1 : 0;
}
