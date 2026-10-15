# Render regression (#821, PR 0)

`allCoreTests renderRegression` skips without opening a window. The display-test
lane runs `allCoreTests renderRegression --self-check` under Xvfb/Mesa. A standalone
project generated with trusscli accepts the same arguments.

- `--self-check`: assertions against known RGBA values in all 16 scenes.
- `--record <dir>`: assertions, PNGs and `samples.json` into an empty directory.
- `--compare <dir>`: assertions, PNGs, `samples.json` and red difference overlays
  into `<dir>.compare`. A missing image, different size, missing scene, or pixel
  exceeding its tolerance fails. All four channels are compared.

Use the same machine, backend and DPI for a before/after pair. Linux invocation:

```sh
export LIBGL_ALWAYS_SOFTWARE=1
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
xvfb-run -a python3 tools/render_regression.py <base-ref> <head-ref>
```

The driver creates two temporary detached worktrees, records on their merge-base,
compares on head, and self-checks head. Build `tools/bin/trusscli` first. Set
`TC_BUILD_WRAPPER` when local builds require a build guard. Outputs remain in
`render-regression-output`; the worktrees are removed even if a comparison fails.
The initial batch, whose base lacks the harness, self-checks head only.
Set `CMAKE_C_COMPILER_LAUNCHER=ccache` and
`CMAKE_CXX_COMPILER_LAUNCHER=ccache` to cache both builds; the driver normalizes
source paths per worktree so base and head share entries.

Scenes wait until the framebuffer matches the requested size (at most 60
frames on every platform), then render and capture, with fixed geometry and a
fixed seed. There is one 256x192 logical window with vsync off. The resize scene
captures 256x192 and 320x224 with pixelPerfect both off and on, so a run produces
38 images. Captures and JSON sizes are framebuffer pixels. Screen assertions
scale their logical coordinates by DPI; FBO assertions use framebuffer pixels.
The text scene uses the embedded default bitmap glyphs and an R8 coverage page.

| Maximum RGBA8 channel difference | Scenes |
| --- | --- |
| 0 | blendModes, text, premultImage, fboClearLoad, scissorFbo, fullscreenShader, resizePixelPerfect, userShaderOtherFormat, readbackRoundTrip, midFrameFlush, midPassClear |
| 2 | msaaResolve, transformPbr, easyCamFbo, pbrShadowFbo, pointMeshTransform |

Tolerances live in the source table. A change to a tolerance needs an explanation
in its PR. Intentionally changed scenes need before/after images and sampled
values in the PR; every other changed pixel is a regression.

The PR 0 baselines intentionally include the existing FBO blend/premult, camera,
model and GPU-point behavior that PRs 2/4 will correct. Self-checks sample stable
known interiors alongside those features. The shadow scene keeps an Fbo active
across a nested shadow pass using a **test-only raw suspend/resume bridge**, since
the current public shadow implementation only suspends the swapchain. PR 3 should
replace that bridge with the target-stack operation and preserve the images.
Web is excluded. Windows/D3D11, macOS/Metal (including Retina) and Pi/GL comparisons
must run on those machines; Linux software rendering cannot verify them.
