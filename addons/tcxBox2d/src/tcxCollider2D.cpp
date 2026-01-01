// =============================================================================
// tcxCollider2D.cpp - 2D Collider Component Implementation
// =============================================================================

#include "tcxCollider2D.h"

namespace tcx::box2d {

// =============================================================================
// Collision Filtering
// =============================================================================

void Collider2D::setCategoryBits(uint16_t bits) {
    if (!fixture_) return;
    b2Filter filter = fixture_->GetFilterData();
    filter.categoryBits = bits;
    fixture_->SetFilterData(filter);
}

void Collider2D::setMaskBits(uint16_t bits) {
    if (!fixture_) return;
    b2Filter filter = fixture_->GetFilterData();
    filter.maskBits = bits;
    fixture_->SetFilterData(filter);
}

void Collider2D::setGroupIndex(int16_t index) {
    if (!fixture_) return;
    b2Filter filter = fixture_->GetFilterData();
    filter.groupIndex = index;
    fixture_->SetFilterData(filter);
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
    if (fixture_) {
        fixture_->SetSensor(isTrigger);
    }
}

} // namespace tcx::box2d
