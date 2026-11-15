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

Slice 2 provides World, an owning RigidBody2D Mod, shapes as v3 ids, and Node
sync from body-move events. Events and drag have no TrussC API yet (slices 3–4).
Classic CircleBody, RectBody and PolyShape remain in the frozen v2 addon.

```cpp
#include <TrussC.h>
#include <tcxBox2d.h>
using namespace tc;
namespace box2d = tcx::box2d;
class tcApp : public App {
    box2d::World world;
    std::shared_ptr<Node> node;
    box2d::RigidBody2D* rb = nullptr;
    void setup() override {
        world.setup();
        node = std::make_shared<Node>();
        node->setPos(100, 100);
        addChild(node); // joins the live tree before addMod, so its body exists now
        rb = node->addMod<box2d::RigidBody2D>(world, b2_dynamicBody);
        rb->addBox(80, 40);
    }
    void update() override { world.update(); }
};
```

Snippets below are replacement methods in this `tcApp` (after setup has attached
`rb`), unless they explicitly replace setup.

## Bodies and lifetime

The Mod creates one body at its Node's **global** position and Z rotation when
the Node is set up in the tree. Mod removal, exclusive replacement, and Node
destruction destroy that body and its shapes. World destruction or another
`world.setup()` invalidates existing Mods; getters return zero, setters do
nothing, and shape creation returns null/empty ids. Reattach a Mod to create a
body in the replacement World:

```cpp
void update() override { world.setup(); node->removeMod<box2d::RigidBody2D>();
    rb = node->addMod<box2d::RigidBody2D>(world, b2_dynamicBody); }
```

Attach only after `world.setup()`. Mod setup is deferred until its Node joins
the live tree; shape calls before body creation return null/empty ids:

```cpp
void setup() override { world.setup(); node = std::make_shared<Node>(); addChild(node);
    rb = node->addMod<box2d::RigidBody2D>(world, b2_dynamicBody); rb->addCircle(20); }
```

World and RigidBody2D cannot be copied or moved. Raw ids are the escape hatch
and use **meters**. Managed bodies and shapes must be destroyed through their
owner; remove the Mod instead of calling raw destroy functions:

```cpp
void update() override { node->removeMod<box2d::RigidBody2D>(); rb = nullptr; }
```

Use `id()` for upstream settings and impulses, and check validity after World
replacement or destruction. Do not overwrite managed body userData; it links
body-move events to the Mod (there is no public API for replacing that link):

```cpp
void update() override { if (b2Body_IsValid(rb->id()))
    b2Body_ApplyLinearImpulseToCenter(rb->id(), {0.0f, -5.0f}, true); }
```

## Shapes

`addBox(width, height, def)` and `addCircle(radius, def)` create centered shapes
in pixels, returning `b2ShapeId`. The optional definition defaults to
`b2DefaultShapeDef()`: density 1 and friction 0.6 are preserved.

```cpp
void update() override { auto def = b2DefaultShapeDef(); def.material.friction = 0.3f;
    b2ShapeId box = rb->addBox(80, 40, def); b2ShapeId circle = rb->addCircle(20); (void)box; (void)circle; }
```

`addPolygon(Path, def)` returns a vector of shape ids. A single convex outline
of up to eight vertices creates one polygon; concave/larger outlines and holes
use `Path::buildFillTriangles()` (the same triangulation as v2), then apply the
combined mass once. `shapeIds()` lists all shapes created by this Mod.

```cpp
void update() override { Path path(std::vector<Vec2>{{0,0},{60,0},{60,30},{30,30},{30,60},{0,60}}); path.close();
    auto polygons = rb->addPolygon(path); const auto& shapes = rb->shapeIds(); (void)polygons; (void)shapes; }
```

Overlapping or self-overlapping contours count their overlap once per layer in
mass and inertia until the ROADMAP Clipper2 union lands (#501). Pass a simple,
non-overlapping outline, winding holes opposite to their enclosing ring:

```cpp
void update() override { Path path(std::vector<Vec2>{{-30,-30},{30,-30},{30,30},{-30,30}}); path.close();
    rb->addPolygon(path); }
```

Degenerate polygons are rejected with a Warning on **every call**; unusable
triangle slivers are skipped with a Warning. Use separated corners and nonzero
area, and inspect the returned ids:

```cpp
void update() override { Path path(std::vector<Vec2>{{0,0},{60,0},{0,60}}); path.close();
    auto ids = rb->addPolygon(path); if (ids.empty()) return; }
```

Circle radii and box dimensions must be finite and positive; invalid values
produce a Warning and null id:

```cpp
void update() override { rb->addCircle(20); rb->addBox(80, 40); }
```

## Pixel methods and Node sync

Position and linear velocity use pixels and pixels/second (30 px = 1 m).
Angles and angular velocities use radians and radians/second:

```cpp
void update() override { rb->setPosition(Vec2(100, 100)); rb->setAngle(0.5f);
    Vec2 position = rb->getPosition(); float angle = rb->getAngle(); (void)position; (void)angle; }
```

```cpp
void update() override { rb->setVelocity(Vec2(60, -120)); rb->setAngularVelocity(0.5f);
    Vec2 velocity = rb->getVelocity(); float spin = rb->getAngularVelocity(); (void)velocity; (void)spin; }
```

After each managed step, only moved bodies update their owning Nodes. Sleeping
bodies leave Node transforms alone. Parent transforms are converted from global
physics pixels to Node local position; local Z is preserved. This is a 2D XY
API with Z rotation, as in v2. Use Z rotation for the Node and its parents:

```cpp
void update() override { node->setRot(0.5f); rb->setAngle(0.5f); }
```

Direct Node moves are **not synced back** to dynamic bodies (nor to static or
kinematic bodies). Use the Mod's pixel methods. `setPosition` and `setAngle`
update the Node immediately, including static and kinematic bodies:

```cpp
void update() override { rb->setPosition(Vec2(100, 100)); rb->setVelocity(Vec2(60, 0)); world.update(); }
```

Pixel coordinates match TrussC (**y down**), with no axis flip. Raw Box2D meters
are y-down too; upstream samples' negative-y gravity becomes positive here:

```cpp
void setup() override { world.setup(); world.setGravity(Vec2(0, 300)); }
```

## World stepping and v3 behavior

Each `update()` advances exactly 1/60 second, with four solver sub-steps by
default. There is no automatic stepping or elapsed-time accumulator. Call it
once from the app's fixed update:

```cpp
void update() override { world.update(); }
```

`setGravity` and `setSubSteps` also work before setup. Non-positive sub-step
counts are ignored; update before setup is a no-op. v3 uses sub-steps rather
than v2 iteration counts; there are no iteration setters:

```cpp
void setup() override { world.setSubSteps(4); world.setup(); }
```

Workers default to one. `setWorkerCount` uses the built-in v3 task system and
upstream clamping, before setup or at runtime:

```cpp
void setup() override { world.setup(); world.setWorkerCount(2); }
```

Raw settings use meters:

```cpp
void setup() override { world.setup(); b2World_SetGravity(world.id(), {0.0f, 10.0f}); }
```

Filters are upstream's 64-bit `b2Filter` in `b2ShapeDef`. Fixed rotation uses
upstream motion locks. The planned contact/sensor/hit events will be delivered
only after managed steps, without Stay; no TrussC event API exists in this slice.

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
