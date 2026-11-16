#include <tcxBox2d.h>
#include <cmath>
#include "../../../../core/tests/common/tcWindowTest.h"
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
    def.gravity = {0.0f, 10.0f};   // the World default: y-down pixels, so +y meters
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
using tcx::box2d::RigidBody2D;
using tcx::box2d::World;
std::shared_ptr<tc::Node> liveNode() {
    auto node = std::make_shared<tc::Node>();
    tc::internal::setupNodeOnce(*node);
    return node;
}
bool approxEq(float a, float b) { return std::abs(a - b) < 0.002f; }
tc::Path outline(std::initializer_list<tc::Vec2> points) {
    tc::Path path{std::vector<tc::Vec2>(points)};
    path.close();
    return path;
}
void testLifetime() {
    static_assert(!std::is_copy_constructible_v<RigidBody2D>);
    static_assert(!std::is_move_constructible_v<RigidBody2D>);
    auto node = liveNode();
    auto world = std::make_unique<World>();
    world->setup();
    auto* rb = node->addMod<RigidBody2D>(*world, b2_dynamicBody);
    auto body = rb->id();
    auto shape = rb->addBox(60, 30);
    node->removeMod<RigidBody2D>();
    check("Mod first destroys body and shapes", !b2Body_IsValid(body) && !b2Shape_IsValid(shape));
    rb = node->addMod<RigidBody2D>(*world, b2_dynamicBody);
    body = rb->id();
    world.reset();
    check("World first invalidates Mod", !b2Body_IsValid(rb->id()) && !b2Body_IsValid(body));
    rb->setPosition({3, 4}); rb->setAngle(1); rb->setVelocity({3, 4}); rb->setAngularVelocity(1);
    check("dead-world getters and shapes are safe", rb->getAngle() == 0 && rb->getAngularVelocity() == 0 &&
          rb->getPosition().x == 0 && rb->getVelocity().y == 0 && !b2Shape_IsValid(rb->addCircle(30)) &&
          !b2Shape_IsValid(rb->addBox(30, 30)) && rb->addPolygon(outline({{0,0},{30,0},{0,30}})).empty());
    node->removeMod<RigidBody2D>();
    world = std::make_unique<World>(); world->setup();
    rb = node->addMod<RigidBody2D>(*world, b2_dynamicBody);
    world->setup();
    auto replacement = liveNode();
    auto* replacementRb = replacement->addMod<RigidBody2D>(*world, b2_dynamicBody);
    check("setup twice leaves old Mod invalid with a new body present", !b2Body_IsValid(rb->id()));
    node.reset();
    check("old Mod cannot destroy replacement body's id", b2Body_IsValid(replacementRb->id()));
    body = replacementRb->id(); replacement.reset();
    check("Node destruction owns body", !b2Body_IsValid(body));
    auto delayed = std::make_shared<tc::Node>();
    delayed->addMod<RigidBody2D>(*world, b2_dynamicBody);
    world.reset();
    tc::internal::setupNodeOnce(*delayed);
    check("World gone before delayed setup", !b2Body_IsValid(delayed->getMod<RigidBody2D>()->id()));
    World beforeSetup;
    auto pending = std::make_shared<tc::Node>();
    auto* pendingRb = pending->addMod<RigidBody2D>(beforeSetup, b2_dynamicBody);
    beforeSetup.setup(); tc::internal::setupNodeOnce(*pending);
    check("Mod deferred setup uses the live world", b2Body_IsValid(pendingRb->id()));
}
void testShapes() {
    World world; world.setup();
    auto node = liveNode();
    auto* rb = node->addMod<RigidBody2D>(world, b2_dynamicBody);
    auto def = b2DefaultShapeDef(); def.density = 2; def.material.friction = 0.3f;
    auto box = rb->addBox(60, 30, def);
    check("box valid and centered", b2Shape_IsValid(box) && approxEq(b2Shape_GetPolygon(box).centroid.x, 0));
    check("box mass from shape definition", approxEq(b2Body_GetMass(rb->id()), 4));
    auto circle = rb->addCircle(30);
    check("circle valid with pixel radius", b2Shape_IsValid(circle) && approxEq(b2Shape_GetCircle(circle).radius, 1));
    check("shape ids list", rb->shapeIds().size() == 2 && B2_ID_EQUALS(rb->shapeIds()[0], box));
    check("combined shape mass", approxEq(b2Body_GetMass(rb->id()), 4 + tc::TAU / 2));
    check("v3 material defaults preserved", approxEq(b2Shape_GetFriction(circle), 0.6f) && approxEq(b2Shape_GetFriction(box), 0.3f));
    node->removeMod<RigidBody2D>(); rb = node->addMod<RigidBody2D>(world, b2_dynamicBody);
    auto convex = rb->addPolygon(outline({{-30,-30},{30,-30},{30,30},{-30,30},{-30,-30}}));
    check("convex polygon uses one shape", convex.size() == 1 && b2Shape_IsValid(convex[0]));
    check("convex mass and inertia", approxEq(b2Body_GetMass(rb->id()), 4) && approxEq(b2Body_GetRotationalInertia(rb->id()), 8.0f / 3));
    node->removeMod<RigidBody2D>(); rb = node->addMod<RigidBody2D>(world, b2_dynamicBody);
    auto concave = rb->addPolygon(outline({{0,0},{60,0},{60,30},{30,30},{30,60},{0,60}}), def);
    check("concave triangulated with exact mass", concave.size() > 1 && approxEq(b2Body_GetMass(rb->id()), 6));
    for (auto shape : concave) check("triangle id valid with requested density", b2Shape_IsValid(shape) && approxEq(b2Shape_GetDensity(shape), 2));
    node->removeMod<RigidBody2D>(); rb = node->addMod<RigidBody2D>(world, b2_dynamicBody);
    tc::Path many;
    for (int i = 0; i < 12; ++i) many.lineTo(std::cos(i * tc::TAU / 12) * 30, std::sin(i * tc::TAU / 12) * 30);
    many.close(); auto large = rb->addPolygon(many);
    check("more than eight vertices triangulate with correct mass", large.size() > 1 && approxEq(b2Body_GetMass(rb->id()), 3));
    node->removeMod<RigidBody2D>(); rb = node->addMod<RigidBody2D>(world, b2_dynamicBody);
    auto hole = outline({{-60,-60},{60,-60},{60,60},{-60,60}});
    hole.moveTo(-30,-30); hole.lineTo(-30,30); hole.lineTo(30,30); hole.lineTo(30,-30); hole.close();
    auto holes = rb->addPolygon(hole);
    check("hole triangulates and subtracts mass", holes.size() > 1 && approxEq(b2Body_GetMass(rb->id()), 12));
    int warnings = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) { if (event.level == tc::LogLevel::Warning) ++warnings; });
    const auto count = rb->shapeIds().size();
    for (int i = 0; i < 2; ++i) {
        check("degenerate rejected", rb->addPolygon(outline({{0,0},{30,0},{60,0}})).empty());
        check("empty rejected", rb->addPolygon(tc::Path{}).empty());
        check("tiny rejected", rb->addPolygon(outline({{0,0},{0.001f,0},{0,0.001f}})).empty());
    }
    check("warning every degenerate call", warnings == 6 && rb->shapeIds().size() == count);
    check("bad box/circle dimensions rejected", !b2Shape_IsValid(rb->addBox(0, 30)) && !b2Shape_IsValid(rb->addCircle(-1)));
}
void testPixelAndSync() {
    World world; world.setGravity({0,0}); world.setup();
    auto parent = liveNode(); parent->setPos(100, 200); parent->setRot(0.4f); parent->setScale(2);
    auto node = std::make_shared<tc::Node>(); node->setPos(30, 60, 7); node->setRot(0.2f); parent->addChild(node);
    auto expected = node->getGlobalPos();
    auto* rb = node->addMod<RigidBody2D>(world, b2_dynamicBody); rb->addBox(30,30);
    check("nested initial world position and rotation", approxEq(rb->getPosition().x, expected.x) &&
          approxEq(rb->getPosition().y, expected.y) && approxEq(rb->getAngle(), 0.6f));
    rb->setPosition({210,330}); rb->setAngle(0.9f);
    check("pixel position and radian angle roundtrip", approxEq(rb->getPosition().x,210) && approxEq(rb->getPosition().y,330) && approxEq(rb->getAngle(),0.9f));
    check("setter sync respects nested parent and preserves z", approxEq(node->getGlobalPos().x,210) && approxEq(node->getRot(),0.5f) && node->getZ() == 7);
    rb->setVelocity({60,-90}); rb->setAngularVelocity(1.2f);
    check("velocity roundtrip", approxEq(rb->getVelocity().x,60) && approxEq(rb->getVelocity().y,-90) && approxEq(rb->getAngularVelocity(),1.2f));
    check("raw velocity in meters", approxEq(b2Body_GetLinearVelocity(rb->id()).x,2));
    node->setPos(-500,-500);
    world.update(); expected = node->getGlobalPos();
    check("move event sync nested node", approxEq(expected.x,rb->getPosition().x) && approxEq(expected.y,rb->getPosition().y) && approxEq(node->getRot()+parent->getRot(),rb->getAngle()));
    rb->setVelocity({0,0}); rb->setAngularVelocity(0); b2Body_SetAwake(rb->id(),false);
    node->setPos(11,12); node->setRot(0.7f); world.update();
    check("sleeping body does not overwrite node", node->getX() == 11 && node->getY() == 12 && approxEq(node->getRot(),0.7f));
    check("direct node edits never drive dynamic body", !approxEq(rb->getPosition().x,node->getGlobalPos().x));
    for (auto type : {b2_staticBody, b2_kinematicBody}) {
        auto target = liveNode(); auto* fixed = target->addMod<RigidBody2D>(world,type);
        fixed->setPosition({70,80}); fixed->setAngle(0.3f);
        check("static/kinematic setter sync immediate", target->getX() == 70 && target->getY() == 80 && approxEq(target->getRot(),0.3f));
        if (type == b2_kinematicBody) {
            fixed->addCircle(15); fixed->setVelocity({60,0}); world.update();
            check("kinematic move events sync", approxEq(target->getX(), fixed->getPosition().x) && target->getX() > 70);
        }
    }
    // A standalone root can also have automatic storage duration.
    tc::Node standalone;
    tc::internal::setupNodeOnce(standalone);
    auto* standaloneRb = standalone.addMod<RigidBody2D>(world, b2_kinematicBody);
    standaloneRb->setPosition({30,60});
    check("standalone root setter sync", standalone.getX() == 30 && standalone.getY() == 60);
    // Raw userData must not be cast/dereferenced as a Mod.
    auto rawDef = b2DefaultBodyDef(); rawDef.type = b2_kinematicBody; rawDef.userData = reinterpret_cast<void*>(1);
    auto raw = b2CreateBody(world.id(), &rawDef); b2Body_SetLinearVelocity(raw,{1,0}); world.update();
    check("raw body userData tolerated", b2Body_IsValid(raw));
    // Transform observers can destroy owners while body-move events are being delivered.
    auto resetting = liveNode(); auto* resettingRb = resetting->addMod<RigidBody2D>(world,b2_dynamicBody);
    resettingRb->addBox(30,30); resettingRb->setVelocity({30,0});
    auto listener = resetting->localMatrixChanged.listen([&]() { world.setup(); });
    world.update();
    check("Node observer can reset World during move delivery", !b2Body_IsValid(resettingRb->id()));
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
        auto gravity = b2World_GetGravity(world.id());
        check("default gravity points down in y-down space", gravity.y > 0.0f && nearlyEqual(gravity.y, 10));
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
    tcCoreTest::WindowFixture receiver;
    testLifetime();
    testShapes();
    testPixelAndSync();
    testSubSteps(1);
    testSubSteps(4, true);
    std::printf("tcxBox2d3: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
