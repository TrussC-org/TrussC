// GNU Objective-C messages exercise the unmodified production function bodies.
// This checks CPU state/bindings, not Metal encoding, GPU lifetime or pixels.
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using NSUInteger = unsigned long;
using MTLResourceOptions = unsigned long;
constexpr MTLResourceOptions MTLResourceCPUCacheModeWriteCombined = 1;
constexpr MTLResourceOptions MTLResourceStorageModeShared = 2;
#define nil ((id)0)
extern "C" id class_createInstance(Class, size_t);
extern "C" id object_dispose(id);
@protocol MTLBuffer
- (NSUInteger)length;
- (void*)contents;
@end
__attribute__((objc_root_class))
@interface MockObject { Class isa; }
+ (id)alloc;
@end
@implementation MockObject
+ (id)alloc { return class_createInstance(self, 0); }
@end
@interface MockBuffer : MockObject <MTLBuffer> {
@public
    NSUInteger size;
    unsigned char* data;
}
@end
@implementation MockBuffer
- (NSUInteger)length { return size; }
- (void*)contents { return data; }
@end
static bool failAllocation = false;
static std::vector<MockBuffer*> allocated;
@interface MockDevice : MockObject
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
@end
@implementation MockDevice
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    (void)options;
    if (failAllocation) return nil;
    auto* buffer = [MockBuffer alloc];
    buffer->size = length;
    buffer->data = static_cast<unsigned char*>(std::calloc(length, 1));
    allocated.push_back(buffer);
    return buffer;
}
@end
struct Binding { id<MTLBuffer> buffer; NSUInteger offset; };
static Binding vertex[8], fragment[8], compute[8];
@interface MockEncoder : MockObject
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setFragmentBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setVertexBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setFragmentBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot;
@end
@implementation MockEncoder
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot { vertex[slot] = {buffer, offset}; }
- (void)setFragmentBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot { fragment[slot] = {buffer, offset}; }
- (void)setBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot { compute[slot] = {buffer, offset}; }
- (void)setVertexBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot { vertex[slot].offset = offset; }
- (void)setFragmentBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot { fragment[slot].offset = offset; }
- (void)setBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot { compute[slot].offset = offset; }
@end
enum sg_shader_stage { SG_SHADERSTAGE_VERTEX, SG_SHADERSTAGE_FRAGMENT, SG_SHADERSTAGE_COMPUTE };
struct sg_range { const void* ptr; size_t size; };
struct _sg_shader_t {
    struct { struct { size_t size; sg_shader_stage stage; } uniform_blocks[8]; } cmn;
    struct { NSUInteger ub_buffer_n[8]; } mtl;
};
struct _sg_pipeline_t { struct { _sg_shader_t* shader; } cmn; };
struct {
    struct {
        int ub_size, cur_ub_offset;
        unsigned char* cur_ub_base_ptr;
        unsigned cur_frame_rotate_index;
        MockDevice* device;
        MockEncoder* render_cmd_encoder;
        MockEncoder* compute_cmd_encoder;
        id<MTLBuffer> uniform_buffers[2];
        struct { int free_queue_top = 64, num_slots = 64, release_queue_front = 0, release_queue_back = 0; } idpool;
    } mtl;
    struct { bool valid = true, is_compute = false; } cur_pass;
    unsigned frame_index = 1;
    bool next_draw_valid = true;
    _sg_pipeline_t* cur_pip;
} _sg;
static std::vector<id<MTLBuffer>> retired;
static int errors = 0;
static _sg_pipeline_t* _sg_pipeline_ref_ptr(_sg_pipeline_t** ref) { return *ref; }
static _sg_shader_t* _sg_shader_ref_ptr(_sg_shader_t* const* ref) { return *ref; }
static int _sg_mtl_add_resource(id buffer) { retired.push_back(buffer); --_sg.mtl.idpool.free_queue_top; return 1; }
static void _sg_mtl_release_resource(unsigned, int) { ++_sg.mtl.idpool.release_queue_front; }
static int _sg_roundup(int value, int alignment) { return (value + alignment - 1) & ~(alignment - 1); }
static void _sg_log(int, int, const char*, int) {}
constexpr int SG_LOGITEM_METAL_UNIFORM_BUFFER_GROWN = 0;
#define _SOKOL_PRIVATE static
#define SOKOL_ASSERT assert
#define SOKOL_UNREACHABLE std::abort()
#define SG_MAX_UNIFORMBLOCK_BINDSLOTS 8
#define _SG_MTL_UB_ALIGN 256
#define _sg_stats_inc(x) ((void)0)
#define _SG_ERROR(x) (++errors)
#define _SG_OBJC_RELEASE(obj) ((obj) = nil)
#include "functions.inc"

static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
static uint32_t value(Binding binding) {
    uint32_t result = 0;
    if (binding.buffer) std::memcpy(&result, (unsigned char*)[binding.buffer contents] + binding.offset, sizeof(result));
    return result;
}
int main() {
    _sg.mtl.device = [MockDevice alloc];
    _sg.mtl.render_cmd_encoder = [MockEncoder alloc];
    _sg.mtl.compute_cmd_encoder = _sg.mtl.render_cmd_encoder;
    _sg.mtl.ub_size = 512;
    _sg.mtl.cur_ub_offset = 256;
    _sg.mtl.uniform_buffers[0] = [_sg.mtl.device newBufferWithLength:512 options:0];
    _sg.mtl.cur_ub_base_ptr = (unsigned char*)[_sg.mtl.uniform_buffers[0] contents];
    _sg_shader_t shader = {};
    _sg_pipeline_t pipeline = {{&shader}};
    _sg.cur_pip = &pipeline;
#if defined(HAS_PASS_BIND)
    _sg_mtl_bind_uniform_buffers();
#endif
    const sg_shader_stage stages[] = {SG_SHADERSTAGE_VERTEX, SG_SHADERSTAGE_FRAGMENT,
        SG_SHADERSTAGE_VERTEX, SG_SHADERSTAGE_COMPUTE, SG_SHADERSTAGE_COMPUTE};
    const NSUInteger slots[] = {2, 5, 6, 1, 7};
    for (int i = 0; i < 5; ++i) {
        shader.cmn.uniform_blocks[i] = {16, stages[i]};
        shader.mtl.ub_buffer_n[i] = slots[i];
    }
    for (int i = 0; i < 5; ++i) {
        uint32_t data[4] = {uint32_t(11 + i), 0, 0, 0};
        sg_range range = {data, sizeof(data)};
        _sg.cur_pass.is_compute = i >= 3;
#if defined(HAS_PASS_BIND)
        if (i == 3) _sg_mtl_bind_uniform_buffers();
#endif
        // Every subsequent block grows: tests several generations while earlier
        // slots retain their own buffer and nonzero offset.
        if (i) _sg.mtl.cur_ub_offset = _sg.mtl.ub_size;
        _sg_mtl_apply_uniforms(i, &range);
    }
    check("split vertex/fragment and later vertex keep earlier bytes",
          value(vertex[2]) == 11 && value(fragment[5]) == 12 && value(vertex[6]) == 13);
    check("compute growth preserves both independent slots", value(compute[1]) == 14 && value(compute[7]) == 15);
    check("each replaced buffer is retired", retired.size() == 4);
    // Base has no transactional failure path: stop after detecting its binding
    // regression instead of dereferencing the nil buffer it would install.
    if (!failures) {
        id<MTLBuffer> before = _sg.mtl.uniform_buffers[0];
        const int size = _sg.mtl.ub_size;
        const int offset = _sg.mtl.cur_ub_offset;
        auto* base = _sg.mtl.cur_ub_base_ptr;
        failAllocation = true;
        _sg_mtl_grow_uniform_buffer(16);
        check("allocation failure leaves all old state intact",
              before == _sg.mtl.uniform_buffers[0] && size == _sg.mtl.ub_size
              && offset == _sg.mtl.cur_ub_offset && base == _sg.mtl.cur_ub_base_ptr && retired.size() == 4);
        _sg_mtl_grow_uniform_buffer(SIZE_MAX);
        check("unrepresentable size leaves old state intact", size == _sg.mtl.ub_size && retired.size() == 4);
        _sg.mtl.cur_ub_offset = size;
        uint32_t data[4] = {};
        sg_range range = {data, sizeof(data)};
        _sg_mtl_apply_uniforms(4, &range);
        check("failed upload rejects draw without cursor or binding changes",
              !_sg.next_draw_valid && _sg.mtl.cur_ub_offset == size && value(compute[7]) == 15);
        check("allocation failures are reported", errors >= 3);
        failAllocation = false;
        _sg.mtl.idpool.free_queue_top = 0;
        _sg_mtl_grow_uniform_buffer(16);
        check("retirement exhaustion leaves live storage intact",
              before == _sg.mtl.uniform_buffers[0] && _sg.mtl.cur_ub_offset == size
              && retired.size() == 4);
        _sg.mtl.idpool.free_queue_top = 60;
        // Odd user reservations can leave an aligned cursor past capacity.
        // The next upload must grow rather than subtracting with underflow.
        _sg.mtl.ub_size = 300;
        _sg.mtl.cur_ub_offset = 512;
        _sg.next_draw_valid = true;
        _sg_mtl_apply_uniforms(4, &range);
        check("unaligned reserve grows safely after cursor rounding",
              _sg.next_draw_valid && _sg.mtl.uniform_buffers[0] != before
              && _sg.mtl.cur_ub_offset == 256 && _sg.mtl.ub_size == 600);
    }
    for (auto* buffer : allocated) { std::free(buffer->data); object_dispose(buffer); }
    object_dispose(_sg.mtl.device);
    object_dispose(_sg.mtl.render_cmd_encoder);
    std::printf("metalUniformGrowth backend mock: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
