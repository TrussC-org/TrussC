// Exercise both public sgl pipeline constructors and context defaults without a GPU.
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include "sokol_gfx.h"
#include "util/sokol_gl_tc.h"
#include <stdio.h>

static int failures;
static void check_layout(const char* label, sgl_pipeline handle, sg_shader shader,
                         int attr_count, int first_offset) {
    const _sgl_pipeline_t* pipeline = _sgl_lookup_pipeline(handle.id);
    bool valid = pipeline && pipeline->slot.state == SG_RESOURCESTATE_VALID;
    const sg_vertex_format formats[] = {
        SG_VERTEXFORMAT_FLOAT3, SG_VERTEXFORMAT_FLOAT2,
        SG_VERTEXFORMAT_FLOAT4, SG_VERTEXFORMAT_FLOAT
    };
    const int offsets[] = { first_offset, 12, 20, 36 };
    for (int primitive = 0; valid && primitive < SGL_NUM_PRIMITIVE_TYPES; ++primitive) {
        const sg_pipeline gpu_pipeline = pipeline->pip[primitive];
        const sg_pipeline_desc desc = sg_query_pipeline_desc(gpu_pipeline);
        valid = sg_query_pipeline_state(gpu_pipeline) == SG_RESOURCESTATE_VALID &&
                desc.shader.id == shader.id && desc.layout.buffers[0].stride == 40;
        for (int attr = 0; valid && attr < attr_count; ++attr) {
            valid = desc.layout.attrs[attr].format == formats[attr] &&
                    desc.layout.attrs[attr].offset == offsets[attr];
        }
        valid = valid && desc.layout.attrs[attr_count].format == SG_VERTEXFORMAT_INVALID;
    }
    printf("%s: %s\n", label, valid ? "PASS" : "FAIL");
    if (!valid) ++failures;
}

int main(void) {
    sg_setup(&(sg_desc){0});
    sgl_setup(&(sgl_desc_t){0});
    const sg_shader custom_shader = sg_make_shader(&(sg_shader_desc){0});
    const sgl_context contexts[] = {
        sgl_default_context(), sgl_make_context(&(sgl_context_desc_t){0})
    };
    for (int context_index = 0; context_index < 2; ++context_index) {
        const sgl_context context = contexts[context_index];
        check_layout("context default keeps point size", _sgl_lookup_context(context.id)->def_pip,
                     _sgl.shd, 4, 0);
        sgl_set_context(context);
        for (int explicit_context = 0; explicit_context < 2; ++explicit_context) {
            sg_pipeline_desc desc = {0};
            // No shader or layout: retain the existing four-attribute contract.
            sgl_pipeline pipeline = explicit_context ? sgl_context_make_pipeline(context, &desc) : sgl_make_pipeline(&desc);
            check_layout("built-in shader", pipeline, _sgl.shd, 4, 0);
            sgl_destroy_pipeline(pipeline);
            desc.shader = custom_shader;
            pipeline = explicit_context ? sgl_context_make_pipeline(context, &desc) : sgl_make_pipeline(&desc);
            check_layout("custom shader without layout", pipeline, custom_shader, 4, 0);
            sgl_destroy_pipeline(pipeline);
            desc.layout.buffers[0].stride = 40;
            desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
            desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
            desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
            pipeline = explicit_context ? sgl_context_make_pipeline(context, &desc) : sgl_make_pipeline(&desc);
            check_layout("custom shader with three attributes", pipeline, custom_shader, 3, 0);
            sgl_destroy_pipeline(pipeline);
            // A caller can also select a subset with its own byte offsets.
            desc.layout.attrs[0].offset = 12;
            desc.layout.attrs[1].format = SG_VERTEXFORMAT_INVALID;
            desc.layout.attrs[2].format = SG_VERTEXFORMAT_INVALID;
            pipeline = explicit_context ? sgl_context_make_pipeline(context, &desc) : sgl_make_pipeline(&desc);
            check_layout("custom shader preserves explicit offset", pipeline, custom_shader, 1, 12);
            sgl_destroy_pipeline(pipeline);
            desc.shader.id = SG_INVALID_ID;
            pipeline = explicit_context ? sgl_context_make_pipeline(context, &desc) : sgl_make_pipeline(&desc);
            check_layout("built-in shader keeps its layout", pipeline, _sgl.shd, 4, 0);
            sgl_destroy_pipeline(pipeline);
        }
    }
    sgl_destroy_context(contexts[1]);
    sg_destroy_shader(custom_shader);
    sgl_shutdown();
    sg_shutdown();
    return failures ? 1 : 0;
}
