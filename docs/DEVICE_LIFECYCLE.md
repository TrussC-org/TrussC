# Device lifecycle

Classes that open a device (a serial port, a camera, a MIDI port, a depth camera, an audio device) follow one small convention. In v0.8.0, `Serial`, `DepthCamera`, `VideoGrabber`, `MidiIn` / `MidiOut`, `AudioEngine` and `MicInput` follow it.

## The common surface

| Member | Meaning |
|---|---|
| `static listDevices()` | List the devices (each class keeps its own form) |
| `setup(...)` | Open a device; the arguments are those of the class (path and baud, width and height, ...). MIDI opens with `openPort()` / `openVirtualPort()`, `MicInput` with `start()`, and `DepthCamera` has only `setup()` |
| `bool setup()` | Open again with the last settings (see rule 2 for which device) |
| `bool isConnected() const` | True only while the device is open |
| `void close()` | Close the device |
| `Event<DeviceDisconnectEventArgs> onDisconnect` | The OS reported the device lost; `e.reason` says why |

Older per-class names (`isInitialized`, `isOpen`, `closePort`, ...) stay as aliases of `isConnected()` and `close()`.

## The five rules

1. `isConnected()` is true only while the device is open.
2. `setup()` with no arguments opens again with the last settings. `Serial`, `VideoGrabber`, MIDI and `DepthCamera` reopen the last device; `AudioEngine` and `MicInput` open the system default device. On failure it returns `false` and logs an error.
3. `close()` closes the device and never fires `onDisconnect`.
4. `onDisconnect` fires once, and only when the OS, the SDK or the library reports the device lost. `isConnected()` is already false when it fires. Which thread it fires on depends on the class (see [Threads](#threads)).
5. Nothing fires from the destructor.

A device is an ordinary member object, closed by its destructor. Opening the same physical device twice is not refused. There is no waiting or retrying when no device is there at startup. Where a platform does not tell us about a loss, the class documentation says so.

## Reconnecting is app code

There is no automatic reconnect. Poll in `update()`, with the elapsed-time idiom of your choice:

```cpp
// tcApp.h
Serial serial;
float lastTry = 0.0f;

// tcApp.cpp
void tcApp::update() {
    if (!serial.isConnected() && getElapsedTime() - lastTry > 2.0f) {
        lastTry = getElapsedTime();
        serial.setup();   // same port, same baud rate as before
    }
}
```

Or call `setup()` from the `onDisconnect` listener. See `examples/communication/serialExample`.

## Threads

The event fires on the thread that found the loss. For `VideoGrabber`, MIDI, `AudioEngine` and `MicInput` that is always the main thread. For `Serial` it is the thread whose read or write found the loss, and for `DepthCamera` the thread that called `reportLost()`. A listener that must run on the main thread whichever thread uses the device registers with `Deliver::Main`, as for network events:

```cpp
EventListener listener = serial.onDisconnect.listen([](DeviceDisconnectEventArgs& e) {
    logWarning() << "serial lost: " << e.reason;
}, Deliver::Main);
```

`DeviceDisconnectEventArgs` is not `DeviceLostEventArgs`, which belongs to the graphics device.

## Backend authors (DepthCamera)

`DepthCamera` (the `tcxDepthCamera` addon) provides the whole surface in its base class, over the existing `openDevice()` hook: `setup()` reopens through `openDevice()` with the settings the backend remembers, `isConnected()`, `close()` and `onDisconnect` are there already, and a backend written for the v0.8 interface compiles unchanged. The base fires `onDisconnect` only when a backend reports a loss, so a backend that does nothing never fires it. Where a backend already detects the loss (an SDK callback, a capture that says "device removed"), it adds one line, on any thread:

```cpp
reportLost("Orbbec: device removed");   // protected; fires onDisconnect once, then isConnected() is false
```

After a loss the session keeps its resources until the app calls `close()` or `setup()`, so the backend's `captureInto()` should return an empty frame once the device is gone. Where the SDK does not report a loss, the backend's documentation says so.

## Status

| Class | State |
|---|---|
| `Serial` | Follows the convention (v0.8.0) |
| `DepthCamera` (`tcxDepthCamera`) | The base follows the convention (v0.8.0); each backend adds its `reportLost()` line where its SDK reports a loss |
| `VideoGrabber` | Follows the convention (v0.8.0). `update()` reports the loss; see the class notes for what each OS tells us |
| `tcxMidi` (`MidiIn`, `MidiOut`) | Follows the convention; loss is reported on CoreMIDI, ALSA sequencer and WinMM, not on WebMIDI (see [addons/tcxMidi/README.md](../addons/tcxMidi/README.md)) |
| `AudioEngine`, `MicInput` | Follow the convention (v0.8.0): `setup()` / `isConnected()` / `close()` / `onDisconnect`. `AudioEngine::setup()` and `init()` open the system default device (to use a specific device, pass `AudioSettings` with `deviceName`), and `MicInput::setup()` opens the default capture device at the last sample rate; any reopen of a closed engine, including `Sound::load*()`, does the same. The audio thread only records a stop; the event fires on the main thread. An `AudioEngine` device opened by name is closed before the event fires (no automatic fallback; to keep playing on the system default, call `setup()` inside the listener). An `AudioEngine` device opened as the system default follows the OS default and never reports a loss; if every device disappears, the sound just stops (and resumes when the OS has a default again). Apps that need to know open the device by name. `MicInput::onDisconnect` fires when the capture device stops, which on Windows includes a change of the default microphone; call `setup()` in the listener to reopen the default microphone. Which backends report a stop depends on the OS (see the `onDisconnect` cards). |
