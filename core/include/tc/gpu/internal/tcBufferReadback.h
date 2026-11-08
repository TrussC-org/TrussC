#pragma once
#include "../tcReadbackResult.h"
#include <deque>
#include <functional>
#include <memory>

namespace trussc::internal {
struct AppOwner;
// Backend requests own their staging resources; no StorageBuffer or App pointer.
struct BufferReadbackRequest {
    virtual ~BufferReadbackRequest() = default;
    // True means finished (including failure). Never wait for GPU completion.
    virtual bool poll() { return true; }
    ReadbackResult result;
    std::function<void(ReadbackResult)> callback;
    std::weak_ptr<AppOwner> owner;
    std::function<void()> warn;
};

class BufferReadbackQueue {
public:
    bool accepting() const { return accepting_; }
    void start() { accepting_ = true; }
    void enqueue(std::unique_ptr<BufferReadbackRequest> request) {
        if (accepting_) pending_.push_back(std::move(request));
    }
    void cancelAppCallbacks(const std::shared_ptr<AppOwner>& owner) {
        // Keep backend requests until polling retires their native resources.
        for (auto& request : pending_) {
            if (request->owner.lock() == owner) request->callback = {};
        }
    }
    void poll() {
        // Requests made by a callback wait for the next update too.
        size_t remaining = pending_.size();
        while (remaining-- && !pending_.empty()) {
            if (!pending_.front()->poll()) break;
            auto request = std::move(pending_.front());
            pending_.pop_front();
            if (!request->result.ok() && request->warn) request->warn();
            auto callback = std::move(request->callback);
            auto result = std::move(request->result);
            request.reset(); // Retire native resources before entering user code.
            if (callback) callback(std::move(result));
        }
    }
    void shutdown() noexcept {
        accepting_ = false;
        pending_.clear(); // Drop callbacks while the GPU context still exists.
    }
private:
    bool accepting_ = true;
    std::deque<std::unique_ptr<BufferReadbackRequest>> pending_;
};

BufferReadbackQueue& bufferReadbacks();
} // namespace trussc::internal
