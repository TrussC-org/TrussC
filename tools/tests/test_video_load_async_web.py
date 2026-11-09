"""Exercise the production Web loadAsync and slot without a browser/SDK."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VideoLoadAsyncWebTests(unittest.TestCase):
    def test_application_does_not_yield(self):
        video = (ROOT / 'core/include/tc/video/tcVideoPlayer.h').read_text(encoding='utf-8')
        resource = (ROOT / 'core/include/tc/utils/tcResourceLoad.h').read_text(encoding='utf-8')
        state = video.split('    struct AsyncState :', 1)[1].split('\npublic:', 1)[0]
        method = video.split('    void loadAsync(', 1)[1].split('\n    Event<LoadResult> onLoad;', 1)[0]
        slot = resource.split('template<class State> class ResourceLoadSlot', 1)[1].split(
            '\n} // namespace', 1)[0]
        # Only the host/Event plumbing and decoder are stand-ins. Compile the
        # actual Web state, entry/application body, and shared slot lifecycle.
        prefix = r'''
#include <atomic>
#include <cassert>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "tc/utils/tcLoadResult.h"
#undef __APPLE__
#define __EMSCRIPTEN__
using trussc::LoadResult;
namespace fs = std::filesystem;
template<class T> struct Event { std::function<void(T&)> callback; };
namespace internal {
struct NetworkDelivery { bool open = true; void close() { open = false; } };
template<class T> struct EventSnapshotDelivery {
    using Target = std::function<void(T&)>;
    std::weak_ptr<Target> target;
    static auto bind(Event<T>& event, std::shared_ptr<NetworkDelivery> gate) {
        return std::make_shared<Target>([&event, gate](T& result) {
            if (gate->open && event.callback) event.callback(result);
        });
    }
};
auto appRunToken() { static auto run = std::make_shared<std::atomic<bool>>(true); return run; }
bool appRunIsAlive(const std::weak_ptr<std::atomic<bool>>& run) {
    auto live = run.lock(); return live && live->load();
}
auto currentAppOwner() { return std::shared_ptr<int>{}; }
auto registerResourceStop(std::function<void()> stop) {
    return std::make_shared<std::function<void()>>(std::move(stop));
}
struct ResourceLoadState {
    using Target = std::function<void(ResourceLoadState&)>;
    std::atomic<bool> cancelled{false};
    std::weak_ptr<std::atomic<bool>> run;
    std::weak_ptr<int> appOwner;
    std::weak_ptr<Target> apply;
    EventSnapshotDelivery<LoadResult> event;
    std::shared_ptr<std::function<void()>> stop;
    LoadResult result;
    bool localFile = false;
    virtual ~ResourceLoadState() = default;
    virtual size_t retainedBytes() const noexcept { return 0; }
    virtual void discardPayload() noexcept {}
    void cancel() { cancelled = true; }
};
std::vector<std::shared_ptr<ResourceLoadState>> mailbox;
void completeResourceLoad(std::shared_ptr<ResourceLoadState> state) {
    if (!state->cancelled) mailbox.push_back(state);
}
bool admitExternalResourceLoad(std::shared_ptr<ResourceLoadState>) { return true; }
auto pathToDisplayUtf8(const fs::path& path) { return path.string(); }
auto resolveLoadPath(const fs::path& path, bool) { return path; }
bool resourceLoadIsLocalPath(const fs::path&, const fs::path&) { return true; }
void clearLoadName(fs::path& path) { path.clear(); }
bool applying = false;
void deliver() {
    auto batch = std::exchange(mailbox, {});
    for (auto& state : batch) if (!state->cancelled) {
        if (auto target = state->apply.lock()) {
            applying = true; (*target)(*state); applying = false;
        }
    }
}
template<class State> class ResourceLoadSlot
'''
        fake_player = r'''
}
std::function<void()> input;
int decoderCalls = 0;
class VideoPlayer {
    struct AsyncState :
'''
        support = r'''
    fs::path sourcePath_;
    unsigned char* pixels_ = nullptr;
    int width_ = 0, height_ = 0;
    float volume_ = 1, speed_ = 1;
    bool loop_ = false, autoPoster_ = true, audioStreaming_ = true;
    LoadResult loadInternal(const fs::path& path) {
        // Model an Asyncify yield. No decoder may be entered by application.
        assert(!internal::applying);
        ++decoderCalls;
        if (auto callback = std::exchange(input, {})) callback();
        sourcePath_ = path; width_ = height_ = 1;
        return LoadResult::success();
    }
    void closeInternal(bool) { sourcePath_.clear(); }
    void moveFrom(VideoPlayer&& other) { sourcePath_ = std::move(other.sourcePath_); }
    void applyCachedStateToPlatform() {}
public:
    ~VideoPlayer() { async_.cancel(); }
    void close() { async_.cancel(); closeInternal(true); }
    Event<LoadResult> onLoad;
    bool isLoading() const { return async_.loading(); }
    const fs::path& getPath() const { return sourcePath_; }
    void loadAsync(
'''
        main = r'''
};
int main() {
    for (int action = 0; action < 4; ++action) {
        int events = 0;
        decoderCalls = 0;
        auto player = std::make_unique<VideoPlayer>();
        player->onLoad.callback = [&](LoadResult& result) {
            assert(result && player && !player->isLoading()); ++events;
        };
        input = [&] {
            assert(events == 0);
            if (action == 1) player->close();
            if (action == 2) player.reset();
            if (action == 3) player->loadAsync("replacement.mp4");
        };
        player->loadAsync("initial.mp4");
        assert(events == 0);
        assert(decoderCalls == (action == 3 ? 2 : 1));
        internal::deliver(); internal::deliver();
        assert(events == (action == 0 || action == 3 ? 1 : 0));
        if (action == 0) assert(player->getPath() == "initial.mp4");
        if (action == 3) assert(player->getPath() == "replacement.mp4");
    }
}
'''
        source_text = prefix + slot + fake_player + state + support + method + main
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'probe.cpp'
            binary = Path(scratch) / 'probe'
            source.write_text(source_text, encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-g',
                            '-fsanitize=address', '-fno-omit-frame-pointer',
                            '-I' + str(ROOT / 'core/include'), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
