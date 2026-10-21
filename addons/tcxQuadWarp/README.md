# tcxQuadWarp

`tcx::quadwarp::QuadWarp` maps source coordinates onto a perspective quad.
Set the source rectangle with `setSourceRect()` and the four screen corners
with `dstPoints`, in perimeter order. Use `getMatrix()` to render the warp.

## Coordinate conversion

Map a screen point back to source coordinates (inverse warp):

```cpp
tc::Vec2 source;
if (warp.screenToSource(screen, source)) handleSourcePoint(source);
```

Map a source point to screen coordinates (forward warp):

```cpp
tc::Vec2 screen;
if (warp.sourceToScreen(source, screen)) drawScreenMarker(screen);
```

Both helpers use `getMatrix()` and perform the homogeneous divide. They return
`false` for degenerate or folded (self-intersecting or non-convex) quads, invalid
homographies, non-finite results, or a homogeneous coordinate `w <= 0`, leaving
the output unchanged. Keep both quads convex. Neither helper clamps or tests
bounds: points outside the source rectangle or screen quad can map successfully.
The caller decides which points are inside, for example by checking Fbo bounds.
