# tcxBox2d

2D physics for [TrussC](https://github.com/TrussC-org/TrussC), built on
[Box2D](https://box2d.org/) v2.4.1 (fetched by CMake). Namespace `tcx::box2d`.

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
| `PolyShape::setupConvex(world, points, x, y)` | `Shape2D::convex(points)` | any number of points | One polygon of at most 8 points approximating the convex hull. |
| `PolyShape::setupCompound(world, path, x, y)` | `Shape2D::compound(path)` | any outline | The exact shape, one fixture per triangle. |

All three classic calls take either a `std::vector<tc::Vec2>` or a `tc::Path`.
`setup()` and `setupConvex()` use every point of every subpath together;
`setupCompound()` uses the path as an outline (a vector is one closed outline).

- **Concave input** to `setup()` / `polygon()` silently becomes its convex
  hull, as Box2D itself does. `PolyShape::getVertices()` and
  `RigidBody2D::shape().verts` hold that hull, and `draw()` / `drawFill()` /
  `ColliderRenderer2D` draw it, so what you see is what collides. Convex
  input whose points already go around the outline in order (either winding,
  any starting point) comes back as given. Otherwise, when points were dropped
  or listed in a crossing order (a Z-ordered square, a star-ordered pentagon),
  the hull comes back in Box2D's order, starting at the rightmost point.
- **`setupConvex()` / `convex()`** take the convex hull of the points and drop
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
`RigidBody2D` events per body pair. Enter (`onCollisionEnter`,
`onCollisionBegan` / `onTriggerBegan`) fires on the pair's first touching
contact, Stay once per update, and Exit (`onCollisionExit`,
`onCollisionEnded` / `onTriggerEnded`) when its last contact ends, however
many fixtures touch.

- This covers every body with several fixtures, not only compound ones:
  `World::createBounds()` makes its four walls as one static body, so a
  `RigidBody2D` already touching the floor gets no new `onCollisionBegan`
  when it reaches a side wall, and `onCollisionEnded` only when it leaves the
  last wall.
- An Exit whose last contact ends inside a physics step is dispatched after
  that step, and a contact of the same pair that begins in the same step
  cancels it: a body sliding from one fixture of a compound onto the next
  never sees Exit + Enter. `World::update()` does this; if you call
  `b2World::Step()` yourself, call `world.getCollisionManager()->update()`
  right after it, before creating or destroying bodies. Exits from outside a
  step (destroying a body, `SetEnabled(false)`, `SetType()`) fire at once.
- A body destroyed before its deferred Exit fires (`Body::destroy()`, a
  `RigidBody2D`'s node going away) gets none; the other side still gets its
  own, with that body null (`CollisionEvent::other`, `Contact2D::other`,
  `WorldContact::a` / `b`). Bodies freed with a raw `b2World::DestroyBody()`
  are not tracked: call `update()` first.
- Stay listeners and the deferred Exit listeners may destroy bodies,
  including the other body of their own pair, which then hears nothing more.

## Tests

`tests/` is a headless console harness, run by CI through
`examples/build_all.py --addon-tests-only`. Run it locally with
`trusscli run -p .` from `tests/`.
