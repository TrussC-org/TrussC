#pragma once

#include <box2d/box2d.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace tcx::box2d {
class World;
class CollisionManager;
namespace detail {

// Independent of World storage: an update/notification retains this state, not
// the World. All delivery cancellation and active payloads live here.
struct DispatchState : b2DestructionListener {
    DispatchState();
    ~DispatchState();
    bool alive = true;
    uint64_t epoch = 0;
    uint64_t setupGeneration = 0;
    // Public weak compatibility token expires at logical deletion, even while
    // an active dispatch retains this state until its stack unwinds.
    std::shared_ptr<int> token = std::make_shared<int>(0);
    unsigned depth = 0;
    World* world = nullptr;
    b2World* physics = nullptr;
    b2DestructionListener* delegate = nullptr; // borrowed, preserved by setup
    std::unique_ptr<CollisionManager> manager;

    struct Frame {
        DispatchState& state;
        bool sensor = false; // current internal routing classification
        std::function<void(b2Body*, b2Fixture*)> retire;
        std::function<void()> refresh;
        Frame(DispatchState& s, std::function<void(b2Body*, b2Fixture*)> r, std::function<void()> f)
            : state(s), retire(std::move(r)), refresh(std::move(f)) { state.frames.push_back(this); }
        ~Frame() { std::erase(state.frames, this); }
    };
    std::vector<Frame*> frames;
    void refreshFrames(b2Body* body = nullptr, b2Fixture* fixture = nullptr) {
        for (auto* frame : frames) {
            if (body && frame->retire) frame->retire(body, fixture);
            frame->refresh();
        }
    }
    void SayGoodbye(b2Fixture* fixture) override;
    void SayGoodbye(b2Joint* joint) override;
};
} // namespace detail
} // namespace tcx::box2d
