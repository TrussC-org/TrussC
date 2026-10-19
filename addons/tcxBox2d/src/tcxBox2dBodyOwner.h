#pragma once

#include <cstdint>

namespace tcx::box2d::detail {

// Internal back-link stored in b2Body user data (reserved by TrussC).
// Each attachment gets a new generation, including reuse of a body address.
struct BodyOwner {
    virtual ~BodyOwner() = default;
    virtual void onWorldReleased() = 0;
    uint64_t generation = 0;
};

} // namespace tcx::box2d::detail
