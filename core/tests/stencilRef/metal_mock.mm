#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#define nil ((id)0)
#define _SOKOL_PRIVATE static
#define _SOKOL_UNUSED(x) (void)(x)
#define SOKOL_ASSERT assert
#define _sg_stats_inc(x) ((void)0)
#define _SG_MTL_INVALID_SLOT_INDEX (-1)
extern "C" id class_createInstance(Class, size_t);
extern "C" id object_dispose(id);
static unsigned referenceValue, pipelineChanges;
__attribute__((objc_root_class))
@interface Encoder { Class isa; }
+ (id)alloc;
- (void)setStencilReferenceValue:(unsigned)value;
- (void)setComputePipelineState:(id)value;
- (void)setBlendColorRed:(float)r green:(float)g blue:(float)b alpha:(float)a;
- (void)setCullMode:(int)value;
- (void)setFrontFacingWinding:(int)value;
- (void)setDepthBias:(float)value slopeScale:(float)slope clamp:(float)clampValue;
- (void)setRenderPipelineState:(id)value;
- (void)setDepthStencilState:(id)value;
@end
@implementation Encoder
+ (id)alloc { return class_createInstance(self, 0); }
- (void)setStencilReferenceValue:(unsigned)value { referenceValue = value; }
- (void)setComputePipelineState:(id)value { (void)value; }
- (void)setBlendColorRed:(float)r green:(float)g blue:(float)b alpha:(float)a { (void)r; (void)g; (void)b; (void)a; }
- (void)setCullMode:(int)value { (void)value; }
- (void)setFrontFacingWinding:(int)value { (void)value; }
- (void)setDepthBias:(float)value slopeScale:(float)slope clamp:(float)clampValue { (void)value; (void)slope; (void)clampValue; }
- (void)setRenderPipelineState:(id)value { (void)value; ++pipelineChanges; }
- (void)setDepthStencilState:(id)value { (void)value; }
@end
struct sg_color { float r, g, b, a; };
struct Slot { int id; };
struct _sg_pipeline_t {
    Slot slot;
    struct { bool is_compute; sg_color blend_color; struct { float bias, bias_slope_scale, bias_clamp; } depth; } cmn;
    struct { int cps, cull_mode, winding, rps, dss; unsigned stencil_ref; } mtl;
};
static struct {
    struct { bool is_compute; } cur_pass;
    struct { struct { Slot cur_pip; } cache; Encoder* render_cmd_encoder; Encoder* compute_cmd_encoder; } mtl;
} _sg;
static bool _sg_sref_slot_eql(const Slot* a, const Slot* b) { return a->id == b->id; }
static Slot _sg_sref(const Slot* value) { return value ? *value : Slot{}; }
static id _sg_mtl_id(int slot) { (void)slot; return nil; }
#include "functions.inc"
int main() {
    _sg.mtl.render_cmd_encoder = [Encoder alloc];
    _sg_pipeline_t pipeline = {};
    pipeline.slot.id = 1;
    pipeline.mtl.stencil_ref = 2;
    _sg_mtl_apply_pipeline(&pipeline);
    assert(referenceValue == 2 && pipelineChanges == 1);
    _sg_mtl_apply_stencil_ref(&pipeline, 3);
    assert(referenceValue == 3 && pipeline.mtl.stencil_ref == 2);
    _sg_mtl_apply_pipeline(&pipeline);
    assert(referenceValue == 2 && pipelineChanges == 2);
    object_dispose(_sg.mtl.render_cmd_encoder);
    std::puts("Metal production bodies: override and same-pipeline reset PASS (mock encoder)");
}
