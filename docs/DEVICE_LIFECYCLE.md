# Device lifecycle

Classes that open a device (a serial port, a camera, a MIDI port, a depth camera, an audio device) follow one small convention. `Serial` is the first; the others follow (#415).

## The common surface

| Member | Meaning |
|---|---|
| `static listDevices()` | List the devices (each class keeps its own form) |
| `setup(...)` | Open a device; the arguments are those of the class (path and baud, width and height, port, ...) |
| `bool setup()` | Reopen the last device with the last settings |
| `bool isConnected() const` | True only while the device is open |
| `void close()` | Close the device |
| `Event<DeviceDisconnectEventArgs> onDisconnect` | The OS reported the device lost; `e.reason` says why |

Older per-class names (`isInitialized`, `isOpen`, `closePort`, ...) stay as aliases of `isConnected()` and `close()`.

## The five rules

1. `isConnected()` is true only while the device is open.
2. `setup()` with no arguments reopens the last device with the last settings. On failure it returns `false` and logs an error.
3. `close()` closes the device and never fires `onDisconnect`.
4. `onDisconnect` fires once, on the main thread (the usual off-thread delivery of the Runtime, in the owning App's context), and only when the OS, the SDK or the library reports the device lost. `isConnected()` is already false when it fires.
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

The event fires on the thread that found the loss, which is normally the main thread. A listener that must run on the main thread whichever thread uses the device registers with `Deliver::Main`, as for network events:

```cpp
EventListener listener = serial.onDisconnect.listen([](DeviceDisconnectEventArgs& e) {
    logWarning() << "serial lost: " << e.reason;
}, Deliver::Main);
```

`DeviceDisconnectEventArgs` is not `DeviceLostEventArgs`, which belongs to the graphics device.

## Status

| Class | State |
|---|---|
| `Serial` | Follows the convention (v0.8.0) |
| `VideoGrabber`, `tcxMidi` (`MidiIn`, `MidiOut`), `DepthCamera`, `AudioEngine` | Planned (#415) |
