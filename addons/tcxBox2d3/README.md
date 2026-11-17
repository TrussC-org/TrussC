Experimental: Box2D v3.2, exclusive with tcxBox2d (choose one per project).

# tcxBox2d3

A thin, single-precision layer over Box2D v3.2.0 in `tcx::box2d`.
Select `tcxBox2d3` in `addons.make` and include `tcxBox2d.h`.
CMake 3.22 and a C17 compiler are required. Loading both addons fails during
configure; select only `tcxBox2d3` for a v3 project:

```make
# addons.make
tcxBox2d3
```

World, an owning RigidBody2D Mod, shapes as v3 ids, Node sync from body-move
events, and post-step events are available. Drag follows in slice 4.
Classic CircleBody, RectBody and PolyShape remain in the frozen v2 addon.

```cpp
#include <TrussC.h>
#include <tcxBox2d.h>
using namespace std;
using namespace tc;
using namespace tcx;
class tcApp : public App {
    box2d::World world;
    shared_ptr<Node> node;
    box2d::RigidBody2D* rb = nullptr;
    void setup() override {
        world.setup();
        node = make_shared<Node>();
        node->setPos(100, 100);
        addChild(node); // joins the live tree before addMod, so its body exists now
        rb = node->addMod<box2d::RigidBody2D>(world, b2_dynamicBody);
        rb->addBox(80, 40);
    }
    void update() override {
        world.update();
    }
};
```

Snippets below show the relevant lines inside this `tcApp`. Body operations
assume setup has attached `rb`; World configuration belongs in setup.

## Bodies and lifetime

The Mod creates one body at its Node's **global** position and Z rotation when
the Node is set up in the tree. Mod removal, exclusive replacement, and Node
destruction destroy that body and its shapes. World destruction or another
`world.setup()` invalidates existing Mods; getters return zero, setters do
nothing, and shape creation returns null/empty ids. Reattach a Mod to create a
body in the replacement World:

```cpp
world.setup();
node->removeMod<box2d::RigidBody2D>();
rb = node->addMod<box2d::RigidBody2D>(world, b2_dynamicBody);
```

Attach only after `world.setup()`. Mod setup is deferred until its Node joins
the live tree; shape calls before body creation return null/empty ids:

```cpp
world.setup();
node = make_shared<Node>();
addChild(node);
rb = node->addMod<box2d::RigidBody2D>(world, b2_dynamicBody);
rb->addCircle(20);
```

World and RigidBody2D cannot be copied or moved. Raw ids are the escape hatch
and use **meters**. Managed bodies and shapes must be destroyed through their
owner; remove the Mod instead of calling raw destroy functions:

```cpp
node->removeMod<box2d::RigidBody2D>();
rb = nullptr;
```

Use `id()` for upstream settings and impulses, and check validity after World
replacement or destruction. Do not overwrite managed body userData; it links
body-move events to the Mod (there is no public API for replacing that link):

```cpp
if (b2Body_IsValid(rb->id())) {
    b2Body_ApplyLinearImpulseToCenter(rb->id(), {0.0f, -5.0f}, true);
}
```

## Shapes

`addBox(width, height, def)` and `addCircle(radius, def)` create centered shapes
in pixels, returning `b2ShapeId`. The optional definition defaults to
`b2DefaultShapeDef()`: density 1 and friction 0.6 are preserved.

```cpp
auto def = b2DefaultShapeDef();
def.material.friction = 0.3f;
b2ShapeId box = rb->addBox(80, 40, def);
b2ShapeId circle = rb->addCircle(20);
```

`addPolygon(Path, def)` returns a vector of shape ids. A single convex outline
of up to eight vertices creates one polygon; concave/larger outlines and holes
use `Path::buildFillTriangles()` (the same triangulation as v2), then apply the
combined mass once. `shapeIds()` lists all shapes created by this Mod.

```cpp
Path path(vector<Vec2>{{0,0},{60,0},{60,30},{30,30},{30,60},{0,60}});
path.close();
auto polygons = rb->addPolygon(path);
const auto& shapes = rb->shapeIds();
```

Overlapping or self-overlapping contours count their overlap once per layer in
mass and inertia until the ROADMAP Clipper2 union lands (#501). Pass a simple,
non-overlapping outline, winding holes opposite to their enclosing ring:

```cpp
Path path(vector<Vec2>{{-30,-30},{30,-30},{30,30},{-30,30}});
path.close();
rb->addPolygon(path);
```

Degenerate polygons are rejected with a Warning on **every call**; unusable
triangle slivers are skipped with a Warning. Use separated corners and nonzero
area, and inspect the returned ids:

```cpp
Path path(vector<Vec2>{{0,0},{60,0},{0,60}});
path.close();
auto ids = rb->addPolygon(path);
if (ids.empty()) return;
```

Circle radii and box dimensions must be finite and positive; invalid values
produce a Warning and null id:

```cpp
rb->addCircle(20);
rb->addBox(80, 40);
```

## Pixel methods and Node sync

Position and linear velocity use pixels and pixels/second (30 px = 1 m).
Angles and angular velocities use radians and radians/second:

```cpp
rb->setPosition(Vec2(100, 100));
rb->setAngle(0.5f);
Vec2 position = rb->getPosition();
float angle = rb->getAngle();
```

```cpp
rb->setVelocity(Vec2(60, -120));
rb->setAngularVelocity(0.5f);
Vec2 velocity = rb->getVelocity();
float spin = rb->getAngularVelocity();
```

After each managed step, only moved bodies update their owning Nodes. Sleeping
bodies leave Node transforms alone. Parent transforms are converted from global
physics pixels to Node local position; local Z is preserved. This is a 2D XY
API with Z rotation, as in v2. Use Z rotation for the Node and its parents:

```cpp
node->setRot(0.5f);
rb->setAngle(0.5f);
```

Direct Node moves are **not synced back** to dynamic bodies (nor to static or
kinematic bodies). Use the Mod's pixel methods. `setPosition` and `setAngle`
update the Node immediately, including static and kinematic bodies:

```cpp
rb->setPosition(Vec2(100, 100));
rb->setVelocity(Vec2(60, 0));
world.update();
```

Pixel coordinates match TrussC (**y down**), with no axis flip. Raw Box2D meters
are y-down too; upstream samples' negative-y gravity becomes positive here:

```cpp
world.setup();
world.setGravity(Vec2(0, 300));
```

## World stepping and v3 behavior

Each `update()` advances exactly 1/60 second, with four solver sub-steps by
default. There is no automatic stepping or elapsed-time accumulator. Call it
once from the app's fixed update:

```cpp
world.update();
```

`setGravity` and `setSubSteps` also work before setup. Non-positive sub-step
counts are ignored; update before setup is a no-op. v3 uses sub-steps rather
than v2 iteration counts; there are no iteration setters:

```cpp
world.setSubSteps(4);
world.setup();
```

Workers default to one. `setWorkerCount` uses the built-in v3 task system and
upstream clamping, before setup or at runtime:

```cpp
world.setup();
world.setWorkerCount(2);
```

Raw settings use meters:

```cpp
world.setup();
b2World_SetGravity(world.id(), {0.0f, 10.0f});
```

Filters are upstream's 64-bit `b2Filter` in `b2ShapeDef`. Fixed rotation uses
upstream motion locks.

## Events

tcxBox2d3 delivers contact, sensor and hit events on RigidBody2D after each world.update().
Delivery follows Node sync, in this order: contact begin, contact end, hit,
sensor begin, sensor end; each category preserves v3's array order. Each event
notifies shape A then B (sensor then visitor for sensor events). There is no Stay.
Raw `b2World_Step` calls do not deliver TrussC events; use the managed step:

```cpp
world.update();
```

Contact begin/end flags are on from creation for every shape created by a
RigidBody2D Mod, regardless of listeners. Subscribing after a broad-phase
contact exists still receives future begin/end events; past events are not replayed.

At the next managed step after a listener state change, sensor flags are enabled
on **all Mod-created shapes in that World** while any Mod listens to sensor
begin/end. v3 requires both sensor and visitor flags and reads them each step: subscribing during an
existing overlap produces a begin on the next step. Removing the last sensor
listener turns off the automatic flags. Hit listeners enable hit flags only on
the listening Mod's shapes. Removing the last hit listener turns off those
automatic flags too. Explicit true sensor/hit flags from shape definitions
remain enabled. Shapes added later participate on the next managed step.

Raw shapes have no owning Mod; when mixing raw sensor/visitor shapes, enable
their sensor flag explicitly (inside tcApp setup, with the raw shape id):

```cpp
b2Shape_EnableSensorEvents(rawShapeId, true);
```

Raw ids also let you turn off flags on managed shapes. Contact flags are never
reapplied; raw sensor/hit overrides stick until the corresponding automatic listener state next changes (none ↔ some):

```cpp
b2Shape_EnableContactEvents(rb->shapeIds().front(), false);
b2Shape_EnableSensorEvents(rb->shapeIds().front(), false);
b2Shape_EnableHitEvents(rb->shapeIds().front(), false);
```

Keep the EventListener handles as tcApp members, for example
`EventListener contactBegin, contactEnd, sensorBegin, sensorEnd, hit;`.
`ContactEvent`, `SensorEvent` and `HitEvent` carry `shape`, `otherShape` and
`other`. Each receiver sees its own shape first. `other` is null if its owner
is gone or unmanaged; check it before accessing the other Mod:

```cpp
contactBegin = rb->onContactBegin.listen([](box2d::ContactEvent& e) {
    if (!e.other) return;
    e.other->getOwner()->setName("touching");
});
```

```cpp
contactEnd = rb->onContactEnd.listen([](box2d::ContactEvent& e) {
    if (!e.other) return;
    e.other->getOwner()->setName("separated");
});
```

```cpp
sensorBegin = rb->onSensorBegin.listen([](box2d::SensorEvent& e) {
    if (!e.other) return;
    e.other->getOwner()->setName("touching");
});
```

```cpp
sensorEnd = rb->onSensorEnd.listen([](box2d::SensorEvent& e) {
    if (!e.other) return;
    e.other->getOwner()->setName("separated");
});
```

For hit events, listen in setup, before bodies touch:

```cpp
hit = rb->onHit.listen([](box2d::HitEvent& e) {
    logNotice() << e.approachSpeed;
});
```

Hit events need an approach speed above v3's `hitEventThreshold` (default
1 m/s = 30 pixels/s); move the body fast enough for an impact:

```cpp
rb->setVelocity(Vec2(0, 300));
```

Contact begin and hit carry `point` in world pixels and a unit `normal` pointing
from the receiving body toward the other body. Begin uses the first point of
v3's current contact manifold (resolved from its contactId after the step),
reconstructed from body A's world center and anchorA. A zero-point manifold
uses the midpoint of the two body origins. Contact end leaves point/normal
zero. Hit also carries `approachSpeed` in pixels/second.

Every v3 event array is copied before Node observers or event listeners run.
Receivers and `other` are resolved again before each listener. Destroyed
receivers are skipped; destroying/resetting World stops the old step's delivery.
v3.2 emits end events on body destruction; surviving owners receive those on
the next managed step with null `other`. Shape ids retain their generation but
may be stale; validate before raw API access:

```cpp
if (!b2Shape_IsValid(e.otherShape)) return;
auto bodyId = b2Shape_GetBody(e.otherShape);
```

Listener changes follow core Event semantics: removal takes effect immediately;
a listener added during a notification starts at the next notification.
Delivery is synchronous on the thread calling update, including listeners
registered with Deliver::Main; step from the app's update thread:

```cpp
void update() override { world.update(); }
```

Re-entrant `world.update()` is refused with a Warning on every call. Request
a step in a listener and perform it next frame instead (tcApp members):

```cpp
bool stepRequested = false;
void requestStep() { stepRequested = true; }
void update() override { if (std::exchange(stepRequested, false)) world.update(); }
```

Concurrent World mutation or dispatch from multiple threads is unsupported;
there is no API for cross-thread physics mutation. Rare nested interleavings
outside owner destruction, re-setup, and listener changes have no added ordering
guarantees. Keep physics changes on the app update thread:

```cpp
void update() override { world.update(); }
```

## Offline build

Configure an existing project build tree before building/running its tests:

```sh
cmake -S addons/tcxBox2d3/tests -B addons/tcxBox2d3/tests/build-linux \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_BOX2D3=/path/to/box2d-v3.2.0
```

FetchContent name `box2d3` selects `v3.2.0` from
https://github.com/erincatto/box2d.git and links upstream target `box2d`.
Samples, unit tests, benchmarks, docs and profiling are disabled. CI discovers
this harness through `addons/*/tests/` and uses its shared FetchContent cache.
