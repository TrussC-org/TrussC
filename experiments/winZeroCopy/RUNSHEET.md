## Windows RUN SHEET

This is a throwaway branch experiment, not a production rollout. On the owner's machine: no installs, settings changes, downloads, driver changes, or device-loss/TDR tests. Use the existing SDK/compiler and installed codecs. If anything is missing, report it and stop that step.

1. Record branch revision, Windows build, GPU/adapter and driver, logical CPU count, display resolution/refresh, window size and observed window fps, existing power mode, clip codecs/frame rates/durations, and decoder choice if observable. Keep the same settings in both modes; do not change vsync/device/debug settings.
2. Configure/build the harness with the existing Windows tools from the repository root:

   ```powershell
   cmake -S experiments/winZeroCopy -B experiments/winZeroCopy/build-windows -DFETCHCONTENT_FULLY_DISCONNECTED=ON
   cmake --build experiments/winZeroCopy/build-windows --config Release
   ```

   If the Windows orchestration uses tcbuild.sh, run these through that existing build wrapper. Do not run trusscli generate/update/regenerate. If configure/build fails, return the full errors verbatim, including the first compiler error. Do not install missing tools or replace the pinned shader compiler.
3. Prefer existing local SDR color-bar clips of known frame rate, 60 s each: 1920×1080 H.264, 3840×2160 H.264, 7680×4320 HEVC. If ffmpeg is already installed, generate them locally (no download):

   ```powershell
   ffmpeg -f lavfi -i "smptehdbars=size=1920x1080:rate=30" -t 60 -c:v libx264 -pix_fmt yuv420p bars-1080-h264.mp4
   ffmpeg -f lavfi -i "smptehdbars=size=3840x2160:rate=30" -t 60 -c:v libx264 -pix_fmt yuv420p bars-4k-h264.mp4
   ffmpeg -f lavfi -i "smptehdbars=size=7680x4320:rate=30" -t 60 -c:v libx265 -pix_fmt yuv420p bars-8k-hevc.mp4
   ```

   Use only encoders already available. If 8K HEVC cannot be generated/decoded, omit that row and explain. Record actual source rate; use absolute local clip paths. Set the executable path for the existing build layout, normally `experiments/winZeroCopy/bin/winZeroCopy.exe`.
4. Each clip × mode {baseline, zerocopy} × players {1, 4}: one discarded warmup run, then three separate samples with `--warmup 5 --seconds 60`. Use a unique --out filename per sample; the harness overwrites that file. Keep the same 1280×720 window/display. Example:

   ```powershell
   $app = ".\experiments\winZeroCopy\bin\winZeroCopy.exe"
   Remove-Item Env:TRUSSC_EXP_WIN_ZEROCOPY -ErrorAction SilentlyContinue
   $p = Start-Process -FilePath $app -ArgumentList '--clip "C:\clips\bars-4k-h264.mp4" --players 1 --warmup 5 --seconds 60 --out baseline-4k-p1-r1.csv' -Wait -PassThru -NoNewWindow; $p.ExitCode
   $env:TRUSSC_EXP_WIN_ZEROCOPY = "1"
   $p = Start-Process -FilePath $app -ArgumentList '--clip "C:\clips\bars-4k-h264.mp4" --players 1 --warmup 5 --seconds 60 --out zerocopy-4k-p1-r1.csv' -Wait -PassThru -NoNewWindow; $p.ExitCode
   ```

   The target defines TRUSSC_SHOW_CONSOLE, including MSVC Release. Launch every run (discarded warmup, performance sample, open/close, screenshot, and trace) with this Start-Process pattern so it finishes before the next starts. Record each $p.ExitCode; a nonzero value invalidates that sample. Repeat for r2/r3 and all combinations. A fallback warning means requested zerocopy actually used baseline; preserve the warning and mark the case unsupported rather than claiming a speedup. If a combination cannot sustain playback or runs out of resources, report that limitation.
5. Per mode, run `--openclose 20`. The harness first loads all selected players, plays 2 s, closes, and waits two frame boundaries before taking private_bytes_warm. It then performs K=20 additional cycles, each playing 2 s and waiting two frame boundaries after close. It takes private_bytes_mid after cycle floor(K/2) and private_bytes_after after cycle K, all in the same closed, drained state, before playback warmup/measurement. For K=1 the midpoint is cycle zero (the warm value). At minimum use the 4K clip with 1 and 4 players. Preserve private_bytes_warm/private_bytes_mid/private_bytes_after in CSV and console. For example, with the desired mode environment already set:

   ```powershell
   $p = Start-Process -FilePath $app -ArgumentList '--clip "C:\clips\bars-4k-h264.mp4" --players 1 --openclose 20 --warmup 5 --seconds 60 --out openclose-4k-p1.csv' -Wait -PassThru -NoNewWindow; $p.ExitCode
   ```

   Use a unique output per mode/player count. The CPU value is process kernel+user time / measured wall / all logical CPUs ×100, repeated in every player row; do not sum it. This experiment records no GPU memory counters.
6. Take one color-bar screenshot per mode using TRUSSC_MCP `tc_get_screenshot` after the harness prints `READY_FOR_SCREENSHOT`. The harness pauses a live frame for 5 s after warmup, outside the measurement window, then resumes automatically. Capture during that hold; static color bars also remain visible during the 60 s sample. Do not manually change playback. Compare red/blue, grayscale, orientation, all tiles, and the initial poster/live transition. Keep screenshots and note the exact case.
7. For zerocopy 1-player 4K, set `TRUSSC_EXP_WIN_ZEROCOPY_LOG=1`, save stdout/stderr, and run a sample. Return the first sequence including mf-acquire(0), transfer, mf-release(1), sokol-acquire(1), GPU completion, sokol-release(0), plus any timeout/failure. Trace stops after transfer 120. Unset this variable for performance samples.
8. The existing MF device already tries DEBUG first. Record each `D3D11 device created with: ...` success line and any debugger/debug-layer messages. If the layer is not installed, use the existing fallback and state that it was inactive; do not install it. Do not change Sokol device flags.
9. Report back CSV files, screenshots, mutex log excerpt, debug-layer messages (if any), all warning lines, successful/failed cases, and full build errors if applicable. Tabulate per-player new-frame fps/drops and process CPU medians/ranges over three samples; include measured window fps and any source/display cap. Check for corruption, crashes/hangs, growing private bytes, and unexplained frame/CPU regressions. Seek/loop/resize and older/multi-vendor coverage not exercised by this fixed-clip harness remain explicit follow-ups; device loss is excluded.



## Update 2 (env switches)

- `TRUSSC_D3D11_DEBUG=1`: the video player's MF device tries the D3D11 debug layer. Unset (the default here) = no debug layer, so the fps/CPU matrix measures release behavior. Use it for the mutex trace and leak runs only.
- `TRUSSC_EXP_WIN_ZEROCOPY_GPUWAIT=1`: zerocopy waits on the CPU for GPU completion (event query) before releasing the key to MF. Unset (the default) = the keyed mutex alone orders the GPU work, no CPU wait. Run zerocopy 4K 1 player once with it set to compare.
- The harness sets the MCP tool level to ReadOnly, so `tc_get_screenshot` works.
