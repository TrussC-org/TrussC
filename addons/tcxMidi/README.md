# tcxMidi

MIDI input/output for TrussC, backed by [libremidi](https://github.com/celtera/libremidi)
(CoreMIDI / WinMM / ALSA / WebMIDI). Send and receive notes, control changes,
program changes, pitch bend and sysex, with both an event-callback and a
thread-safe polling API.

## Dependency

libremidi is fetched automatically at configure time via CMake `FetchContent`
(release tarball + SHA256 — **no `git` required**). No manual install.

Per-platform system requirements:

| Platform | Backend  | Status | Extra install / notes |
|----------|----------|--------|-----------------------|
| macOS    | CoreMIDI | ✅ tested (in/out on a real device) | none (bundled with the OS) |
| Web      | WebMIDI  | ✅ tested in Chrome (input; **no sysex**) | Chromium + https |
| Windows  | WinMM    | ✅ tested (in/out + sysex on a real device) | none |
| Linux    | ALSA     | ⚠️ untested (expected to work) | `libasound2-dev` |
| iOS      | CoreMIDI | ⚠️ untested (auto-enabled; BLE MIDI needs a Bluetooth usage description) | none |
| Android  | AMidi    | 🧪 experimental — enumerates but open is broken (see Android note) | **API 31+** + manifest entries |

JACK / PipeWire / network backends are disabled by default to keep the
dependency footprint small (toggle the `LIBREMIDI_NO_*` options in
`CMakeLists.txt` to re-enable them).

## Quick start

```cpp
#include <tcxMidi.h>
using namespace tcx;

MidiIn  in;
MidiOut out;

void setup() {
    for (auto& d : MidiIn::listDevices())     // enumerate input ports
        logNotice() << d.getPortNumber() << ": " << d.getName();
    in.openPort(0);                   // or in.openPort("Launchpad")
    out.openPort(0);

    // Event API (callback fires on the backend thread — guard shared state!)
    in.onMessage.listen([](MidiMessage& m){
        logNotice("midi") << m.toString();
    });
}

void update() {
    // Polling API (thread-safe, runs on the main thread)
    MidiMessage m;
    while (in.getNextMessage(m)) {
        if (m.isNoteOn()) { /* m.getPitch(), m.getVelocity(), m.getChannel() */ }
    }
}

void draw() {
    out.sendNoteOn(1, 60, 100);       // channel 1, middle C, velocity 100
    out.sendControlChange(1, 7, 64);  // CC#7 (volume)
}
```

## API summary

Enumeration follows the TrussC convention (`AudioEngine::listDevices()` etc.):
`listDevices()` returns `std::vector<MidiDeviceInfo>`, where each
`MidiDeviceInfo` has `getPortNumber()` / `getName()`.

Port lists and `openPort(index|name)` include virtual ports exposed by the OS,
including this application's own virtual ports. Prefer opening by name: indices
shift when ports appear or disappear (name matching uses the first
case-sensitive substring match, so choose a distinctive name).
CoreMIDI and ALSA sequencer enable libremidi's virtual-port tracking. WinMM,
WinUWP and WebMIDI ignore that flag and list whatever their OS/browser API
exposes; on Windows use a virtual MIDI driver such as loopMIDI. WebMIDI still
requires browser permission and `midiReady()`.

**`MidiIn`** — `listDevices()`, `openPort(index|name)`, `openVirtualPort(name)`,
`closePort()`, `isOpen()`, `getName()` (open port), `getPort()` (open port
number, -1 if virtual), `isVirtual()`, `ignoreTypes(sysex, timing, sensing)`,
`onMessage` event, and the polling API `hasNewMessage()` / `getNextMessage()` /
`setBufferSize()` / `getDroppedMessageCount()`. The polling queue holds up to 1024
messages by default and has a fixed **64 MiB retained-storage budget**, allocated
on demand. The charge includes message objects, byte-vector capacity, copied
port-name storage, and queue nodes. Oldest whole messages are evicted before
admission, using the copy's measured capacity; a single over-budget message is
rejected whole. `setBufferSize(0)` keeps zero messages; shrinking discards the
oldest whole messages. These losses count in `getDroppedMessageCount()` since
construction; querying and close/reopen do not reset the total. Inline events
are independent of queue eviction.

The first queue loss automatically logs one Warning on the main thread, naming
the store, limit, needed amount and units, even without another poll. Its once
gate survives close/reopen. Drain more often, or raise `setBufferSize()` for
message-count pressure; the storage budget remains fixed. Backend assembly and
main-thread event delivery are separate stores from this polling queue.

`setBufferSize()` / `getBufferSize()` reason: device input or a stalled reader can
fill this queue; finite count and storage bounds keep memory finite. Overflow
and shrinking evict oldest whole messages and increment the counter.
日本語: デバイス入力や停止した読み手によるメモリ増加を抑えるため、件数と保存量を制限します。
上限超過時と縮小時は古いメッセージを丸ごと破棄し、カウンターに加算します。
한국어: 장치 입력이나 멈춘 소비자로 인한 메모리 증가를 막기 위해 메시지 수와 저장량을 제한합니다.
초과하거나 한도를 줄이면 오래된 메시지 전체를 버리고 카운터에 더합니다.


Native SysEx reception has a separate fixed limit of **1,048,576 bytes including
F0/F7**. Larger messages are discarded whole and counted internally. The first
SysEx loss per `MidiIn` lifetime emits one **Warning on the main thread**, even
without polling. These losses do not change `getDroppedMessageCount()`, and
`setBufferSize()` does not raise the SysEx limit. WebMIDI does not enable SysEx.
ALSA output splits long SysEx into 256-byte events, drains each event with
bounded retries, and paces partial SysEx by 1 ms. Pacing and draining run on
**the calling thread**, at about 256 KB/s (roughly 80 times DIN MIDI). A 1 MB
SysEx takes about 4 s; calling `sendSysex` from `update()` or `draw()` stalls
that thread for the entire send. Drain retries and scheduling can add latency.
Private clients are opened nonblocking; supplied contexts retain their mode.
A blocking supplied handle can block in the kernel drain beyond the retry bound.
Supplied contexts concern direct libremidi configuration only; TrussC's own
MidiOut never supplies a context.
With immediate mock drains and the real ALSA codec, the 1,048,577-byte
host send measured **4,369 ms** (4,097 events); real-device timing can vary.
A send reports success when the sequencer accepts it, or a clear error when the
send itself fails; a failed send may have delivered a prefix. A receiver whose
input pool overflows loses data on its side; send success does not acknowledge
reception, as on the other backends. Other output backends are unchanged.
See [the local libremidi patch ledger](LIBREMIDI_TRUSSC_MODIFICATIONS.md).

**`MidiOut`** — `listDevices()`, `openPort(index|name)`, `openVirtualPort(name)`,
`getName()` / `getPort()` / `isVirtual()`, `sendNoteOn/Off`,
`sendControlChange`, `sendProgramChange`, `sendAftertouch`,
`sendPolyAftertouch`, `sendPitchBend` (14-bit, or raw `lsb,msb`), `sendSysex`,
`sendMidiByte`, `sendBytes`, `send(MidiMessage)`. All send helpers return `bool`:
`true` when libremidi accepts the message, `false` with a warning when no output
port is open, the message is empty, or the backend reports an error. Warnings are
logged once per reason each time the port is opened. Closing also resets the
warning gates. `true` does not guarantee delivery to the device.
Existing calls may ignore the result;
explicit member-function pointers must use a `bool` return type.

```cpp
if (!out.sendNoteOn(1, 60, 100)) {
    // The send failed; the warning log includes the reason.
}
```

**`MidiMessage`** — `bytes`, `deltatime` (seconds since previous event),
`portName`, `portNum`, plus `getStatus()`, `getChannel()` (1-16), predicates
`isNoteOn/Off`, `isControlChange`, `isPitchBend`, `isSysex`, and system ones
`isClock`/`isStart`/`isStop`/`isContinue`/`isSongPosition`/`isTimeCode`;
data accessors `getPitch()`, `getVelocity()`, `getControl()`, `getValue()`,
`getPitchBend()`, `getSongPosition()`; `toString()` / `getStatusString()`.

**`MidiClock`** — feed `update(MidiMessage)`; read `getBpm()`, `getBeats()`
(MIDI beats = 16th notes), `getSeconds()`. Tracks tempo from 0xF8 ticks and
song position from SPP.

**`MidiTimecode`** — feed `update(MidiMessage)`; when it returns true, read
`getFrame()` → `MidiTimecodeFrame { hours, minutes, seconds, frames, rate }`
with `toString()` / `getFps()` / `toSeconds()`. Decodes both MTC quarter-frame
and full-frame SysEx.

`MidiStatus` now distinguishes every system message (Sysex, TimeCode,
SongPosPointer, TimeClock, Start, Continue, Stop, ActiveSensing, …), not just a
single lumped Sysex.

**`midiReady()`** — free function; gate port enumeration/opening on it (always
true on native, async on the web — see the Web note below).

Channel numbers are **1-16** throughout.

## Threading note

`MidiIn::onMessage` is invoked on libremidi's input thread, **not** the main
(update/draw) thread. Guard any shared data with a mutex, or use the polling
API and drain `getNextMessage()` from `update()` (what the examples do).

## Windows note

The backend is **WinMM**. A USB device that exposes several MIDI ports (e.g. the
Launchpad Mini Mk3's *DAW* + *MIDI* ports) appears as one port per jack, and
WinMM renames every port after the first to `MIDIIN2 (<name>)` /
`MIDIOUT2 (<name>)`. The index and parentheses are added by **Windows, not the
device**, and the original jack labels (e.g. "DAW" / "MIDI") are lost — so
`openPort(name)` matching that works on macOS/Linux may need a different string
on Windows. WinMM also truncates port names to **31 characters**. When in doubt,
print `listDevices()` and match on what you actually see.

loopMIDI drops SysEx larger than its SysexSize setting (KiB, maximum 512) without an error to either side, so the port check's over-cap step cannot run through loopMIDI.

## Web (Emscripten) note

Web MIDI works in **Chromium-based browsers over https** (Safari/Firefox have
no Web MIDI). It is **asynchronous**: `navigator.requestMIDIAccess()` resolves a
frame or more after launch, and touching libremidi before then crashes. Don't
open ports in `setup()` — gate on `midiReady()` from `update()`:

```cpp
if (!started_ && midiReady()) { started_ = true; midiIn_.openPort(0); }
```

`midiReady()` is always true on native, so the same code runs everywhere (the
examples all use this pattern). On the web the access is requested **without
sysex**, so sysex (e.g. Launchpad Programmer mode) is native-only.

## Android note

> **Status: experimental.** Device *enumeration* works (`listDevices()` returns
> the connected device), but *opening* a port currently fails inside libremidi's
> AMidi backend on recent Android: it can't find the `MidiDeviceCallback` class
> from its native thread (JNI classloader) and reports empty port names. So
> Android builds and enumerates, but send/receive isn't functional yet — it
> needs upstream libremidi fixes. The notes below are for when that lands.

libremidi's Android backend uses **AMidi**, which requires **API level 31+**
(Android 12+) and a small Java glue class. tcxMidi handles most of it:

- The Java glue (`dev.celtera.libremidi.MidiDeviceCallback`) ships in
  `tcxMidi/android/java/` and is compiled into the APK automatically by TrussC's
  addon aggregation — you don't add it yourself.
- The MIDI manifest lines ship in `tcxMidi/android/manifest/uses.xml`. They are
  **not** injected automatically: at configure time TrussC collects them into
  `build-android/REQUIRED_MANIFEST_SNIPPET.xml` and prints a NOTICE. Paste the
  ones you need (MIDI feature, USB host, BLE permissions) into your app's
  `AndroidManifest.xml`.

You must set the build to API 31+ yourself: trusscli currently hardcodes
`ANDROID_PLATFORM: android-26` in `CMakePresets.json`, so bump it to
`android-31` (the bundled `example-basic` preset is already set to 31). Building
on android-26 fails configure with *"AMidi API requires API level 31+"*.

BLE MIDI additionally needs the `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT` runtime
permissions requested at runtime, and the device paired/connected at the OS
level (raw CoreMIDI/AMidi sees it once the system has connected it).

## Examples

Generic examples (work with any MIDI device):

- **example-basic** — the smallest usage and the most universal tool: opens the
  first in/out port, shows a scrolling history of incoming messages, SPACE
  sends a note. Watch the log to learn what each control on your device sends.
- **example-async** — the same monitor, but driven asynchronously by the
  `onMessage` event (callback on the MIDI thread) instead of polling, showing
  the correct mutex-guarded pattern for handling messages off the main thread.
- **example-keyboard** — a piano-keyboard visualizer. Note numbers are standard
  pitches, so held keys light up the same on any keyboard (velocity = brightness).
- **example-controller** — knob/fader monitor: every CC number that arrives gets
  a labelled meter that animates to its value. Move a control, a bar appears.

Device-specific demos live in their own repos (they only make sense with that
hardware):

- **[trussc-launchpadMiniMk3-demo](https://github.com/tettou771/trussc-launchpadMiniMk3-demo)**
  — Novation Launchpad Mini Mk3: paint / sequencer / ripple modes with LED
  feedback and ChipSound audio.

## License

MIT (same as TrussC). libremidi is BSD-2-Clause.
