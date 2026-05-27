# Audio Engine Verification Checklist

> ⚠️ **TEMPORARY DOCUMENT** — delete this file once `feature/sound-loadStream`
> has been verified on all target platforms and merged to `dev`. This is
> working notes for the audio engine refactor, not permanent documentation.

Scope: verify the audio engine work on `feature/sound-loadStream` (live
re-init, `AudioSettings`, `audioOut`/`audioIn` listeners, `listDevices`,
`audioDeviceChanged`, MixMode + channelMap routing, stride-bug fix)
across all supported platforms before merging.

The core mixer / re-init / event code lives in `core/include/tc/sound/`
and goes through miniaudio, so we don't expect platform-specific code
changes — but each platform's audio backend (CoreAudio / WASAPI / ALSA /
AAudio / Web Audio) has its own quirks that can only be confirmed by
running.

## Status

| Platform | Backend | listDevices | Live re-init | audioOut listener | audioDeviceChanged | MixMode | Notes |
|----------|---------|:-----------:|:------------:|:-----------------:|:------------------:|:-------:|-------|
| macOS    | CoreAudio | ✅ | ✅ | ✅ | ✅ | ⏳ | Verified 67 random rate switches over 60s, 0 failures. Needed persistent `ma_context` to avoid `ma_device_uninit` hang on second cycle. |
| Windows  | WASAPI    | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | Pending. Check the macOS uninit-hang issue doesn't reappear on WASAPI. |
| Linux    | ALSA / PulseAudio | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | Pending. `listDevices` may show PulseAudio "sinks" vs ALSA HW devices differently. |
| iOS      | CoreAudio | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | Should behave like macOS; light check OK. |
| Android  | AAudio    | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | AAC stays a stub; only verify playback/listener path. |
| Web      | Web Audio | ⏳ | n/a | ⏳ | n/a | ⏳ | `loadStream` already falls back to eager. Device switching not meaningful here. |

Legend: ✅ verified · ⏳ pending · ❌ broken (write a follow-up issue/commit) · n/a not applicable on this platform

## Per-platform test plan

For each target platform, run the following from the worktree:

### 1. Build the demo apps

```sh
cd examples/sound/audioDeviceExample
trusscli update                 # if cmakepresets is stale
trusscli build
```

Also build `soundPlayerExample`, `soundStreamExample`, `audioSynthExample`
and confirm they all link and start cleanly.

### 2. Manual smoke test — `audioDeviceExample`

1. Launch — confirm sine wave starts at 440 Hz on the system default
   playback device.
2. The ImGui panel should list at least the system default; "(default)"
   marker on the right device.
3. Click each of `44100 Hz` / `48000 Hz` / `96000 Hz` in turn. Sine
   should continue across each switch with a brief gap (~30–100 ms).
4. Click a non-default device (if any) → confirm sine routes to it.
5. Watch the "Last audioDeviceChanged" line — should update on every
   click with the resolved device name + flags.
6. Hit `Refresh device list` and confirm no crash / no duplicates.

### 3. MCP-driven stress test

```sh
TRUSSC_MCP=1 TRUSSC_MCP_PORT=8765 ./bin/audioDeviceExample.app/Contents/MacOS/audioDeviceExample &
# (adjust path per platform — on Windows it's bin/audioDeviceExample.exe)

# 30 random sample-rate switches, 1 second apart
RATES=("44100 Hz" "48000 Hz" "96000 Hz")
for i in $(seq 1 30); do
    R="${RATES[$((RANDOM % 3))]}"
    curl -s -X POST http://localhost:8765/mcp -H "Content-Type: application/json" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$i,\"method\":\"tools/call\",\"params\":{\"name\":\"imgui_click\",\"arguments\":{\"label\":\"$R\"}}}" > /dev/null
    sleep 1
done

# Quit
curl -s -X POST http://localhost:8765/mcp -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":99,"method":"tools/call","params":{"name":"quit","arguments":{}}}'
```

Check the app's stderr/logs:
- Expected: 30 × `AudioEngine: re-initializing ...` + matching
  `AudioEngine: initialized ...` lines, 30 × `audioDeviceChanged` fires.
- Failure signs: any line containing `failed to initialize`, app hang,
  process exit before 30 seconds elapse, missing `audioDeviceChanged`
  events for successful inits.

### 4. Channel routing test (once commit C lands)

(To be filled in once `setMixMode` / `setChannelMap` / `setChannelGains`
are merged — TODO.)

### 5. Regression check on existing examples

Re-run with the new build:
- `soundPlayerExample` — eager AAC, loop, ±10 speed, reverse playback.
- `soundStreamExample` — streaming WAV, loop, [0, 10] speed.
- `audioSynthExample` — sine via `audioOut` listener.
- `audioStreamTests/` (if synced) — 7 format coverage.

All should sound identical to the macOS baseline.

## Known platform issues / things to watch

- **macOS CoreAudio**: `ma_device_stop` blocks on second cycle if called
  before `ma_device_uninit`. Fixed by skipping the explicit stop —
  `ma_device_uninit` handles teardown internally. See commit
  `0bb31c47` for details.
- **macOS aggregate / virtual devices**: opening `MOTIV Mix Virtual`,
  `NDI Audio` etc. may trigger a mic-permission prompt that blocks
  `ma_device_init`. Not a code bug; deferred to "Audio device hot-plug
  handling" ROADMAP entry.
- **WASAPI**: unknown whether the same uninit cycle behavior applies.
  Run the MCP stress test to confirm.
- **PulseAudio**: init/uninit may be slower than CoreAudio (async
  protocol). Watch for visible UI stutter during rapid switches.
- **AAudio**: lifetime model differs from CoreAudio. Persistent
  `ma_context` reuse is unverified.
- **Web**: `audioDeviceChanged` will fire on initial init only;
  device-switching is a no-op (single default device). `loadStream`
  falls back to `load`, which is intentional.

## Removal

Delete this file as part of the PR that finishes the verification work
(after all rows are ✅ or with explicit follow-up entries). Don't merge
this checklist to `dev` permanently — the permanent reference is
`docs/AI_AUTOMATION.md` for MCP and the ROADMAP entries for outstanding
work.
