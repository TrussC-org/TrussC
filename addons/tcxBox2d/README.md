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
(`b2_maxPolygonVertices`). tcxBox2d offers two ways to build one:

| Classic | Mod | Takes | Result |
| --- | --- | --- | --- |
| `PolyShape::setup(world, points, x, y)` | `Shape2D::polygon(points)` | 3 to 8 points | One polygon. Concave input becomes its convex hull. |
| `PolyShape::setupConvex(world, points, x, y)` | `Shape2D::convex(points)` | any number of points | One polygon of at most 8 points approximating the convex hull. |

Both classic calls also take a `tc::Path`; every point of every subpath is used.

- **Concave input** to `setup()` / `polygon()` silently becomes its convex
  hull, as Box2D itself does. `PolyShape::getVertices()` and
  `RigidBody2D::shape().verts` hold that hull, and `draw()` / `drawFill()` /
  `ColliderRenderer2D` draw it, so what you see is what collides. Convex
  input comes back as given, in its order; a hull that dropped points is in
  Box2D's order, starting at the rightmost point.
- **`setupConvex()` / `convex()`** take the convex hull of the points and drop
  the vertices whose removal loses the least area until 8 remain. No point is
  guaranteed to survive: tips and extents can shrink, a symmetric outline can
  come back lopsided, concave parts and holes are filled and curves become
  coarser.
- **Refused input** logs a warning and creates **no body**, the same in Debug
  and Release:
  - fewer than 3 points, or more than 8 for `setup()` / `polygon()`;
  - collinear points, or a hull with almost no area;
  - points that nearly coincide (Box2D merges points closer than
    `0.5 * b2_linearSlop`, 0.075 px at the default scale) so fewer than 3 are
    left;
  - a polygon so small next to its distance from the local origin that a
    dynamic body's rotational inertia rounds to zero (for example a 0.5 px
    triangle at (600, 600)): give points relative to the body position.

  Check `PolyShape::isCreated()` or `RigidBody2D::getBody() != nullptr`.
  Without a body, `PolyShape::draw()` and `ColliderRenderer2D` draw nothing
  for the polygon.

## Tests

`tests/` is a headless console harness, run by CI through
`examples/build_all.py --addon-tests-only`. Run it locally with
`trusscli run -p .` from `tests/`.
