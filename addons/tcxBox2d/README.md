# tcxBox2d

2D physics for [TrussC](https://github.com/TrussC-org/TrussC), built on
[Box2D](https://box2d.org/) (fetched by CMake; version in
[docs/LICENSE.md](../../docs/LICENSE.md#third-party-libraries)). Namespace `tcx::box2d`.

Add `tcxBox2d` to your project's `addons.make`, then `#include <tcxBox2d.h>`.

Two APIs share one `box2d::World`:

- **Classic**: `World` plus body nodes: `CircleBody`, `RectBody`, `PolyShape`.
  See `example-node` / `example-collision` / `example-rawAccess`.
- **Mod** (experimental): `node->addMod<box2d::RigidBody2D>(box2d::Shape2D::...)`
  plus `box2d::ColliderRenderer2D` to draw it. See `example-basic`.

Units are pixels (30 px = 1 m by default, `World::scale`) and radians.

## Polygon shapes

A Box2D polygon is **one convex shape of 3 to 8 points**
(`b2_maxPolygonVertices`). tcxBox2d offers three ways to build a polygon body:

| Classic | Mod | Takes | Result |
| --- | --- | --- | --- |
| `PolyShape::setup(world, points, x, y)` | `Shape2D::polygon(points)` | 3 to 8 points | One polygon. Concave input becomes its convex hull. |
| `PolyShape::setupSimplified(world, points, x, y)` | `Shape2D::simplified(points)` | any number of points | One polygon of at most 8 points approximating the convex hull. |
| `PolyShape::setupCompound(world, path, x, y)` | `Shape2D::compound(path)` | any outline | The exact shape, one fixture per triangle. |

All three calls, classic and Mod, take either a `std::vector<tc::Vec2>` or a
`tc::Path`. `setup()` / `polygon()` and `setupSimplified()` / `simplified()`
use every point of every subpath together; `setupCompound()` / `compound()`
use the path as an outline (a vector is one closed outline).

- **Concave input** to `setup()` / `polygon()` becomes its convex hull, as
  Box2D itself does. When the hull leaves input points inside it (concave
  input, or points inside the hull), one warning gives the counts and points
  to the exact-shape call, once per process for each of `setup()` and
  `polygon()` (a scene that builds many bodies doesn't flood the log):
  `tcxBox2d: PolyShape::setup() dropped 2 of 7 points inside the convex hull;
  use setupCompound() to keep the exact shape.` Points on the outline
  (collinear points along an edge, duplicates) and hull points listed in a
  crossing order give no warning: the shape is the same.
  `PolyShape::getVertices()` and
  `RigidBody2D::shape().verts` hold that hull, and `draw()` / `drawFill()` /
  `ColliderRenderer2D` draw it, so what you see is what collides. Convex
  input whose points already go around the outline in order (either winding,
  any starting point) comes back as given. Otherwise, when points were dropped
  or listed in a crossing order (a Z-ordered square, a star-ordered pentagon),
  the hull comes back in Box2D's order, starting at the rightmost point.
- **`setupSimplified()` / `simplified()`** take the convex hull of the points and drop
  the vertices whose removal loses the least area until 8 remain. No point is
  guaranteed to survive: tips and extents can shrink, a symmetric outline can
  come back lopsided, concave parts and holes are filled and curves become
  coarser.
- **`setupCompound()` / `compound()`** keep any outline exactly: concave parts,
  holes and any number of points. `Path::buildFillTriangles()` triangulates it,
  the same fill `Path::drawFill()` draws (non-zero winding: a subpath wound
  opposite to its enclosing one is a hole; self-intersections are split), and
  each triangle becomes one fixture on the one body. Box2D sums mass and
  centroid over the fixtures. A path that is one convex ring of at most 8
  points becomes one ordinary polygon fixture instead. Slivers Box2D can't use
  (collinear or nearly coincident corners, almost no area) are skipped with
  one warning. Tiny triangles are kept: the check against the distance from
  the origin (below) runs once on the whole body, as Box2D's own check does,
  not per triangle.
  - Area the fill covers more than once gets one layer of triangles per
    cover, and so weighs (mass, inertia) once per layer while it draws the
    same: a self-overlapping outline (a pentagram's center), overlapping
    subpaths wound the same way (glyph contours), a "hole" wound like its
    outer ring. Give a simple outline, and wind holes opposite to the outer
    ring (`Path::reverseWinding()`).
  - `getVertices()` returns the outline points (every subpath, in order);
    `draw()` outlines each subpath and `drawFill()` fills like
    `Path::drawFill()`, from triangles kept when the body was made (not
    tessellated every frame). `ColliderRenderer2D` does the same.
  - The body's `Collider2D` stands for all its fixtures: its filter setters
    change every fixture, as `Body::setSensor()` and
    `RigidBody2D::setTrigger()` do.
  - Collision events are per touching **body pair**, not per fixture (see
    [Collision events](#collision-events)).
  - The fixture count grows with the outline's detail.
- **Refused input** logs a warning and creates **no body**, the same in Debug
  and Release:
  - fewer than 3 points, or more than 8 for `setup()` / `polygon()`;
  - collinear points, or a hull with almost no area;
  - points that nearly coincide (Box2D merges points closer than
    `0.5 * b2_linearSlop`, 0.075 px at the default scale) so fewer than 3 are
    left;
  - a polygon so small next to its distance from the local origin that a
    dynamic body's rotational inertia rounds to zero (for example a 0.5 px
    triangle at (600, 600)): give points relative to the body position. For
    `setupCompound()` / `compound()` this is checked for the whole body
    (all its fixtures together, with a margin that grows with the fixture
    count); a single convex ring of at most 8 points that fails it is
    refused as it is, not triangulated.

  Check `PolyShape::isCreated()` or `RigidBody2D::getBody() != nullptr`.
  Without a body, `PolyShape::draw()` and `ColliderRenderer2D` draw nothing
  for the polygon or the compound outline.

## Collision events

Both APIs count contacts per pair: `Collider2D` events per collider pair,
`RigidBody2D` events per body pair. Enter fires on the pair's first touching
contact, Stay once per update,
and Exit when its last contact ends, however many fixtures touch. Classic
colliders use `onCollisionEnter/Stay/Exit` for both solid and sensor contacts;
Mods use `onTriggerEnter/Stay/Exit` for sensor contacts and `onCollision*`
for solid contacts.

- This covers every body with several fixtures, not only compound ones:
  `World::createBounds()` makes its four walls as one static body, so a
  `RigidBody2D` already touching the floor gets no new `onCollisionEnter`
  when it reaches a side wall, and `onCollisionExit` only when it leaves the
  last wall.
- Begin and End are delivered in observed Box2D order after each managed
  sub-step; Stay runs once per update. Fixture hand-over folds End + Begin
  only within the same managed sub-step, so sliding across a compound seam
  stays continuous. Raw `b2World::Step()` events wait for the next fresh-frame
  `World::update()` (including zero-dt updates), preserving every aggregated
  transition; raw Steps have no hand-over folding.
- TrussC contact listeners run after Box2D returns. Outside a listener,
  explicit `Body::destroy()`, disable and type changes deliver eligible Exits
  before the wrapper returns, unless an earlier raw event still awaits update.
  Inside a listener, the surviving side's Exit waits until the initiating
  callback returns. Body destruction and Mod teardown defer Exits until that
  callback-return boundary, or the next update when outside dispatch.
- A destroyed side receives no further callbacks, including its own Exit.
  The surviving side receives Exit with a null counterpart
  (`CollisionEvent::other`, `Contact2D::other` / `otherNode`, or
  `WorldContact::a` / `b`).
- Begin, Stay and Exit listeners may destroy either or both bodies, clear the
  World or delete it. Borrowed payload pointers can become null even inside
  the initiating callback; recheck after mutation and retain application IDs
  rather than raw physics pointers. Clear/setup discard the old contact epoch.
- Classic `Collider2D::isTrigger` changes fixture sensor state before each
  managed sub-step when its value changes. Use `setTrigger(bool)` or
  `Body::setSensor(bool)` for immediate application before raw Steps. Each
  transition retains its recorded sensor classification; sensor aggregation of
  a queued Begin is limited to its own managed sub-step.
- Register a borrowed user destruction listener with
  `World::setDestructionListener()`, preserving the World's retirement observer
  in Box2D's destruction-listener slot.

Migration: move destroyed-side Exit cleanup before release, accept callback-return surviving Exits with null counterparts, and drain raw Step events through update.

## Raw Box2D access

Use `World::getWorld()` for direct `b2World` access.

b2Body user data is reserved by TrussC; put application data in your Body subclass. Fixture user data is reserved too.

Bodies owned by a TrussC Body or RigidBody2D are destroyed through TrussC (`Body::destroy()`, removing the Mod, `World::clear()`); don't call raw `b2World::DestroyBody`/`DestroyFixture` on them. Raw destroys of bodies you created yourself are tracked when the body still has fixtures (the destruction listener); removing a body's last fixture and then the body without an `update()` in between is not tracked.

## Tests

`tests/` is a headless console harness, run by CI through
`examples/build_all.py --addon-tests-only`. Run it locally with
`trusscli run -p .` from `tests/`.
