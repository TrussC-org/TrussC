// Ordered, unlocked contact delivery shared by the classic and Mod APIs.
#pragma once

#include "tcxCollider2D.h"
#include "tcxCollisionEvent.h"
#include "tcxBox2dDispatch.h"
#include <TrussC.h>
#include <box2d/box2d.h>
#include <deque>

namespace tcx::box2d {
class World;

// Borrowed endpoints. Release can null these even in the executing callback;
// recheck after mutation and never retain physics pointers for later use.
struct WorldContact {
    b2Body* a = nullptr;
    b2Body* b = nullptr;
    tc::Vec2 point;
    tc::Vec2 normal;
};

class CollisionManager : public b2ContactListener {
public:
    CollisionManager() = default;
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    // Begin/End follow Box2D order AFTER each managed sub-step, world/Mod then
    // classic A/B for each transition. Stay runs once per update. Raw Step
    // records every transition and delivers at the next update (even dt=0).
    // Managed fixture hand-overs within one sub-step remain one touching pair.
    tc::Event<WorldContact> contactBegan;
    tc::Event<WorldContact> contactStay;
    tc::Event<WorldContact> contactEnded;
    void update();
    void BeginContact(b2Contact* contact) override;
    void EndContact(b2Contact* contact) override;
    void PreSolve(b2Contact*, const b2Manifold*) override {}
    void PostSolve(b2Contact*, const b2ContactImpulse*) override {}

private:
    friend class World;
    friend class Body;
    friend class RigidBody2D;
    friend struct detail::DispatchState;
    struct Endpoint {
        b2Body* body = nullptr; // immutable identity, never dereferenced after retirement
        b2Fixture* fixture = nullptr; // rebindable body witness: compare membership, never dereference
        Collider2D* collider = nullptr;
        uint64_t generation = 0;
        bool live = true;
    };
    struct Pair {
        Endpoint a, b;
        std::vector<b2Contact*> contacts; // only live Box2D contacts, never deferred geometry
        uint64_t endedStep = 0;
        bool beganDelivered = false;
    };
    enum class Phase { Begin, Stay, End };
    struct Record {
        Phase phase;
        uint64_t epoch;
        uint64_t step;
        std::shared_ptr<Pair> pair; // Each Pair object provides identity, even if body addresses are reused.
        WorldContact geometry;
        bool sensor = false;
        uint64_t readyUpdate = 0;
        bool cancelled = false;
    };
    // One journal: active aggregation, ordered records, step bracket and
    // mutation eligibility are bookkeeping for the same transition stream.
    struct Journal {
        std::vector<std::shared_ptr<Pair>> pairs;
        std::deque<Record> records;
        uint64_t stepCounter = 0;
        uint64_t updateCounter = 0;
        uint64_t managedStep = 0;
        unsigned mutationDepth = 0;
        bool deferred = false;
        bool draining = false;
    } journal_;
    std::weak_ptr<detail::DispatchState> state_;

    // Explicit wrappers mark eligible records; raw calls remain update-only.
    struct Mutation {
        std::shared_ptr<detail::DispatchState> state;
        bool previous;
        Mutation(std::shared_ptr<detail::DispatchState> s, bool deferred);
        ~Mutation();
    };
    Mutation mutation(bool deferred = false) { return Mutation(state_.lock(), deferred); }
    void beginStep();
    void endStep();
    void drain();
    void dispatchStay();
    void deliver(const Record& record);
    void clearContacts();
    void forget(b2Body* body);
    void forgetFixture(b2Body* body, b2Fixture* fixture);
    void forgetBody(b2Body* body) { forget(body); }
    void retire(Pair& pair, b2Body* body, b2Fixture* fixture = nullptr);
    static bool rebind(Endpoint& endpoint, b2Body* other, b2Fixture* removed = nullptr);
    bool valid(Endpoint& endpoint, b2Body* other, const detail::DispatchState& state) const;
    Collider2D* collider(Endpoint& endpoint, b2Body* other, const detail::DispatchState& state) const;
    Record snapshot(Phase phase, const std::shared_ptr<Pair>& pair, b2Contact* contact);
    static WorldContact geometry(b2Contact* contact);
    static Endpoint endpoint(b2Fixture* fixture);
    void dropEnded();
};
} // namespace tcx::box2d
