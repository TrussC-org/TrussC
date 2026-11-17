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

struct Pair {
    std::unique_ptr<World> world = std::make_unique<World>();
    std::shared_ptr<tc::Node> a = liveNode(), b = liveNode();
    RigidBody2D *ra, *rb;
    b2ShapeId sa, sb;
    explicit Pair(bool sensor = false, bool impact = false) {
        world->setGravity({0,0}); world->setup();
        a->setPos(90,120); b->setPos(impact ? 160 : 140,120);
        ra = a->addMod<RigidBody2D>(*world,b2_staticBody);
        rb = b->addMod<RigidBody2D>(*world,b2_dynamicBody);
        auto def = b2DefaultShapeDef(); def.isSensor = sensor;
        sa = ra->addCircle(30,def); sb = rb->addCircle(30);
        if (impact) rb->setVelocity({-600,0});
    }
    void step(int count = 1) { for (int i=0; i<count; ++i) world->update(); }
};
void testEventKinds() {
    using namespace tcx::box2d;
    for (bool sensor : {false,true}) {
        Pair p(sensor);
        int beginA=0, beginB=0, endA=0, endB=0;
        auto validate = [&](auto& e, bool sideA) {
            check("event shape ids and other", B2_ID_EQUALS(e.shape,sideA?p.sa:p.sb) &&
                  B2_ID_EQUALS(e.otherShape,sideA?p.sb:p.sa) && e.other==(sideA?p.rb:p.ra));
        };
        tc::EventListener la,lb,leA,leB;
        if (sensor) {
            la=p.ra->onSensorBegin.listen([&](SensorEvent& e){ ++beginA; validate(e,true); });
            lb=p.rb->onSensorBegin.listen([&](SensorEvent& e){ ++beginB; validate(e,false); });
            leA=p.ra->onSensorEnd.listen([&](SensorEvent& e){ ++endA; validate(e,true); });
            leB=p.rb->onSensorEnd.listen([&](SensorEvent& e){ ++endB; validate(e,false); });
        } else {
            la=p.ra->onContactBegin.listen([&](ContactEvent& e){
                ++beginA; validate(e,true);
                check("contact point pixels and A normal", std::abs(e.point.x-115)<3 && std::abs(e.point.y-120)<1 && e.normal.x>0.99f);
                check("Node sync precedes events", approxEq(p.b->getX(),p.rb->getPosition().x));
            });
            lb=p.rb->onContactBegin.listen([&](ContactEvent& e){ ++beginB; validate(e,false); check("B normal flipped",e.normal.x < -0.99f); });
            leA=p.ra->onContactEnd.listen([&](ContactEvent& e){ ++endA; validate(e,true); });
            leB=p.rb->onContactEnd.listen([&](ContactEvent& e){ ++endB; validate(e,false); });
        }
        p.step(); check("default defs begin on both sides",beginA==1 && beginB==1);
        p.rb->setPosition({300,120}); p.step(2);
        check("end on both sides",endA==1 && endB==1);
    }
    Pair p(false,true);
    p.a->removeMod<RigidBody2D>();
    p.ra=p.a->addMod<RigidBody2D>(*p.world,b2_staticBody);
    int hits=0;
    auto a=p.ra->onHit.listen([&](HitEvent& e){
        ++hits; check("hit A ids normal point speed", e.other==p.rb && B2_ID_EQUALS(e.shape,p.sa) &&
                     B2_ID_EQUALS(e.otherShape,p.sb) && e.normal.x>0.99f && std::abs(e.point.x-120)<5 && std::abs(e.point.y-120)<1 && e.approachSpeed>500);
    });
    auto b=p.rb->onHit.listen([&](HitEvent& e){ ++hits; check("hit B flipped",e.other==p.ra && e.normal.x < -0.99f && B2_ID_EQUALS(e.shape,p.sb)); });
    p.sa=p.ra->addCircle(30); // Created after listen(), with the default definition.
    p.step(12); check("listen in setup before touch receives hits on both sides",hits==2);
}
void testEventOrder() {
    using namespace tcx::box2d;
    Pair p(false,true);
    auto sensorNode=liveNode(); sensorNode->setPos(150,120);
    auto* sensor=sensorNode->addMod<RigidBody2D>(*p.world,b2_staticBody);
    auto def=b2DefaultShapeDef(); def.isSensor=true; sensor->addCircle(100,def);
    struct Entry { int kind; b2ShapeId shape, other; };
    std::vector<Entry> delivered;
    std::vector<tc::EventListener> listeners;
    for (auto* body : {p.ra,p.rb,sensor}) {
        listeners.push_back(body->onContactBegin.listen([&](ContactEvent& e){delivered.push_back({0,e.shape,e.otherShape});}));
        listeners.push_back(body->onContactEnd.listen([&](ContactEvent& e){delivered.push_back({1,e.shape,e.otherShape});}));
        listeners.push_back(body->onHit.listen([&](HitEvent& e){delivered.push_back({2,e.shape,e.otherShape});}));
        listeners.push_back(body->onSensorBegin.listen([&](SensorEvent& e){delivered.push_back({3,e.shape,e.otherShape});}));
        listeners.push_back(body->onSensorEnd.listen([&](SensorEvent& e){delivered.push_back({4,e.shape,e.otherShape});}));
    }
    int seen[5]={};
    for (int step=0;step<15;++step) {
        if(step==10) p.rb->setPosition({600,120});
        delivered.clear(); p.step();
        auto contacts=b2World_GetContactEvents(p.world->id());
        auto sensors=b2World_GetSensorEvents(p.world->id());
        std::vector<Entry> expected;
        auto append=[&](int kind,b2ShapeId a,b2ShapeId b){++seen[kind];expected.push_back({kind,a,b});expected.push_back({kind,b,a});};
        for(int i=0;i<contacts.beginCount;++i) append(0,contacts.beginEvents[i].shapeIdA,contacts.beginEvents[i].shapeIdB);
        for(int i=0;i<contacts.endCount;++i) append(1,contacts.endEvents[i].shapeIdA,contacts.endEvents[i].shapeIdB);
        for(int i=0;i<contacts.hitCount;++i) append(2,contacts.hitEvents[i].shapeIdA,contacts.hitEvents[i].shapeIdB);
        for(int i=0;i<sensors.beginCount;++i) append(3,sensors.beginEvents[i].sensorShapeId,sensors.beginEvents[i].visitorShapeId);
        for(int i=0;i<sensors.endCount;++i) append(4,sensors.endEvents[i].sensorShapeId,sensors.endEvents[i].visitorShapeId);
        bool matches=expected.size()==delivered.size();
        for(size_t i=0;matches && i<expected.size();++i) matches=expected[i].kind==delivered[i].kind &&
            B2_ID_EQUALS(expected[i].shape,delivered[i].shape) && B2_ID_EQUALS(expected[i].other,delivered[i].other);
        check("all event categories preserve upstream array and side order",matches);
    }
    for(int count:seen) check("order scene exercised each event kind",count>0);
}
void testEventFlags() {
    using namespace tcx::box2d;
    Pair p;
    auto defaults=[](b2ShapeId s) { return b2Shape_AreContactEventsEnabled(s) && !b2Shape_AreSensorEventsEnabled(s) && !b2Shape_AreHitEventsEnabled(s); };
    check("contact enabled at creation without listeners",defaults(p.sa) && defaults(p.sb));
    auto contact=p.ra->onContactBegin.listen([](ContactEvent&){});
    auto sensor=p.ra->onSensorBegin.listen([](SensorEvent&){});
    auto hit=p.ra->onHit.listen([](HitEvent&){});
    p.step();
    check("policy E contact always, local hit, world sensor",b2Shape_AreContactEventsEnabled(p.sa) && b2Shape_AreHitEventsEnabled(p.sa) &&
          b2Shape_AreSensorEventsEnabled(p.sa) && b2Shape_AreSensorEventsEnabled(p.sb) && b2Shape_AreContactEventsEnabled(p.sb) && !b2Shape_AreHitEventsEnabled(p.sb));
    auto late=p.ra->addCircle(10); p.step();
    check("later shapes enabled next step",b2Shape_AreContactEventsEnabled(late) && b2Shape_AreSensorEventsEnabled(late) && b2Shape_AreHitEventsEnabled(late));
    contact.disconnect(); sensor.disconnect(); hit.disconnect(); p.step();
    check("last disconnect restores defaults",defaults(p.sa) && defaults(p.sb) && defaults(late));
    auto def=b2DefaultShapeDef(); def.enableContactEvents=true; def.enableSensorEvents=true; def.enableHitEvents=true;
    auto explicitShape=p.ra->addCircle(5,def); p.step();
    auto explicitSensor=p.ra->onSensorBegin.listen([](SensorEvent&){});
    auto explicitHit=p.ra->onHit.listen([](HitEvent&){});
    p.step();
    explicitSensor.disconnect();
    explicitHit.disconnect();
    p.step();
    check("explicit flags survive listener removal",b2Shape_AreContactEventsEnabled(explicitShape) && b2Shape_AreSensorEventsEnabled(explicitShape) && b2Shape_AreHitEventsEnabled(explicitShape));
    b2Shape_EnableContactEvents(late, false);
    p.step();
    check("raw contact disable is preserved", !b2Shape_AreContactEventsEnabled(late));
    tc::Path triangle(std::vector<tc::Vec2>{{0,0},{10,0},{0,10}});
    triangle.close();
    auto polygons=p.ra->addPolygon(triangle);
    auto box=p.ra->addBox(10,10);
    check("box and polygon contact flags enabled at creation", b2Shape_AreContactEventsEnabled(box) &&
          !polygons.empty() && b2Shape_AreContactEventsEnabled(polygons.front()));
    for (bool sensorKind : {false,true}) {
        Pair latePair(sensorKind);
        latePair.a->removeMod<RigidBody2D>();
        latePair.ra=latePair.a->addMod<RigidBody2D>(*latePair.world,b2_staticBody);
        int received=0;
        tc::EventListener listener;
        if (sensorKind) listener=latePair.ra->onSensorBegin.listen([&](SensorEvent&){++received;});
        else listener=latePair.ra->onContactBegin.listen([&](ContactEvent&){++received;});
        auto lateDef=b2DefaultShapeDef(); lateDef.isSensor=sensorKind;
        latePair.sa=latePair.ra->addCircle(30,lateDef);
        latePair.step(); check("shape created after listen fires",received==1);
    }
}
void testRawEventFlagOverrides() {
    using namespace tcx::box2d;
    Pair p;
    auto sensor = p.ra->onSensorBegin.listen([](SensorEvent&) {});
    auto hit = p.ra->onHit.listen([](HitEvent&) {});
    p.step();
    b2Shape_EnableSensorEvents(p.sb, false);
    b2Shape_EnableHitEvents(p.sa, false);
    p.step(3);
    check("raw sensor off survives managed steps while listening",
          !b2Shape_AreSensorEventsEnabled(p.sb) && b2Shape_AreSensorEventsEnabled(p.sa));
    check("raw hit off survives managed steps while listening",
          !b2Shape_AreHitEventsEnabled(p.sa));

    auto sensor2 = p.rb->onSensorEnd.listen([](SensorEvent&) {});
    auto hit2 = p.ra->onHit.listen([](HitEvent&) {});
    p.step();
    check("second sensor listener preserves raw off", !b2Shape_AreSensorEventsEnabled(p.sb));
    check("second hit listener preserves raw off", !b2Shape_AreHitEventsEnabled(p.sa));
    auto late = p.ra->addBox(10, 10);
    p.step();
    check("new shape gets current sensor and hit flags",
          b2Shape_AreSensorEventsEnabled(late) && b2Shape_AreHitEventsEnabled(late));
    check("new shape does not reapply old shape flags",
          !b2Shape_AreSensorEventsEnabled(p.sb) && !b2Shape_AreHitEventsEnabled(p.sa));
    sensor.disconnect();
    hit.disconnect();
    p.step();
    check("remaining listeners preserve raw overrides",
          !b2Shape_AreSensorEventsEnabled(p.sb) && !b2Shape_AreHitEventsEnabled(p.sa));

    sensor2.disconnect();
    p.step();
    check("sensor transition leaves raw hit off", !b2Shape_AreHitEventsEnabled(p.sa));
    sensor = p.ra->onSensorBegin.listen([](SensorEvent&) {});
    p.step();
    check("sensor re-subscription reapplies automatic flag", b2Shape_AreSensorEventsEnabled(p.sb));
    check("sensor re-subscription leaves raw hit off", !b2Shape_AreHitEventsEnabled(p.sa));
    b2Shape_EnableSensorEvents(p.sb, false);
    hit2.disconnect();
    p.step();
    hit = p.ra->onHit.listen([](HitEvent&) {});
    p.step();
    check("hit re-subscription reapplies automatic flag", b2Shape_AreHitEventsEnabled(p.sa));
    check("hit transitions leave raw sensor off", !b2Shape_AreSensorEventsEnabled(p.sb));
}
void testLateSubscriptions() {
    using namespace tcx::box2d;
    // Review reproduction: broad-phase overlap exists before subscribing.
    Pair p;
    p.rb->setPosition({152,120});
    p.step();
    b2ContactData data[8];
    check("late subscription starts with non-touching broad-phase contact",
          b2Body_GetContactCapacity(p.rb->id())==1 && b2Body_GetContactData(p.rb->id(),data,8)==0);
    int begins=0, ends=0;
    auto begin=p.ra->onContactBegin.listen([&](ContactEvent&){++begins;});
    auto end=p.ra->onContactEnd.listen([&](ContactEvent&){++ends;});
    p.rb->setVelocity({-60,0});
    p.step(12);
    check("listen after broad-phase overlap receives begin",begins==1 && b2Body_GetContactData(p.rb->id(),data,8)==1);
    p.rb->setPosition({300,120});
    p.step(2);
    check("listen after broad-phase overlap receives end",ends==1);
    {
        Pair touching;
        touching.step();
        int destroyedEnds=0;
        auto listener=touching.ra->onContactEnd.listen([&](ContactEvent& e){
            ++destroyedEnds;
            check("late end subscription resolves destroyed owner",!e.other && B2_ID_EQUALS(e.otherShape,touching.sb));
        });
        touching.step();
        touching.b.reset();
        touching.step(2);
        check("listen after touching receives destroyed-owner end",destroyedEnds==1);
    }
    {
        Pair sensor(true), separateWorld(true);
        sensor.step();
        int sensorBegins=0;
        auto listener=sensor.ra->onSensorBegin.listen([&](SensorEvent&){++sensorBegins;});
        sensor.step();
        check("sensor subscription discovers existing overlap next step",sensorBegins==1);
        separateWorld.step();
        check("sensor listeners stay within their World",!b2Shape_AreSensorEventsEnabled(separateWorld.sa) && !b2Shape_AreSensorEventsEnabled(separateWorld.sb));
        auto otherListener=sensor.rb->onSensorEnd.listen([](SensorEvent&){});
        listener.disconnect();
        sensor.step();
        check("another Mod listener keeps world sensor flags on",b2Shape_AreSensorEventsEnabled(sensor.sa) && b2Shape_AreSensorEventsEnabled(sensor.sb));
        otherListener.disconnect();
        sensor.step();
        check("last sensor disconnect disables both sides",!b2Shape_AreSensorEventsEnabled(sensor.sa) && !b2Shape_AreSensorEventsEnabled(sensor.sb));
        listener=sensor.ra->onSensorBegin.listen([&](SensorEvent&){++sensorBegins;});
        sensor.step();
        check("sensor resubscription rediscovers existing overlap",sensorBegins==2);
    }
}
void testEventLifetime() {
    using namespace tcx::box2d;
    // Register on both sides: whichever v3 orders first performs the action.
    for (int action=0; action<4; ++action) {
        Pair p;
        int calls=0, later=0, nullOther=0;
        auto callback=[&](ContactEvent& e){
            ++calls;
            if (!e.other) { ++nullOther; return; }
            if (calls!=1) return;
            const bool sideA=B2_ID_EQUALS(e.shape,p.sa);
            if (action==0) (sideA?p.a:p.b).reset();
            if (action==1) (sideA?p.b:p.a).reset();
            if (action==2) p.world.reset();
            if (action==3) p.world->setup();
        };
        auto a=p.ra->onContactBegin.listen(callback);
        auto b=p.rb->onContactBegin.listen(callback);
        auto a2=p.ra->onContactBegin.listen([&](ContactEvent& e){++later; if (!e.other) ++nullOther;});
        auto b2=p.rb->onContactBegin.listen([&](ContactEvent& e){++later; if (!e.other) ++nullOther;});
        p.step();
        if (action==0) check("self destroyed skips remaining listeners, other receives null",calls==2 && later==1 && nullOther==2);
        if (action==1) check("other destroyed skips side, refreshes other per listener",calls==1 && later==1 && nullOther==1);
        if (action>=2) {
            check("World destroy/reset stops immediately",calls==1 && later==0 && !b2Body_IsValid(p.ra->id()) && !b2Body_IsValid(p.rb->id()));
            if (action==3) {
                p.a->removeMod<RigidBody2D>(); p.b->removeMod<RigidBody2D>();
                p.ra=p.a->addMod<RigidBody2D>(*p.world,b2_staticBody); p.sa=p.ra->addCircle(30);
                p.rb=p.b->addMod<RigidBody2D>(*p.world,b2_dynamicBody); p.sb=p.rb->addCircle(30);
                int fresh=0; auto listener=p.ra->onContactBegin.listen([&](ContactEvent&){++fresh;});
                p.step(); check("new world steps normally after reset",fresh==1);
            }
        }
    }
    {
        Pair p; p.ra->addCircle(25);
        int selfCalls=0, survivorCalls=0;
        auto first=p.ra->onContactBegin.listen([&](ContactEvent&){++selfCalls;p.a.reset();});
        auto survivor=p.rb->onContactBegin.listen([&](ContactEvent& e){++survivorCalls;check("copied later events have null other",!e.other);});
        p.step(); check("self destruction skips later queued events, copied arrays survive",selfCalls==1 && survivorCalls==2);
    }
    for (bool sensor : {false,true}) {
        Pair p(sensor); int begins=0, ends=0;
        tc::EventListener begin,end;
        if (sensor) {
            begin=p.ra->onSensorBegin.listen([&](SensorEvent&){++begins;});
            end=p.ra->onSensorEnd.listen([&](SensorEvent& e){++ends; check("sensor gone owner null",!e.other && B2_ID_EQUALS(e.otherShape,p.sb));});
        } else {
            begin=p.ra->onContactBegin.listen([&](ContactEvent&){++begins;});
            end=p.ra->onContactEnd.listen([&](ContactEvent& e){++ends; check("contact gone owner null",!e.other && B2_ID_EQUALS(e.otherShape,p.sb));});
        }
        p.step(); p.b.reset(); p.step(2);
        check("destruction between steps emits one end",begins==1 && ends==1);
    }
    {
        Pair p; int first=0, removed=0, added=0;
        tc::EventListener self, later, newcomer;
        self=p.ra->onContactBegin.listen([&](ContactEvent&){
            ++first; self.disconnect(); later.disconnect();
            newcomer=p.ra->onContactBegin.listen([&](ContactEvent&){++added;});
        });
        later=p.ra->onContactBegin.listen([&](ContactEvent&){++removed;});
        p.step(); check("Event removes immediately and adds on next notify",first==1 && removed==0 && added==0);
        p.rb->setPosition({300,120}); p.step(2); p.rb->setPosition({140,120}); p.step(2);
        check("added listener receives next begin",first==1 && added==1);
    }
    {
        Pair p; int warnings=0;
        auto logger=tc::getLogger().onLog.listen([&](tc::LogEventArgs& e){if(e.level==tc::LogLevel::Warning) ++warnings;});
        auto listener=p.ra->onContactBegin.listen([&](ContactEvent&){p.world->update(); p.world->update();});
        p.step(); check("each reentrant update warns and returns",warnings==2);
    }
    {
        Pair p; int calls=0;
        // Unmanaged raw bodies never supply a RigidBody2D, even with arbitrary userData.
        p.b->removeMod<RigidBody2D>();
        auto def=b2DefaultBodyDef(); def.type=b2_dynamicBody; def.position={140.0f/30,4}; def.userData=reinterpret_cast<void*>(1);
        auto raw=b2CreateBody(p.world->id(),&def); auto shapeDef=b2DefaultShapeDef(); b2Circle circle={{0,0},1};
        b2CreateCircleShape(raw,&shapeDef,&circle);
        auto listener=p.ra->onContactBegin.listen([&](ContactEvent& e){++calls;check("unmanaged other null",!e.other);});
        p.step(); check("managed listener receives raw contact",calls==1);
    }
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
    testEventKinds();
    testEventOrder();
    testEventFlags();
    testRawEventFlagOverrides();
    testLateSubscriptions();
    testEventLifetime();
    testLifetime();
    testShapes();
    testPixelAndSync();
    testSubSteps(1);
    testSubSteps(4, true);
    std::printf("tcxBox2d3: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
