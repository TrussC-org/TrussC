// Standalone diagnostic for the GCC 15 fixture-proxy failure (#1427).
#include <box2d/box2d.h>
#include <cstdio>
#include <limits>

static bool expectedBounds(const b2AABB& aabb) {
    return aabb.lowerBound.x == 2 && aabb.upperBound.x == 4 &&
           aabb.lowerBound.y == -1 && aabb.upperBound.y == 1;
}

int main() {
    b2Transform identity;
    identity.SetIdentity();
    b2CircleShape shape;
    shape.m_radius = 1;
    shape.m_p.Set(3, 0);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    b2AABB direct{{nan, nan}, {nan, nan}};
    shape.ComputeAABB(&direct, identity, 0);

    b2World world({0, 0});
    b2BodyDef bodyDef;
    auto* compound = world.CreateBody(&bodyDef);
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &shape;
    // Match the compound fixture order in the round-4 standalone probe.
    shape.m_p.Set(0, 0);
    compound->CreateFixture(&fixtureDef);
    shape.m_p.Set(3, 0);
    fixtureDef.isSensor = true;
    auto* sensor = compound->CreateFixture(&fixtureDef);
    const b2AABB proxy = sensor->GetAABB(0); // Before Step or source-shape death.

    bodyDef.type = b2_dynamicBody;
    bodyDef.position.Set(3.5f, 0);
    auto* otherBody = world.CreateBody(&bodyDef);
    b2CircleShape otherShape;
    otherShape.m_radius = 0.5f;
    auto* other = otherBody->CreateFixture(&otherShape, 1);
    // A zero-time step creates broadphase candidate contacts without motion.
    world.Step(0, 0, 0);
    bool paired = false;
    for (auto* contact = world.GetContactList(); contact; contact = contact->GetNext()) {
        auto* a = contact->GetFixtureA();
        auto* b = contact->GetFixtureB();
        paired |= (a == sensor && b == other) || (a == other && b == sensor);
    }

    const bool ok = expectedBounds(direct) && expectedBounds(proxy) && paired;
    std::printf("box2d-proxy %s direct=[%.9g,%.9g]x[%.9g,%.9g] "
                "proxy=[%.9g,%.9g]x[%.9g,%.9g] pair=%s result=%s\n",
        BOX2D_PROXY_VARIANT,
        direct.lowerBound.x, direct.upperBound.x, direct.lowerBound.y, direct.upperBound.y,
        proxy.lowerBound.x, proxy.upperBound.x, proxy.lowerBound.y, proxy.upperBound.y,
        paired ? "PASS" : "FAIL", ok ? "PASS" : "FAIL");
    return 0; // Diagnostic only: a bad result must not fail the CI job.
}
