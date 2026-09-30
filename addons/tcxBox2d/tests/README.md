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
  collides);
- `setupConvex()` / `Shape2D::convex()` take any number of points and make one
  fixture of at most 8 points whose mass is close to the outline's.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR; a non-zero exit fails the job. Run it locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
