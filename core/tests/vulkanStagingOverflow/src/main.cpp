#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>

using namespace tc;
namespace {
int failures = 0;
bool completed = false;
void check(const char* label, bool okay) {
    std::printf("vulkanStagingOverflow: %s: %s\n", label, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
struct UploadApp : App {
    Pixels largePixels, smallPixels;
    Texture filler, target, another;
    StorageBuffer storage;
    EventListener logs;
    int frame = 0, warnings = 0, recoveries = 0, errors = 0;
    bool vulkan = false;
    uint64_t atlasVersionBefore = 0;
    void setup() override {
        vulkan = sg_query_backend() == SG_BACKEND_VULKAN;
        sg_enable_stats();
        largePixels.allocate(4096, 2048, 4); // 32 MiB, half the default per-frame budget.
        smallPixels.allocate(64, 64, 4);
        filler.allocate(largePixels, TextureUsage::Dynamic);
        target.allocate(smallPixels, TextureUsage::Dynamic);
        another.allocate(smallPixels, TextureUsage::Dynamic);
        check("storage allocated", storage.allocate(16));
        logs = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level == LogLevel::Warning && e.message.find("Vulkan per-frame staging limit hit") != std::string::npos) {
                ++warnings;
                check("warning names budget and setter", e.message.find("64 MiB per frame") != std::string::npos &&
                    e.message.find("WindowSettings::setVulkanUploadSize()") != std::string::npos);
                if (e.message.find("[Texture]") == 0)
                    check("warning includes dimensions and bytes", e.message.find("64 x 64, 16384 bytes") != std::string::npos);
            }
            if (e.level == LogLevel::Notice && e.message.find("recovered after") != std::string::npos) {
                ++recoveries;
                check("recovery includes skip count", e.message.find("3 skipped uploads") != std::string::npos);
            }
            if (e.level == LogLevel::Error || e.level == LogLevel::Fatal) ++errors;
        });
    }
    void draw() override {
        if (completed) return;
        if (frame == 0) {
            const auto before = sg_query_stats().cur_frame;
            filler.loadData(largePixels);
            const auto after = sg_query_stats().cur_frame;
            check("32 MiB upload copied", after.num_copy_buffer_to_image - before.num_copy_buffer_to_image == 1);
            if (vulkan) check("32 MiB native Vulkan copy", after.vk.num_cmd_copy_buffer_to_image - before.vk.num_cmd_copy_buffer_to_image == 1);
            check("32 MiB upload has no warnings or overflow log", warnings == 0 && errors == 0);
            ++frame;
            return;
        }
        if (frame >= 6) {
            auto& atlas = internal::bitmapFontAtlas();
            const bool overflow = frame == 6 || frame == 8;
            if (overflow) {
                filler.loadData(largePixels);
                filler.loadData(largePixels);
                atlasVersionBefore = atlas.version;
            }
            // First bitmap text upload, then a same-size registry update.
            if (frame == 8) ++bitmapfont::internal::registryVersion();
            const bool pending = !atlas.atlasInitialized || atlas.version != bitmapfont::internal::registryVersion();
            const auto before = sg_query_stats().cur_frame;
            ensureFontAtlasForText("retry");
            const auto after = sg_query_stats().cur_frame;
            const bool skipped = vulkan && overflow;
            check("bitmap atlas copy count", after.num_copy_buffer_to_image - before.num_copy_buffer_to_image == (skipped || !pending ? 0u : 1u));
            if (vulkan) check("bitmap atlas native copy count", after.vk.num_cmd_copy_buffer_to_image - before.vk.num_cmd_copy_buffer_to_image == (skipped || !pending ? 0u : 1u));
            if (skipped) {
                check("bitmap atlas version remains pending", atlas.version == atlasVersionBefore);
                if (frame == 6) check("first bitmap atlas remains unpublished", !atlas.atlasInitialized && atlas.rows == 0);
            } else {
                check("bitmap atlas published after recovery", atlas.atlasInitialized && atlas.rows > 0 && atlas.version == bitmapfont::internal::registryVersion());
                const auto copies = after.num_copy_buffer_to_image;
                ensureFontAtlasForText("retry");
                check("bitmap atlas recovery copied once", sg_query_stats().cur_frame.num_copy_buffer_to_image == copies);
            }
            if (++frame == 10) {
                completed = true;
                exitApp(failures ? 1 : 0);
            }
            return;
        }
        const int episodeFrame = frame - 1;
        if (episodeFrame == 2) {
            Texture movedTexture = std::move(target);
            target = std::move(movedTexture);
            StorageBuffer movedStorage = std::move(storage);
            storage = std::move(movedStorage);
        }
        const bool overflowing = episodeFrame != 3;
        if (overflowing) {
            filler.loadData(largePixels);
            filler.loadData(largePixels); // Exhaust 64 MiB before the next upload.
        }
        const auto before = sg_query_stats().cur_frame;
        target.loadData(smallPixels);
        const auto after = sg_query_stats().cur_frame;
        const uint32_t expectedCopies = vulkan && overflowing ? 0 : 1;
        check("texture copy count", after.num_copy_buffer_to_image - before.num_copy_buffer_to_image == expectedCopies);
        if (vulkan) check("native Vulkan copy count", after.vk.num_cmd_copy_buffer_to_image - before.vk.num_cmd_copy_buffer_to_image == expectedCopies);
        const uint32_t data[4] = {1, 2, 3, 4};
        const auto buffersBefore = sg_query_stats().cur_frame.num_copy_buffer_to_buffer;
        check("storage update result", storage.update(data, sizeof(data)) == !(vulkan && overflowing));
        check("storage copy count", sg_query_stats().cur_frame.num_copy_buffer_to_buffer - buffersBefore == expectedCopies);
        if (episodeFrame == 1) another.loadData(smallPixels);
        if (vulkan) {
            const int expectedWarnings[] = {2, 3, 3, 3, 5};
            check("per-target warning cadence", warnings == expectedWarnings[episodeFrame]);
            check("recovery cadence", recoveries == (episodeFrame >= 3 ? 2 : 0));
        } else {
            check("other backend has no overflow messages", warnings == 0 && recoveries == 0);
        }
        if (++frame == 6) {
            check("no backend errors", errors == 0);
            logs = {};
        }
    }
};
}
TC_CORE_TEST_MAIN() {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    return tcCoreTest::runDisplayTest<UploadApp>(WindowSettings{}.setSize(64, 64), failures, [] {
        check("ten upload frames completed", completed);
    });
#else
    std::puts("SKIP: vulkanStagingOverflow checks Linux Vulkan and GL");
    return 0;
#endif
}
