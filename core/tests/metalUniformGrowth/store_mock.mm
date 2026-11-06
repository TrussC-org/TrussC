// The production swapchain adapter runs against plain descriptor values.
// No Apple SDK/device: this checks action translation, not GPU retention.
#include <cassert>
#include <cstdio>
#include <cstddef>
#define _SOKOL_PRIVATE static
#define SOKOL_ASSERT assert
#define SOKOL_UNREACHABLE assert(false)
#define nil ((id)0)
enum sg_load_action { SG_LOADACTION_CLEAR, SG_LOADACTION_LOAD, SG_LOADACTION_DONTCARE };
enum sg_store_action { SG_STOREACTION_STORE, SG_STOREACTION_DONTCARE };
enum MTLLoadAction { MTLLoadActionClear, MTLLoadActionLoad, MTLLoadActionDontCare };
enum MTLStoreAction { MTLStoreActionStore, MTLStoreActionDontCare,
    MTLStoreActionStoreAndMultisampleResolve, MTLStoreActionMultisampleResolve };
@protocol MTLTexture
@end
extern "C" id class_createInstance(Class, size_t);
__attribute__((objc_root_class))
@interface Texture <MTLTexture> { Class isa; }
+ (id)alloc;
@end
@implementation Texture
+ (id)alloc { return class_createInstance(self, 0); }
@end
struct Attachment {
    id<MTLTexture> texture = nil;
    MTLLoadAction loadAction = MTLLoadActionDontCare;
    MTLStoreAction storeAction = MTLStoreActionDontCare;
    double clearDepth = 0;
    unsigned clearStencil = 0;
};
struct Descriptor { Attachment depthAttachment, stencilAttachment; };
struct Swapchain {
    struct { const void* depth_stencil_texture; } metal;
    int depth_format;
};
struct Action {
    struct { sg_load_action load_action; sg_store_action store_action; double clear_value; } depth, stencil;
};
static bool _sg_is_depth_stencil_format(int format) { return format == 2; }
#include "store.inc"
static int failures;
static void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
int main() {
    Swapchain swapchain = {{(const void*)[Texture alloc]}, 2};
    Action action = {{SG_LOADACTION_LOAD, SG_STOREACTION_STORE, 1},
                     {SG_LOADACTION_LOAD, SG_STOREACTION_STORE, 7}};
    Descriptor desc;
    apply(&swapchain, &action, desc);
    check("swapchain adapter preserves depth and stencil on continuation",
        desc.depthAttachment.storeAction == MTLStoreActionStore &&
        desc.stencilAttachment.storeAction == MTLStoreActionStore &&
        desc.depthAttachment.loadAction == MTLLoadActionLoad &&
        desc.stencilAttachment.loadAction == MTLLoadActionLoad);
    action.stencil = {SG_LOADACTION_CLEAR, SG_STOREACTION_DONTCARE, 7};
    apply(&swapchain, &action, desc);
    check("swapchain adapter retains configured initial stencil clear and default discard",
        desc.stencilAttachment.loadAction == MTLLoadActionClear &&
        desc.stencilAttachment.storeAction == MTLStoreActionDontCare &&
        desc.stencilAttachment.clearStencil == 7);
    swapchain.depth_format = 1;
    desc = {};
    apply(&swapchain, &action, desc);
    check("depth-only swapchain has no stencil attachment", desc.stencilAttachment.texture == nil);
    return failures ? 1 : 0;
}
