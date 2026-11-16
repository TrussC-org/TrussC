#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "stencilref.glsl.h"
#include <cstdio>
#include <cstring>

using namespace tc;
namespace {
int failures = 0;
bool completed = false;
void check(const char* label, bool passed) {
    std::printf("stencilRef %s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}
class StencilRefApp : public App {
    void draw() override {
        Fbo target;
        target.allocate(24, 40, 1);
        sg_shader shader = sg_make_shader(stencilref_test_shader_desc(sg_query_backend()));
        // Each winding has its own scissor row and color: neither can hide the other.
        const float vertices[] = {
            -1,-1, 1,0,0, 3,-1, 1,0,0, -1,3, 1,0,0,
            -1,3, 0,1,0, 3,-1, 0,1,0, -1,-1, 0,1,0,
        };
        sg_buffer_desc bufferDesc = {};
        bufferDesc.data = SG_RANGE(vertices);
        sg_buffer buffer = sg_make_buffer(&bufferDesc);
        sg_buffer alternateBuffer = sg_make_buffer(&bufferDesc);
        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = buffer;
        sg_pipeline_desc desc = {};
        desc.shader = shader;
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
        desc.face_winding = SG_FACEWINDING_CCW;
        desc.sample_count = 1;
        desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.stencil.enabled = true;
        desc.stencil.read_mask = 0x7F; // Exercise the pipeline's read mask too.
        desc.stencil.write_mask = 0xFF;
        desc.stencil.front.compare = SG_COMPAREFUNC_ALWAYS;
        desc.stencil.front.pass_op = SG_STENCILOP_REPLACE;
        desc.stencil.back = desc.stencil.front;
        desc.colors[0].write_mask = SG_COLORMASK_NONE;
        sg_pipeline writer = sg_make_pipeline(&desc);
        desc.colors[0].write_mask = SG_COLORMASK_RGBA;
        desc.stencil.front.compare = SG_COMPAREFUNC_EQUAL;
        desc.stencil.front.pass_op = SG_STENCILOP_KEEP;
        desc.stencil.back = desc.stencil.front;
        desc.stencil.write_mask = 0;
        desc.stencil.ref = 2;
        sg_pipeline equal = sg_make_pipeline(&desc);
        desc.stencil.back.compare = SG_COMPAREFUNC_NOT_EQUAL;
        sg_pipeline mixed = sg_make_pipeline(&desc);
        desc.stencil.enabled = false;
        sg_pipeline disabled = sg_make_pipeline(&desc);
        check("resources valid", sg_query_pipeline_state(writer) == SG_RESOURCESTATE_VALID &&
            sg_query_pipeline_state(equal) == SG_RESOURCESTATE_VALID &&
            sg_query_pipeline_state(mixed) == SG_RESOURCESTATE_VALID &&
            sg_query_pipeline_state(disabled) == SG_RESOURCESTATE_VALID);

        auto drawFaces = [](int row) {
            for (int face = 0; face < 2; ++face) {
                sg_apply_scissor_rect(0, row * 4 + face * 2, 24, 2, true);
                sg_draw(face * 3, 3, 1);
            }
        };
        target.begin(0, 0, 0, 1);
        internal::ensureCurrentPass();
        sg_apply_pipeline(writer);
        for (int column = 0; column < 3; ++column) {
            sg_apply_stencil_ref(uint8_t(column + 129)); // masked to 1, 2, 3
            sg_apply_bindings(&bindings);
            sg_apply_scissor_rect(column * 8, 0, 8, 40, true);
            sg_draw(0, 3, 1);
        }
        sg_apply_pipeline(equal);
        sg_apply_bindings(&bindings);
        for (int row = 0; row < 3; ++row) {
            sg_apply_stencil_ref(uint8_t(row + 1));
            // Change the actual vertex binding to exercise GL cache paths.
            bindings.vertex_buffers[0] = row % 2 == 0 ? alternateBuffer : buffer;
            sg_apply_bindings(&bindings);
            drawFaces(row);
        }
        sg_apply_stencil_ref(3);
        sg_apply_pipeline(equal); // Same pipeline: must reset to its own ref 2.
        sg_apply_bindings(&bindings);
        drawFaces(3);
        sg_apply_stencil_ref(3);
        target.end();

        // New pass, same EQUAL pipeline: previous override cannot carry over.
        target.begin();
        internal::ensureCurrentPass();
        sg_apply_pipeline(equal);
        sg_apply_bindings(&bindings);
        drawFaces(4);
        sg_apply_pipeline(disabled);
        sg_apply_stencil_ref(255);
        sg_apply_bindings(&bindings);
        drawFaces(5);
        sg_apply_pipeline(equal);
        sg_apply_bindings(&bindings);
        drawFaces(6);
        sg_apply_pipeline(mixed);
        sg_apply_bindings(&bindings);
        for (int ref = 1; ref <= 2; ++ref) {
            sg_apply_stencil_ref(uint8_t(ref));
            drawFaces(7 + ref);
        }
        target.end();
        Pixels pixels;
        pixels.allocate(24, 40, 4);
        check("readback", target.readPixels(pixels.getData()));
        const int selected[] = {0, 1, 2, 1, 1, -1, 1, -2, 0, 1};
        const char* labels[] = {"ref 1 after bindings", "ref 2 after bindings",
            "ref 3 after bindings", "same pipeline resets own ref", "pass transition resets ref",
            "disabled stencil ignores override", "disabled override does not affect next pipeline",
            "untouched pixels", "mixed comparisons ref 1", "mixed comparisons ref 2"};
        for (int row = 0; row < 10; ++row) {
            for (int face = 0; face < 2; ++face) {
                bool exact = true;
                for (int y = row * 4 + face * 2; y < row * 4 + face * 2 + 2; ++y) {
                    for (int x = 0; x < 24; ++x) {
                        bool colored = selected[row] == -1 || x / 8 == selected[row];
                        if (row >= 8 && face == 1) colored = !colored;
                        for (int channel = 0; channel < 4; ++channel) {
                            const int expected = channel == 3 || (colored && channel == face) ? 255 : 0;
                            exact &= pixels.getData()[(y * 24 + x) * 4 + channel] == expected;
                        }
                    }
                }
                char label[128];
                std::snprintf(label, sizeof(label), "%s, %s", labels[row], face == 0 ? "front/red" : "back/green");
                check(label, exact);
            }
        }
        sg_destroy_pipeline(mixed);
        sg_destroy_pipeline(disabled);
        sg_destroy_pipeline(equal);
        sg_destroy_pipeline(writer);
        sg_destroy_buffer(alternateBuffer);
        sg_destroy_buffer(buffer);
        sg_destroy_shader(shader);
        completed = true;
        exitApp();
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::puts("SKIP: stencilRef needs --gpu-check and a display");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(32, 32).setHighDpi(false).setSampleCount(1).setSwapInterval(0);
    return tcCoreTest::runDisplayTest<StencilRefApp>(settings, failures, [] {
        check("test completed", completed);
    });
}
