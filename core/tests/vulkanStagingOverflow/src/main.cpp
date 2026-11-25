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
// Untextured points isolate uniform capacity from Vulkan descriptor capacity.
bool smallUniformReserve = false;
struct UniformApp : App {
    Fbo target;
    Mesh point;
    EventListener logs;
    int frame = 0, warnings = 0, recoveries = 0, errors = 0;
    void setup() override {
        sg_enable_stats();
        const int expected = smallUniformReserve ? 4096 : 64 * 1024 * 1024;
        if (sg_query_backend() == SG_BACKEND_VULKAN)
            check("uniform startup reserve", sg_query_desc().uniform_buffer_size == expected);
        target.allocate(8, 8, 1);
        point.setMode(PrimitiveMode::Points);
        point.addVertex(0, 0, 0);
        setPointStyle(PointStyle::Square);
        logs = getLogger().onLog.listen([this, expected](LogEventArgs& e) {
            if (e.message.find("Vulkan per-frame uniform limit hit") != std::string::npos) {
                ++warnings;
                check("uniform warning is actionable", e.level == LogLevel::Warning &&
                    e.message.find("RuntimeSettings::reserveUniformBuffer()") != std::string::npos &&
                    e.message.find(std::to_string(expected) + " bytes") != std::string::npos);
            }
            if (e.message.find("uniform buffer recovered after") != std::string::npos) {
                ++recoveries;
                check("uniform recovery counts frames", e.level == LogLevel::Notice &&
                    e.message.find(recoveries == 1 ? "3 overflowing frames" : "1 overflowing frames") != std::string::npos);
            }
            if (e.level == LogLevel::Error || e.level == LogLevel::Fatal) ++errors;
        });
    }
    void draw() override {
        const bool vkSmall = smallUniformReserve && sg_query_backend() == SG_BACKEND_VULKAN;
        // Three overflowing frames, one clean frame, then another episode.
        const bool heavy = smallUniformReserve ? (frame < 3 || frame == 4) : frame == 0;
        const int expectedWarnings[] = {0, 1, 1, 1, 1, 2, 2};
        const int expectedRecoveries[] = {0, 0, 0, 0, 1, 1, 2};
        check("uniform warning cadence at frame boundary", warnings == (vkSmall ? expectedWarnings[frame] : 0));
        check("uniform recovery cadence at frame boundary", recoveries == (vkSmall ? expectedRecoveries[frame] : 0));
        if (frame == 6) {
            check("uniform overflow replaces raw errors", errors == 0);
            completed = true;
            exitApp(failures ? 1 : 0);
            return;
        }
        target.begin();
        const auto before = sg_query_stats().cur_frame;
        for (int i = 0; i < (heavy ? 20000 : 1); ++i) point.draw();
        target.end();
        const auto after = sg_query_stats().cur_frame;
        if (heavy) {
            check("20k draws submitted", after.num_draw - before.num_draw >= 20000);
            check("one uniform block per draw", after.num_apply_uniforms - before.num_apply_uniforms >= 20000);
        }
        ++frame;
    }
};

}
TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (argc > 1 && (std::string(argv[1]) == "--uniform-default" || std::string(argv[1]) == "--uniform-small")) {
        smallUniformReserve = std::string(argv[1]) == "--uniform-small";
        RuntimeSettings runtime;
        if (smallUniformReserve) runtime.reserveUniformBuffer(4096);
        return tcCoreTest::runDisplayTest<UniformApp>(WindowSettings{}.setSize(64, 64).setSwapInterval(0), failures, [] {
            check("uniform frames completed", completed);
        }, runtime);
    }
    if (argc > 1 && std::string(argv[1]) == "--display") {
        return tcCoreTest::runDisplayTest<UploadApp>(WindowSettings{}.setSize(64, 64), failures, [] {
            check("ten upload frames completed", completed);
        });
    }
    std::puts("SKIP: vulkanStagingOverflow opens a window; run it with --display (display-test)");
    return 0;
#else
    (void)argc;
    (void)argv;
    std::puts("SKIP: vulkanStagingOverflow checks Linux Vulkan and GL");
    return 0;
#endif
}
