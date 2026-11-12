# #1422 measurement scratch (throwaway branch, never merged)

Raspberry Pi 5 (Mesa V3D, GLES). Assumes already installed: a C++17 compiler, Python 3, and EGL / GLESv2 / X11 development packages. Install nothing; if something is missing, stop and report it.

## Extension check (#1422 clip control) — run this first

```sh
cd inv-1422-measure
c++ -std=c++17 -O2 -Wall -Wextra -Werror fbo_measure.cpp -o fbo_measure -lEGL -lGLESv2 -lX11
export DISPLAY=:0          # the Pi's own desktop session (use its actual display number)
unset LIBGL_ALWAYS_SOFTWARE GALLIUM_DRIVER
./fbo_measure --gles --extensions > PI_EXTENSIONS.log 2>&1; cat PI_EXTENSIONS.log
```

Send back PI_EXTENSIONS.log. The answer we need: does it list `GL_EXT_clip_control`, and what are GL_VERSION / GL_RENDERER (must be V3D, not llvmpipe)?

## Exact commands (run on the Pi, in a desktop session backed by V3D)

```sh
git fetch origin inv/1422-measure && git checkout inv/1422-measure   # or any checkout of this branch
cd inv-1422-measure
c++ -std=c++17 -O2 -Wall -Wextra -Werror fbo_measure.cpp -o fbo_measure -lEGL -lGLESv2 -lX11 > PI_BUILD.log 2>&1
export DISPLAY=:0          # the Pi's own desktop session (use its actual display number)
unset LIBGL_ALWAYS_SOFTWARE GALLIUM_DRIVER
./fbo_measure --gles --smoke-only > PI_SMOKE.log 2>&1; cat PI_SMOKE.log
# require: DEVICE ... renderer=V3D, four SMOKE lines all PASS, COMPLETE gl_error=0 (stop otherwise)
for run in 1 2 3; do ./fbo_measure --gles --rounds 15 --batch 3 > PI_GLES_$run.log 2>&1 || exit 1; done
python3 summarize.py PI_GLES_1.log PI_GLES_2.log PI_GLES_3.log > PI_MEASUREMENT.csv
{ uname -a; vcgencmd get_throttled; vcgencmd measure_temp; } > PI_SYSTEM.log
```

Send back: PI_BUILD.log, PI_SMOKE.log, PI_GLES_1..3.log, PI_MEASUREMENT.csv, PI_SYSTEM.log, plus OS/Mesa version, RAM, power/cooling and display setup.

## Full brief (from the Linux worker)

The commands above supersede the ones below: on the Pi, build with plain `c++` (no tcbuild.sh) and use this directory instead of the tarball.

Run the same scratch benchmark on a Raspberry Pi 5 with Mesa V3D GLES. The Linux llvmpipe results do not predict Pi performance; this run decides whether E's publication copy and memory cost fit the intended workload. A's result measures only a cooperating reflected writer, not a complete framework solution for arbitrary shaders.

Transfer `MEASUREMENT_SCRATCH.tar.gz` to the Pi using your existing offline transfer method. It contains source and Python scripts only. Use an existing X11/Xwayland display on the Pi that is backed by V3D, with its explicit display number and normal session authorization. Do not use Xvfb or force software rendering for the hardware run. Required tools/headers/libraries must already be installed: C++17 compiler, Python 3, EGL, GLES3, GLESv2, X11, and the unmodified `tcbuild.sh` build guard. If any are missing, stop and report them; do not download anything. This probe has no CMake/FetchContent dependencies.

```sh
mkdir -p fbo-1422-measure
cd fbo-1422-measure
tar -xzf ../MEASUREMENT_SCRATCH.tar.gz
# Set TCBUILD to the existing build guard if it is not on PATH.
TCBUILD="${TCBUILD:-tcbuild.sh}"
"$TCBUILD" c++ -std=c++17 -O2 -Wall -Wextra -Werror \
  ./fbo_measure.cpp -o ./fbo_measure \
  -lEGL -lGLESv2 -lX11 > PI_BUILD.log 2>&1
# Set this to the Pi's existing hardware-backed session; example number only.
export DISPLAY=:2
unset LIBGL_ALWAYS_SOFTWARE GALLIUM_DRIVER
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
./fbo_measure --gles --smoke-only > PI_SMOKE.log 2>&1
cat PI_SMOKE.log
```

Require `DEVICE api=GLES ... renderer=V3D ...`, four `SMOKE` lines (E/A × samples 1/4), all with `canonical=PASS nested=PASS scissor=PASS culling=PASS window=PASS chain_1_to_6=PASS pixels=PASS`, and `COMPLETE gl_error=0`. Stop on a nonzero exit, `FAIL`, llvmpipe/softpipe, an unsupported RGBA16F render target, or unavailable 4× MSAA; send the log without silently substituting another format/sample count. The window check uses the EGL default pbuffer, not a visible on-screen capture.

With other GPU workloads stopped and the Pi cooled to a stable state, run three repetitions:

```sh
for run in 1 2 3; do
  ./fbo_measure --gles --rounds 15 --batch 3 > "PI_GLES_$run.log" 2>&1 || exit 1
done
python3 summarize.py PI_GLES_1.log PI_GLES_2.log PI_GLES_3.log > PI_MEASUREMENT.csv
uname -a > PI_SYSTEM.log
vcgencmd get_throttled >> PI_SYSTEM.log
vcgencmd measure_temp >> PI_SYSTEM.log
```

Each run must contain 15 `RESULT` lines, five `MEMORY` lines, four passing smoke lines and `COMPLETE gl_error=0`. Each mode/case has 45 measured frames: 15 rotating baseline/E/A blocks, two untimed warmups per block, three timed frames per block. A fixed asymmetric quad is rendered; one `glFinish` ends each timed frame. Reported median and p95 use nearest-rank percentiles; allocation and pixel readback are outside timing.

Send `PI_BUILD.log`, `PI_SMOKE.log`, all three full run logs, `PI_MEASUREMENT.csv`, `PI_SYSTEM.log`, the OS/Mesa version, RAM size, power supply/cooling, display setup, and any throttling. For each of the five cases send baseline/E/A median and p95 milliseconds, E−baseline and A−baseline milliseconds and percentages (CSV includes median percentages). Keep all repetitions so variability remains visible. `MEMORY` is calculated additional texture storage, not measured driver allocation: 7.91 / 63.28 / 7.91 / 15.82 / 25.00 MiB for the five cases. E adds one copy after each native resolve; the six-pass case uses two ping-pong targets. The owner should compare E's time/memory against the actual Pi frame budget, then weigh A's remaining writer/camera/scissor integration work. This probe alone does not choose a production mechanism.
