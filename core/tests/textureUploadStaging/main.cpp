// Standalone dummy-device unit test: production pool, no window or TrussC TU.
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include <tc/gpu/detail/tcTextureUploadStaging.h>
#include <cstdio>

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

void lifetime() {
    sg_desc desc = {};
    desc.buffer_pool_size = 8;
    sg_setup(&desc);
    sg_enable_stats();
    {
        trussc::internal::TextureUploadStaging pool;
        const auto first = pool.acquire(5, false, 0);
        const auto second = pool.acquire(16, false, 0);
        check("same-frame ordinals use different buffers", first.id != second.id);
        check("capacity rounds to four bytes", sg_query_buffer_desc(first).size == 8);
        check("CPU staging flags preserved", sg_query_buffer_desc(first).usage.staging_buffer);
        sg_commit();
        check("slot zero reused next frame", pool.acquire(4, false, 1).id == first.id);
        const auto grown = pool.acquire(32, false, 1);
        check("only undersized slot recreated", grown.id != second.id &&
            sg_query_buffer_desc(grown).size == 32 &&
            sg_query_buffer_state(second) == SG_RESOURCESTATE_INVALID);
        sg_commit();
        const auto before = sg_query_stats().total.buffers;
        check("smaller upload retains large slot", pool.acquire(4, false, 2).id == first.id &&
            pool.acquire(8, false, 2).id == grown.id);
        const auto after = sg_query_stats().total.buffers;
        check("warm pool makes/destroys zero buffers", before.allocated == after.allocated &&
            before.deallocated == after.deallocated);
        sg_commit();
        const auto gpuBuffer = pool.acquire(4, true, 3);
        check("backend mode change recreates slot", gpuBuffer.id != first.id &&
            !sg_query_buffer_desc(gpuBuffer).usage.staging_buffer);
        pool.padded.resize(256);
        const auto* storage = pool.padded.data();
        pool.padded.resize(64);
        check("padding CPU storage retained", storage == pool.padded.data());
    }
    check("device-owned pool releases every slot", sg_query_stats().total.buffers.alive == 0 &&
        sg_query_stats().total.buffers.allocated == sg_query_stats().total.buffers.deallocated);
    sg_shutdown();
}
void allocationFailure() {
    sg_desc desc = {};
    desc.buffer_pool_size = 1;
    sg_setup(&desc);
    {
        trussc::internal::TextureUploadStaging pool;
        const auto first = pool.acquire(4, false, 0);
        check("first slot allocated in small resource pool", first.id != 0);
        check("allocation failure returns no staging buffer", pool.acquire(4, false, 0).id == 0);
        sg_commit();
        check("allocation failure does not poison existing slot", pool.acquire(4, false, 1).id == first.id);
    }
    sg_shutdown();
}
} // namespace

int main() {
    lifetime();
    // A fresh owner and device must not carry handles/cursor/capacity over.
    lifetime();
    allocationFailure();
    return failures ? 1 : 0;
}
