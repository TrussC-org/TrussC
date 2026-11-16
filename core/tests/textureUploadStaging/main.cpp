// Standalone dummy-device unit test: production pool, no window or TrussC TU.
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include <tc/gpu/detail/tcTextureUploadStaging.h>
#include <cstdio>

namespace {
using Pool = trussc::internal::TextureUploadStaging;
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

void lifetime() {
    sg_desc desc = {};
    sg_setup(&desc);
    sg_enable_stats();
    {
        Pool pool;
        std::array<sg_buffer, Pool::frameSets> first = {}, second = {};
        bool distinct = true;
        for (size_t frame = 0; frame < Pool::frameSets; ++frame) {
            first[frame] = pool.acquire(5, false, frame).buffer;
            second[frame] = pool.acquire(16, false, frame).buffer;
            distinct &= first[frame].id != second[frame].id;
            for (size_t prior = 0; prior < frame; ++prior)
                distinct &= first[frame].id != first[prior].id;
            sg_commit();
        }
        check("K frames use distinct ordinal buffers", distinct);
        check("capacity rounds to four bytes", sg_query_buffer_desc(first[0]).size == 8);
        const auto usage = sg_query_buffer_desc(first[0]).usage;
        check("CPU staging flags preserved", usage.staging_buffer && usage.copy_src && usage.write_transient);
        check("slot zero reused only K frames later", pool.acquire(4, false, Pool::frameSets).buffer.id == first[0].id);
        const auto grown = pool.acquire(32, false, Pool::frameSets).buffer;
        check("only undersized slot recreated", grown.id != second[0].id &&
            sg_query_buffer_desc(grown).size == 32 &&
            sg_query_buffer_state(second[0]) == SG_RESOURCESTATE_INVALID);
        sg_commit();
        bool reused = true;
        const auto before = sg_query_stats().total.buffers;
        for (size_t frame = Pool::frameSets + 1; frame <= 2 * Pool::frameSets; ++frame) {
            reused &= pool.acquire(4, false, frame).buffer.id == first[frame % Pool::frameSets].id;
            pool.acquire(8, false, frame);
            sg_commit();
        }
        const auto after = sg_query_stats().total.buffers;
        check("smaller uploads reuse each rotated set", reused);
        check("warm pool makes/destroys zero buffers", before.allocated == after.allocated &&
            before.deallocated == after.deallocated);
        const auto gpuBuffer = pool.acquire(4, true, 3 * Pool::frameSets).buffer;
        check("backend mode change recreates slot", gpuBuffer.id != first[0].id &&
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

void burstAndIdle() {
    sg_desc desc = {};
    sg_setup(&desc);
    sg_enable_stats();
    {
        Pool pool;
        sg_buffer big = {};
        bool temporary = true;
        for (size_t frame = 0; frame < Pool::frameSets; ++frame) {
            for (size_t ordinal = 0; ordinal < 100; ++ordinal) {
                const auto upload = pool.acquire(ordinal == 1 ? 1024 * 1024 : 16, false, frame);
                if (frame == 0 && ordinal == 1) big = upload.buffer;
                temporary &= upload.temporary == (ordinal >= Pool::slotsPerFrame);
                Pool::release(upload);
                if (upload.temporary)
                    temporary &= sg_query_buffer_state(upload.buffer) == SG_RESOURCESTATE_INVALID;
            }
            sg_commit();
        }
        check("burst overflow uses per-upload make/destroy", temporary);
        const auto stats = sg_query_stats().total.buffers;
        check("100-upload bursts retain only N times K slots", stats.alive == Pool::slotsPerFrame * Pool::frameSets);
        check("all overflow buffers were destroyed", stats.allocated == 100 * Pool::frameSets &&
            stats.deallocated == (100 - Pool::slotsPerFrame) * Pool::frameSets);
        // Ordinal zero remains busy while the other ordinals go idle.
        for (uint64_t frame = Pool::frameSets; frame < Pool::idleFrames; ++frame) {
            pool.acquire(16, false, frame);
            sg_commit();
        }
        check("one-off big slot retained before 120 idle frames", sg_query_buffer_state(big) == SG_RESOURCESTATE_VALID);
        pool.acquire(16, false, Pool::idleFrames);
        check("one-off big upload released at 120 idle frames", sg_query_buffer_state(big) == SG_RESOURCESTATE_INVALID);
        sg_commit();
        pool.acquire(16, false, Pool::idleFrames + Pool::frameSets);
        check("idle ordinals released while ordinary uploads continue", sg_query_stats().total.buffers.alive == Pool::frameSets);
        sg_commit();
        const auto prior = sg_query_stats().total.buffers;
        const auto fresh = pool.acquire(16, false, 2 * Pool::idleFrames + Pool::frameSets);
        const auto current = sg_query_stats().total.buffers;
        check("idle pool releases every retained buffer before acquiring", current.deallocated == prior.deallocated + Pool::frameSets && current.alive == 1);
        check("idle release resets capacity to requested size", sg_query_buffer_desc(fresh.buffer).size == 16);
    }
    check("burst pool cleanup balances allocations", sg_query_stats().total.buffers.alive == 0);
    sg_shutdown();
}

void allocationFailure() {
    sg_desc desc = {};
    desc.buffer_pool_size = 1;
    sg_setup(&desc);
    {
        Pool pool;
        const auto first = pool.acquire(4, false, 0).buffer;
        check("first slot allocated in small resource pool", first.id != 0);
        check("allocation failure returns no staging buffer", pool.acquire(4, false, 0).buffer.id == 0);
        sg_commit();
        check("allocation failure does not poison existing slot", pool.acquire(4, false, Pool::frameSets).buffer.id == first.id);
    }
    sg_shutdown();
}
} // namespace

int main() {
    lifetime();
    // A fresh owner and device must not carry handles/cursor/capacity over.
    lifetime();
    burstAndIdle();
    allocationFailure();
    return failures ? 1 : 0;
}
