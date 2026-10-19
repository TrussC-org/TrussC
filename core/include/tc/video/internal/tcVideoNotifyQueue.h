#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

namespace trussc::internal {

// Platform-neutral MF notification transport. Only the main thread interprets
// events; close and enqueue share a lock so no event can survive close.
struct VideoNotifyEvent {
    uint32_t event;
    uintptr_t param1;
    uint32_t param2;
};

class VideoNotifyQueue {
public:
    void push(VideoNotifyEvent event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closed_) events_.push_back(event);
    }

    std::deque<VideoNotifyEvent> take() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<VideoNotifyEvent> result;
        result.swap(events_);
        return result;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        events_.clear();
    }

private:
    std::mutex mutex_;
    bool closed_ = false;
    std::deque<VideoNotifyEvent> events_;
};

template<class State>
void enqueueVideoNotify(const std::weak_ptr<State>& target, VideoNotifyEvent event) {
    if (auto state = target.lock()) state->notifications.push(event);
}

} // namespace trussc::internal
