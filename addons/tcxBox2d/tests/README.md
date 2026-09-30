# tcxBox2d tests

Headless console test (no window). The world is stepped by hand and nothing is
drawn.

Polygon points (#342). `PolyShape::setup()` and `RigidBody2D` with
`Shape2D::polygon()` share one check, run before the body is created:

- fewer than 3 or more than 8 points, collinear points, points Box2D would
  merge, and a hull with almost no area each give one warning and no body, in
  Debug and Release alike;
- concave input with 3 to 8 points becomes its convex hull, and
  `getVertices()` / `shape().verts` hold that hull (what draws is what
  collides); convex input already in outline order keeps its order, and hull
  points listed in a crossing order come back in hull order;
- `setupConvex()` / `Shape2D::convex()` take any number of points and make one
  fixture of at most 8 points whose mass is close to the outline's; input
  they collapse to fewer than 3 points gets a warning saying so.

Compound bodies (#427). `setupCompound()` / `Shape2D::compound()` keep any
outline exactly, one fixture per triangle:

- a 20-point circle gives one fixture per triangle and the 20-gon's mass; a
  convex outline of at most 8 points (also with a closing point) gives exactly
  one fixture, a pentagram does not;
- a notch and a hole stay empty: a ball in a ring's hole falls to the hole's
  floor, and a ball above the solid part lands on it;
- slivers are skipped with one warning; an outline with nothing usable gives a
  warning and no body;
- `Collider2D` filter setters, `setSensor()` and `RigidBody2D::setTrigger()`
  reach every fixture;
- a box pressed into a compound bar touches several of its fixtures at once,
  and still gives exactly one Enter / Began per side, one Stay per update, and
  one Exit / Ended per side when they separate (classic `Collider2D` and Mod
  `RigidBody2D` events).

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR; a non-zero exit fails the job. Run it locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
