# videoRecExample

Records a rendered scene to an H.264 `.mp4` using TrussC's **native**
`VideoRecorder` — no ffmpeg binary, no extra dependency. Each platform uses its
own media stack:

| Platform | Backend |
|----------|---------|
| macOS    | AVFoundation (`AVAssetWriter`) |
| Windows  | Media Foundation (`IMFSinkWriter`) |
| Linux    | GStreamer (`appsrc → encoder → mp4mux`) |

## Controls

- `r` — start / stop recording
- `m` — switch source between the **clean Fbo** and the **whole window** (only while idle)

Two output files are written to `bin/data/`:

- `videoRec_clean.mp4` — the offscreen Fbo only (no on-screen text)
- `videoRec_screen.mp4` — the whole window, overlay included

`VideoRecorder::start()` auto-captures every frame via the `afterFrame` hook
until `close()`, so there is no per-frame `addFrame()` call in the sample.

## Linux: automatic encoder selection & fallback

On Linux there is no single OS encoder API like AVFoundation/Media Foundation,
so the recorder builds a GStreamer pipeline and **picks the encoder at runtime**.
The goal: use the most efficient path that actually works on the machine, and
never fail where software encoding is possible.

### Selection order

Candidates are tried **hardware-first**, software last:

1. `nvh264enc` — **NVIDIA NVENC** (dedicated encode ASIC; preferred when present)
2. `vah264lpenc` / `vah264enc` — **Intel/AMD VA-API** (modern `va` plugin)
3. `vaapih264enc` — legacy `gstreamer-vaapi` (needs a `vaapipostproc` upload)
4. `v4l2h264enc` — **V4L2** stateful HW encoder (Raspberry Pi 3/4 etc.)
5. `x264enc` — **software** (libx264); the portable default
6. `openh264enc` — software fallback

### How fallback stays safe

A hardware encoder element can *exist* yet fail to initialize (missing driver,
unsupported resolution, busy GPU). So before committing to one, the recorder
runs a **tiny probe pipeline** (`videotestsrc → encoder → fakesink`) at the
actual recording resolution. Only an encoder that reaches EOS without error is
chosen; otherwise the next candidate is tried. The software encoders always
work where their plugin is installed, so recording never silently produces an
empty file.

The chosen encoder is logged, e.g.:

```
[NOTICE] [VideoRecorder] selected hardware H.264 encoder: nvh264enc
[NOTICE] [VideoRecorder] GStreamer encoder: nvh264enc (hardware)
```

> The probe runs at the real width/height on purpose: NVENC (and some HW
> encoders) reject frames below a minimum size, so a fixed tiny probe would
> falsely reject a working encoder.

All backends emit **H.264 4:2:0** (`profile=main`/`high`). The pipeline forces
`I420` before the software encoders, because x264enc would otherwise negotiate
`high-4:4:4` straight from RGBA — which many players, browser `<video>` tags and
hardware decoders can't decode.

### Required GStreamer plugins

`gstreamer-1.0` + `gstreamer-app` are already TrussC dependencies. The encoders
live in optional plugin packages — install the ones matching your hardware:

| Encoder | Debian/Ubuntu package | Arch package |
|---------|-----------------------|--------------|
| `x264enc` (software, default) | `gstreamer1.0-plugins-ugly` | `gst-plugins-ugly` |
| `openh264enc` (software) | `gstreamer1.0-plugins-bad` | `gst-plugins-bad` |
| `nvh264enc` (NVIDIA) | `gstreamer1.0-plugins-bad` + NVIDIA driver | `gst-plugins-bad` + `nvidia-utils` |
| `vah264lpenc` / `vaapih264enc` (Intel/AMD) | `gstreamer1.0-vaapi` | `gstreamer-vaapi` |
| `v4l2h264enc` (Raspberry Pi) | `gstreamer1.0-plugins-good` | — |

`tools/install_dependencies_linux.sh` installs the software default
(`plugins-ugly`) plus `-good`/`-bad`; add the VA-API/NVIDIA packages above for
hardware encoding.

### Troubleshooting

- **A hardware encoder you expect isn't picked.** Check it registers:
  `gst-inspect-1.0 nvh264enc` (or `vah264lpenc`). If it's missing or shows
  `0 features`, the plugin couldn't load its driver library.
- **NVENC missing right after installing the driver / plugin.** GStreamer
  caches its plugin registry; a stale cache can keep reporting the old
  "library not found" result. Clear it and rescan:
  ```
  rm -f ~/.cache/gstreamer-1.0/registry.*.bin
  gst-inspect-1.0 nvcodec
  ```
- **Raspberry Pi 5** has no hardware H.264 encoder (Broadcom dropped the block),
  so it uses software `x264enc`. Pi 3/4 expose `v4l2h264enc`.
- **Inspect a recording:** `gst-discoverer-1.0 bin/data/videoRec_clean.mp4`.
