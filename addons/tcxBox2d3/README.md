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

Slice 1 provides an owning World and synchronous fixed stepping. The complete
addon will add pixel units, Node/Mod lifetime ownership, and post-step event
delivery. RigidBody2D, shapes on Mods, events and drag are not implemented yet;
no public Mod/event/drag API exists in this slice. Classic CircleBody, RectBody
and PolyShape remain in the frozen v2 addon.

```cpp
#include <TrussC.h>
#include <tcxBox2d.h>
using namespace tc;
class tcApp : public App {
    tcx::box2d::World world;
    void setup() override {
        world.setup();
        world.setGravity(Vec2(0.0f, 300.0f)); // px/s²: 30 px = 1 m
        world.setSubSteps(4);
    }
    void update() override { world.update(); }
};
```

`setup()` creates the world, replacing any previous world and its bodies.
The destructor destroys the world and all contained Box2D objects. World cannot
be copied or moved. `setGravity()` and `setSubSteps()` also work before setup.
Default gravity follows v3: (0, -300) px/s². Set positive y gravity for a
screen coordinate scene as in the example above.
Non-positive sub-step counts are ignored. `update()` before setup is a no-op.

Each `update()` advances exactly 1/60 second, with four solver sub-steps by
default. Workers default to one; `setWorkerCount()` uses v3's built-in task
system and upstream clamping, before setup or at runtime:

```cpp
void setup() override { world.setup(); world.setWorkerCount(2); }
```

There is no automatic stepping or elapsed-time accumulator in this
slice. Call it once from the app's fixed update:

```cpp
void update() override { world.update(); }
```

## v3 behavior and raw ids

Raw ids are the escape hatch and use **meters**, rather than pixels. Do not
destroy the World id through the raw API: its lifetime belongs to World.
Managed bodies/shapes in later slices must likewise be destroyed through their
owners. Use raw calls for settings:

```cpp
void setup() override { world.setup(); b2World_SetGravity(world.id(), {0.0f, 10.0f}); }
```

v3 uses sub-steps, not v2 velocity/position iteration counts. There are no
iteration setters; choose the sub-step count instead:

```cpp
void setup() override { world.setSubSteps(4); world.setup(); }
```

v3 shape defaults are unchanged: density 1 and friction 0.6. Scenes migrating
from v2 may need tuning. Use upstream shape definitions (the Mod shape API is
not available yet):

```cpp
void setup() override { auto def = b2DefaultShapeDef(); def.material.friction = 0.3f; (void)def; }
```

The planned contact/sensor/hit events are delivered **after** managed steps,
with no Stay event and no delivery for raw `b2World_Step` calls. There is no
TrussC event API in this slice. The future `addPolygon(Path)` will triangulate
concave input; overlapping or self-overlapping contours count overlap once
per layer in mass and inertia until the ROADMAP Clipper2 union lands (#501).
Pass a simple, non-overlapping outline. No polygon API exists in this slice.
Filters will be upstream's 64-bit `b2Filter`; fixed rotation uses upstream
motion locks; Node sync will use body-move events.

## Offline build

Configure an existing project build tree before building/running its tests:

```sh
cmake -S addons/tcxBox2d3/tests -B addons/tcxBox2d3/tests/build-linux \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_BOX2D3=/path/to/box2d-v3.2.0
```

The ordinary FetchContent path fetches `v3.2.0` from
https://github.com/erincatto/box2d.git using the content name `box2d3` and
upstream target `box2d`. Samples, unit tests, benchmarks, docs and profiling
are disabled. CI discovers this harness through `addons/*/tests/` and uses
its normal shared FetchContent cache; no worker-specific source path is
needed in CI.
