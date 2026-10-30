#include "tcxCollisionManager.h"
#include "tcxBox2dWorld.h"
#include "tcxBox2dBody.h"

namespace tcx::box2d {

CollisionManager::Mutation::Mutation(std::shared_ptr<detail::DispatchState> s, bool deferred)
    : state(std::move(s)), previous(state->manager->journal_.deferred) {
    auto& j = state->manager->journal_;
    ++j.mutationDepth;
    j.deferred = deferred;
}
CollisionManager::Mutation::~Mutation() {
    auto& j = state->manager->journal_;
    --j.mutationDepth;
    j.deferred = previous;
}

CollisionManager::Endpoint CollisionManager::endpoint(b2Fixture* fixture) {
    auto* body = fixture->GetBody();
    auto* owner = reinterpret_cast<detail::BodyOwner*>(body->GetUserData().pointer);
    return {body, fixture, reinterpret_cast<Collider2D*>(fixture->GetUserData().pointer),
            owner ? owner->generation : 0, true};
}
bool CollisionManager::rebind(Endpoint& ep, b2Body* other, b2Fixture* removed) {
    // The caller has established that this body is still alive. Only a live,
    // touching contributor to THIS pair can replace the identity witness.
    // EndContact can run before Box2D unlinks the destroyed contact edge, so
    // also check fixture membership; never dereference the saved witness.
    for (auto* edge = ep.body->GetContactList(); edge; edge = edge->next) {
        if (edge->other != other || !edge->contact->IsTouching()) continue;
        auto* contact = edge->contact;
        auto* candidate = contact->GetFixtureA()->GetBody() == ep.body ? contact->GetFixtureA() : contact->GetFixtureB();
        if (candidate == removed) continue;
        for (auto* f = ep.body->GetFixtureList(); f; f = f->GetNext()) {
            if (f == candidate) { ep.fixture = f; return true; }
        }
    }
    return false;
}
bool CollisionManager::valid(Endpoint& ep, b2Body* other, const detail::DispatchState& state) const {
    if (!ep.live || !state.alive || !state.physics) return false;
    for (auto* body = state.physics->GetBodyList(); body; body = body->GetNext()) {
        if (body != ep.body) continue;
        auto* owner = reinterpret_cast<detail::BodyOwner*>(body->GetUserData().pointer);
        if (ep.generation ? owner && owner->generation == ep.generation : !owner) {
            for (auto* f = body->GetFixtureList(); f; f = f->GetNext()) {
                if (f == ep.fixture) return true;
            }
            // A previously touching witness can be removed after its contact
            // ended, with no new EndContact or SayGoodbye. Preserve the pair
            // while another fixture still contributes a touching contact.
            if (rebind(ep, other)) return true;
            if (owner && !body->GetFixtureList()) {
                body->GetUserData().pointer = 0;
                owner->onWorldReleased();
            }
        }
        break;
    }
    // Missing body/owner or no touching replacement permanently closes the
    // endpoint. An already retired endpoint can never be rebound or revived.
    ep.live = false;
    return false;
}
Collider2D* CollisionManager::collider(Endpoint& ep, b2Body* other, const detail::DispatchState& state) const {
    if (!ep.collider || !valid(ep, other, state)) return nullptr;
    // Fixture metadata is borrowed too. Check membership before exposing it.
    for (auto* f = ep.body->GetFixtureList(); f; f = f->GetNext()) {
        if (f->GetUserData().pointer == reinterpret_cast<uintptr_t>(ep.collider)) return ep.collider;
    }
    return nullptr;
}
WorldContact CollisionManager::geometry(b2Contact* contact) {
    WorldContact c;
    if (contact->GetManifold()->pointCount > 0) {
        b2WorldManifold manifold;
        contact->GetWorldManifold(&manifold);
        c.point = World::toPixels(manifold.points[0]);
        c.normal = tc::Vec2(manifold.normal.x, manifold.normal.y);
    }
    return c;
}
CollisionManager::Record CollisionManager::snapshot(Phase phase, const std::shared_ptr<Pair>& pair, b2Contact* contact) {
    auto state = state_.lock();
    auto c = geometry(contact);
    bool sensor = contact->GetFixtureA()->IsSensor() || contact->GetFixtureB()->IsSensor();
    // Any contributing sensor classifies an aggregated pair as a trigger.
    for (auto* other : pair->contacts) {
        sensor = sensor || other->GetFixtureA()->IsSensor() || other->GetFixtureB()->IsSensor();
    }
    if (contact->GetFixtureA()->GetBody() != pair->a.body) c.normal = -c.normal;
    bool eligible = phase == Phase::Stay || journal_.managedStep || (journal_.mutationDepth && (!journal_.deferred || state->depth));
    return {phase, state->epoch, journal_.managedStep, pair, c, sensor, journal_.updateCounter + (eligible ? 0 : 1), false};
}
void CollisionManager::beginStep() { journal_.managedStep = ++journal_.stepCounter; }
void CollisionManager::endStep() { journal_.managedStep = 0; dropEnded(); }
void CollisionManager::dropEnded() {
    std::erase_if(journal_.pairs, [](const auto& p) { return p->contacts.empty(); });
}
void CollisionManager::BeginContact(b2Contact* contact) {
    auto* a = contact->GetFixtureA()->GetBody();
    auto* b = contact->GetFixtureB()->GetBody();
    for (const auto& pair : journal_.pairs) {
        if (!((pair->a.body == a && pair->b.body == b) || (pair->a.body == b && pair->b.body == a))) continue;
        if (!pair->a.live || !pair->b.live) continue;
        if (pair->contacts.empty()) {
            if (!journal_.managedStep || pair->endedStep != journal_.managedStep) continue;
            // Suppression is limited to this explicit managed Step bracket.
            for (auto& record : journal_.records) {
                if (record.pair == pair && record.phase == Phase::End && record.step == journal_.managedStep)
                    record.cancelled = true;
            }
        }
        pair->contacts.push_back(contact);
        // Only this managed sub-step may aggregate a queued Begin's sensors.
        // Earlier raw-Step snapshots must remain exactly as recorded.
        if (journal_.managedStep && (contact->GetFixtureA()->IsSensor() || contact->GetFixtureB()->IsSensor())) {
            for (auto& record : journal_.records) {
                if (record.pair == pair && record.phase == Phase::Begin && record.step == journal_.managedStep)
                    record.sensor = true;
            }
        }
        return;
    }
    auto pair = std::make_shared<Pair>();
    pair->a = endpoint(contact->GetFixtureA());
    pair->b = endpoint(contact->GetFixtureB());
    pair->contacts.push_back(contact);
    journal_.pairs.push_back(pair);
    journal_.records.push_back(snapshot(Phase::Begin, pair, contact));
}
void CollisionManager::EndContact(b2Contact* contact) {
    for (const auto& pair : journal_.pairs) {
        auto it = std::find(pair->contacts.begin(), pair->contacts.end(), contact);
        if (it == pair->contacts.end()) continue;
        // Capture last-contact classification before removing its only fixture.
        auto record = snapshot(Phase::End, pair, contact);
        pair->contacts.erase(it);
        if (pair->contacts.empty()) {
            pair->endedStep = journal_.managedStep;
            journal_.records.push_back(std::move(record));
        }
        break;
    }
    // Box2D 2.4 unlinks a directly destroyed fixture BEFORE calling EndContact,
    // while fixture/body storage is still live. Unlike SayGoodbye, this observes
    // DestroyFixture, even for the last fixture of an ownerless body. Rebind or
    // retire now, before a fixtureless DestroyBody can silently recycle storage.
    for (auto* fixture : {contact->GetFixtureA(), contact->GetFixtureB()}) {
        auto* body = fixture->GetBody();
        bool attached = false;
        for (auto* f = body->GetFixtureList(); f; f = f->GetNext()) attached |= f == fixture;
        if (!attached) forgetFixture(body, fixture);
    }
    if (!journal_.managedStep) dropEnded();
}
void CollisionManager::retire(Pair& pair, b2Body* body, b2Fixture* fixture) {
    for (auto* ep : {&pair.a, &pair.b}) {
        if (ep->body != body || !ep->live) continue;
        if (!fixture) {
            ep->live = false;
        } else if (ep->fixture == fixture) {
            // Rebind queued records and executing frames to a remaining
            // touching contributor, never an arbitrary non-touching fixture.
            ep->live = rebind(*ep, ep == &pair.a ? pair.b.body : pair.a.body, fixture);
        }
    }
}
void CollisionManager::forgetFixture(b2Body* body, b2Fixture* fixture) {
    if (!body->GetFixtureList()) {
        // A later DestroyBody has no fixture callback. Detach the wrapper now
        // as well, so its destructor cannot destroy a recycled raw body.
        auto* owner = reinterpret_cast<detail::BodyOwner*>(body->GetUserData().pointer);
        body->GetUserData().pointer = 0;
        if (owner) owner->onWorldReleased();
        forget(body);
        return;
    }
    for (auto& pair : journal_.pairs) retire(*pair, body, fixture);
    for (auto& record : journal_.records) retire(*record.pair, body, fixture);
    if (auto state = state_.lock()) state->refreshFrames(body, fixture);
}
void CollisionManager::forget(b2Body* body) {
    if (!body) return;
    for (auto& pair : journal_.pairs) retire(*pair, body);
    for (auto& record : journal_.records) retire(*record.pair, body);
    if (auto state = state_.lock()) state->refreshFrames(body);
}
void CollisionManager::clearContacts() {
    auto state = state_.lock();
    ++state->epoch;
    journal_.records.clear();
    journal_.pairs.clear();
    state->refreshFrames();
}

void CollisionManager::deliver(const Record& record) {
    auto state = state_.lock();
    if (!state || !state->alive || record.epoch != state->epoch || record.cancelled) return;
    WorldContact worldPayload = record.geometry;
    CollisionEvent classicPayload;
    int side = -1;
    auto prepare = [&] {
        bool a = valid(record.pair->a, record.pair->b.body, *state), b = valid(record.pair->b, record.pair->a.body, *state);
        bool open = state->alive && record.epoch == state->epoch;
        if (record.phase != Phase::End) open = open && a && b;
        if (record.phase == Phase::Stay) open = open && !record.pair->contacts.empty();
        worldPayload.a = open && a ? record.pair->a.body : nullptr;
        worldPayload.b = open && b ? record.pair->b.body : nullptr;
        auto* ca = open ? collider(record.pair->a, record.pair->b.body, *state) : nullptr;
        auto* cb = open ? collider(record.pair->b, record.pair->a.body, *state) : nullptr;
        classicPayload.other = side == 0 ? (cb ? cb->getBody() : nullptr) : (ca ? ca->getBody() : nullptr);
        if (!open || (side == 0 && !ca) || (side == 1 && !cb)) classicPayload.other = nullptr;
        return open && (side < 0 ? a || b : side == 0 ? ca != nullptr : cb != nullptr);
    };
    detail::DispatchState::Frame frame(*state,
        [&](b2Body* body, b2Fixture* fixture) { retire(*record.pair, body, fixture); }, [&] { prepare(); });
    frame.sensor = record.sensor;
    struct Depth {
        detail::DispatchState& state;
        Depth(detail::DispatchState& s) : state(s) { ++state.depth; }
        ~Depth() { --state.depth; }
    } depth(*state);
    if (record.phase == Phase::Begin && prepare()) record.pair->beganDelivered = true;
    auto& event = record.phase == Phase::Begin ? contactBegan : record.phase == Phase::Stay ? contactStay : contactEnded;
    tc::internal::GuardedEventDelivery<WorldContact>::notify(event, worldPayload, prepare);
    // Both classic colliders must have existed for classic pair notifications.
    if (!record.pair->a.collider || !record.pair->b.collider) return;
    for (side = 0; side < 2; ++side) {
        if (!prepare()) continue;
        auto* target = collider(side == 0 ? record.pair->a : record.pair->b,
            side == 0 ? record.pair->b.body : record.pair->a.body, *state);
        classicPayload.impulse = 0.0f;
        classicPayload.contactPoint = record.geometry.point;
        classicPayload.normal = side == 0 ? record.geometry.normal : -record.geometry.normal;
        auto& targetEvent = record.phase == Phase::Begin ? target->onCollisionEnter :
            record.phase == Phase::Stay ? target->onCollisionStay : target->onCollisionExit;
        tc::internal::GuardedEventDelivery<CollisionEvent>::notify(targetEvent, classicPayload, prepare);
    }
}
void CollisionManager::drain() {
    auto state = state_.lock(); // retains this manager through World deletion
    if (!state || journal_.draining || state->depth) return;
    // The wrapper's Box2D operation has returned. Its RAII storage may still
    // retain the dispatcher, but raw calls in listeners must not inherit its
    // immediate eligibility. Nested wrappers open their own mutation scope.
    struct DeliveryScope {
        Journal& journal;
        unsigned mutationDepth;
        explicit DeliveryScope(Journal& target)
            : journal(target), mutationDepth(target.mutationDepth) {
            journal.mutationDepth = 0;
            journal.draining = true;
        }
        ~DeliveryScope() {
            journal.mutationDepth = mutationDepth;
            journal.draining = false;
        }
    } deliveryScope(journal_);
    const auto epoch = state->epoch;
    while (!journal_.records.empty() && state->alive && state->epoch == epoch) {
        if (journal_.records.front().readyUpdate > journal_.updateCounter) break;
        Record record = std::move(journal_.records.front());
        journal_.records.pop_front();
        deliver(record);
    }
}
void CollisionManager::update() {
    auto state = state_.lock();
    if (!state || !state->alive || state->depth) return;
    ++journal_.updateCounter;
    drain();
    dispatchStay();
}
void CollisionManager::dispatchStay() {
    auto state = state_.lock();
    if (!state || !state->alive || state->depth) return;
    // Snapshot only live touching pairs; revalidate both the epoch and contact
    // state before each Stay, since a previous callback can disable another pair.
    std::vector<Record> stays;
    for (const auto& pair : journal_.pairs) {
        if (!pair->beganDelivered) continue;
        for (auto* contact : pair->contacts) {
            if (contact->IsTouching()) {
                stays.push_back(snapshot(Phase::Stay, pair, contact));
                break;
            }
        }
    }
    for (const auto& record : stays) {
        if (!record.pair->contacts.empty()) deliver(record);
        drain(); // callback-generated Exits, after the callback/Mod teardown
    }
}
} // namespace tcx::box2d
