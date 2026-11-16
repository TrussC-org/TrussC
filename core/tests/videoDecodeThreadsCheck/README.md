# Linux decoder thread device check

This opt-in app is an own-binary core test with `src/main.cpp`, `src/tcApp.h`
and `src/tcApp.cpp`, like emptyExample. With no arguments it returns 77 (SKIP),
which the core build sweep accepts. The display runner explicitly skips this
manual check. Configure and build it separately, then run on a real GPU display:

Generate the usual ignored CMake project with `trusscli update -p core/tests/videoDecodeThreadsCheck` if needed.

```sh
tcbuild.sh cmake -S core/tests/videoDecodeThreadsCheck -B core/tests/videoDecodeThreadsCheck/build-linux -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_FULLY_DISCONNECTED=ON
tcbuild.sh cmake --build core/tests/videoDecodeThreadsCheck/build-linux --config Release
core/tests/videoDecodeThreadsCheck/bin/videoDecodeThreadsCheck /path/to/clip.mp4 4 1 1 4
```

Arguments: clip, player count, decode thread preference, hardware on (`1`) or off
(`0`), optional playback seconds (default `4`). Each invocation is a new process,
so `/proc/self/status`'s `VmHWM` is a per-run peak. All players loop, update and
draw in a grid. Output includes the requested thread preference, actual reported
`isUsingHwAccel()` / `getHwAccelName()`, and delivered `isFrameNew()` updates
versus elapsed seconds times nominal frame rate, per player. Delivered counts
measure application presentation opportunities, not every frame decoded inside
FFmpeg; small differences at startup and loop boundaries are expected. There is
no performance pass threshold. Load/playback errors fail the check.

There is no public API or environment setting in this backend to force CUDA
versus VAAPI. It tries CUDA first, then VAAPI, VDPAU, V4L2M2M and DRM. Inspect
`getHwAccelName` in every result. A VAAPI check needs an environment where CUDA
device creation is unavailable and VAAPI succeeds; otherwise report VAAPI as
unverified. The app prints the selected backend at load and at completion.

Use the matrix in `PR_DRAFT.md` on trussc-gdm. Software results under Xvfb or
llvmpipe do not settle the real-display contention question.
