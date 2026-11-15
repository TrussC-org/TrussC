#include <tcxBox2d.h>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace {
int failures = 0;
int checks = 0;
void check(const char* label, bool passed) {
    ++checks;
    if (!passed) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
bool nearlyEqual(float a, float b) { return std::abs(a - b) < 1.0e-6f; }
b2BodyId fallingBody(b2WorldId world) {
    auto def = b2DefaultBodyDef();
    def.type = b2_dynamicBody;
    def.position = {0.0f, 1.0f};
    auto body = b2CreateBody(world, &def);
    auto shapeDef = b2DefaultShapeDef();
    b2Circle circle = {{0.0f, 0.0f}, 0.5f};
    b2CreateCircleShape(body, &shapeDef, &circle);
    return body;
}
void testSubSteps(int count, bool useDefault = false) {
    tcx::box2d::World world;
    if (!useDefault) world.setSubSteps(count);
    world.setup();
    auto body = fallingBody(world.id());
    auto def = b2DefaultWorldDef();
    def.workerCount = 1;
    auto reference = b2CreateWorld(&def);
    auto expectedBody = fallingBody(reference);
    auto alternate = b2CreateWorld(&def);
    auto alternateBody = fallingBody(alternate);
    for (int i = 0; i < 10; ++i) {
        world.update();
        b2World_Step(reference, 1.0f / 60.0f, count);
        b2World_Step(alternate, 1.0f / 60.0f, count == 1 ? 4 : 1);
    }
    check("managed fixed step matches raw world with requested subSteps",
          nearlyEqual(b2Body_GetPosition(body).y, b2Body_GetPosition(expectedBody).y));
    check("subSteps affect the observed trajectory",
          !nearlyEqual(b2Body_GetPosition(body).y, b2Body_GetPosition(alternateBody).y));
    // Changes at runtime are used on the next managed step.
    world.setSubSteps(2);
    world.setSubSteps(0);
    world.setSubSteps(-1);
    world.update();
    b2World_Step(reference, 1.0f / 60.0f, 2);
    check("runtime subSteps and ignored invalid counts",
          nearlyEqual(b2Body_GetPosition(body).y, b2Body_GetPosition(expectedBody).y));
    b2DestroyWorld(reference);
    b2DestroyWorld(alternate);
}
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<tcx::box2d::World>);
    static_assert(!std::is_move_constructible_v<tcx::box2d::World>);
    b2WorldId retired = b2_nullWorldId;
    b2BodyId retiredBody = b2_nullBodyId;
    {
        tcx::box2d::World world;
        check("id invalid before setup", !b2World_IsValid(world.id()));
        world.update();
        world.setGravity(tc::Vec2(60.0f, 900.0f));
        world.setup();
        check("id valid after setup", b2World_IsValid(world.id()));
        auto gravity = b2World_GetGravity(world.id());
        check("pre-setup gravity uses 30 px/m", nearlyEqual(gravity.x, 2) && nearlyEqual(gravity.y, 30));
        world.setGravity(tc::Vec2(-30.0f, 300.0f));
        gravity = b2World_GetGravity(world.id());
        check("runtime gravity uses 30 px/m", nearlyEqual(gravity.x, -1) && nearlyEqual(gravity.y, 10));
        retired = world.id();
        retiredBody = fallingBody(retired);
        world.update();
        check("world steps a raw body", b2Body_GetPosition(retiredBody).y > 1.0f);
        world.setup();
        check("setup replaces and destroys previous world and bodies",
              !b2World_IsValid(retired) && !b2Body_IsValid(retiredBody) && b2World_IsValid(world.id()));
        retired = world.id();
        retiredBody = fallingBody(retired);
    }
    check("RAII destroys world and its raw bodies", !b2World_IsValid(retired) && !b2Body_IsValid(retiredBody));
    {
        tcx::box2d::World world;
        world.setup();
        check("default worker count is one", b2World_GetWorkerCount(world.id()) == 1);
        world.setWorkerCount(2);
        check("worker count changes at runtime", b2World_GetWorkerCount(world.id()) == 2);
        world.update();
        world.setWorkerCount(1);
        auto defaults = b2DefaultWorldDef();
        auto gravity = b2World_GetGravity(world.id());
        check("default gravity is upstream gravity", nearlyEqual(gravity.y, defaults.gravity.y));
        auto body = fallingBody(world.id());
        auto shapeDef = b2DefaultShapeDef();
        check("v3 shape defaults preserved", nearlyEqual(shapeDef.density, 1.0f) && nearlyEqual(shapeDef.material.friction, 0.6f));
        check("default shape has nonzero mass", b2Body_GetMass(body) > 0.0f);
    }
    {
        tcx::box2d::World world;
        world.setWorkerCount(2);
        world.setup();
        check("worker count can be set before setup", b2World_GetWorkerCount(world.id()) == 2);
        world.setWorkerCount(0);
        check("worker count uses upstream clamping", b2World_GetWorkerCount(world.id()) == 1);
    }
    testSubSteps(1);
    testSubSteps(4, true);
    std::printf("tcxBox2d3: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
