
#include <cstdio>
#include <cassert>
#include <cstddef>
#define SOKOL_METAL
#define SOKOL_API_IMPL
#define _SOKOL_PRIVATE static
#define SOKOL_ASSERT assert
#define nil ((id)0)
extern "C" id class_createInstance(Class,size_t);
static bool activeEncoder;
static int invalidCommits, ended;
static bool acquireSucceeds = true;
using dispatch_time_t = unsigned long long;
constexpr dispatch_time_t DISPATCH_TIME_NOW = 0, NSEC_PER_SEC = 1000000000;
static dispatch_time_t dispatch_time(dispatch_time_t, dispatch_time_t delay) { return delay; }
__attribute__((objc_root_class))
@interface Obj { Class isa; }
+ (id)alloc;
- (void)endEncoding;
- (void)commit;
- (void)waitUntilCompleted;
@end
@implementation Obj
+ (id)alloc { return class_createInstance(self,0); }
- (void)endEncoding { activeEncoder=false; ++ended; }
- (void)commit { if(activeEncoder) ++invalidCommits; }
- (void)waitUntilCompleted {}
@end
static struct {
    bool valid=true;
    struct { bool in_pass=false; } cur_pass;
    struct { Obj* cmd_buffer; Obj* blit_cmd_encoder; Obj* render_cmd_encoder; Obj* compute_cmd_encoder; } mtl;
} _sg;

static bool _sg_mtl_acquire_command_buffer(dispatch_time_t) { return acquireSucceeds; }
#include "seams.inc"
static int failures;
static void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
static void beginUpload() {
    _sg.mtl.cmd_buffer = [Obj alloc]; _sg.mtl.blit_cmd_encoder = [Obj alloc];
    activeEncoder = true; invalidCommits = ended = 0;
}
int main() {
    beginUpload();
    sg_tc_submit_prefix();
    check("prefix finishes upload encoder before commit",
        invalidCommits == 0 && ended == 1 && !_sg.mtl.blit_cmd_encoder && !_sg.mtl.cmd_buffer);
    beginUpload();
    const void* buffer = sg_tc_mtl_command_buffer();
    check("capture finishes upload encoder before handing out command buffer",
        buffer == (const void*)_sg.mtl.cmd_buffer && !activeEncoder && ended == 1 && !_sg.mtl.blit_cmd_encoder);
    // A native capture encoder can now open and finish before prefix submission.
    activeEncoder = true;
    [[Obj alloc] endEncoding];
    sg_tc_submit_prefix();
    check("capture followed by prefix has no invalid commit", invalidCommits == 0);
    beginUpload(); sg_tc_mtl_flush();
    check("flush keeps its existing encoder termination",
        invalidCommits == 0 && ended == 1 && !_sg.mtl.blit_cmd_encoder && !_sg.mtl.cmd_buffer);
    acquireSucceeds = false;
    check("capture propagates acquisition failure", !sg_tc_mtl_command_buffer());
    return failures ? 1 : 0;
}
