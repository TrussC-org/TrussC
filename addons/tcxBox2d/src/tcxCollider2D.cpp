// =============================================================================
// tcxCollider2D.cpp - 2D Collider Component Implementation
// =============================================================================

#include "tcxCollider2D.h"
#include "tcxBox2dBody.h"

namespace tcx::box2d {

// =============================================================================
// Collision Filtering
// =============================================================================

// Setters change every fixture of the body (a compound body has several);
// getters read the first one.
template<typename F>
static void forEachFixture(b2Fixture* first, F&& f) {
    if (!first) return;
    for (b2Fixture* fx = first->GetBody()->GetFixtureList(); fx; fx = fx->GetNext()) f(fx);
}

void Collider2D::setCategoryBits(uint16_t bits) {
    forEachFixture(fixture_, [bits](b2Fixture* f) {
        b2Filter filter = f->GetFilterData();
        filter.categoryBits = bits;
        f->SetFilterData(filter);
    });
}

void Collider2D::setMaskBits(uint16_t bits) {
    forEachFixture(fixture_, [bits](b2Fixture* f) {
        b2Filter filter = f->GetFilterData();
        filter.maskBits = bits;
        f->SetFilterData(filter);
    });
}

void Collider2D::setGroupIndex(int16_t index) {
    forEachFixture(fixture_, [index](b2Fixture* f) {
        b2Filter filter = f->GetFilterData();
        filter.groupIndex = index;
        f->SetFilterData(filter);
    });
}

uint16_t Collider2D::getCategoryBits() const {
    if (!fixture_) return 0x0001;
    return fixture_->GetFilterData().categoryBits;
}

uint16_t Collider2D::getMaskBits() const {
    if (!fixture_) return 0xFFFF;
    return fixture_->GetFilterData().maskBits;
}

int16_t Collider2D::getGroupIndex() const {
    if (!fixture_) return 0;
    return fixture_->GetFilterData().groupIndex;
}

// =============================================================================
// Trigger Mode
// =============================================================================

void Collider2D::applyTriggerMode() {
    bool on = isTrigger;
    appliedTrigger_ = on;
    // fixture_ is borrowed and raw DestroyFixture may already have recycled it.
    // Resolve sensors through the owner, including changed-flag synchronization.
    if (auto* body = body_ ? body_->getBody() : nullptr) {
        for (auto* f = body->GetFixtureList(); f; f = f->GetNext()) f->SetSensor(on);
    }
}

} // namespace tcx::box2d
