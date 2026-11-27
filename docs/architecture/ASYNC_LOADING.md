# Asynchronous resource loading

`Image::loadAsync`, `SoundBuffer::loadAsync` and `Font::loadAsync` prepare data
privately and apply it on main in a later frame. Keep an `onLoad` listener for
the result. A handle may be destroyed while loading or inside its own completion
listener; destroying it cancels further delivery without waiting for the decoder.

Moving a loading Image, or copying, assigning or moving a loading SoundBuffer or Font,
is not supported; the destination is not loading. Images remain noncopyable. Copy and move operations transfer only committed data;
listeners remain attached to their original handles. Serialize handle operations
on main and wait for a load to finish before copying, assigning or moving it.

Decoder allocation failure is a failed load with an Error. Allocation failure in
queueing or other bookkeeping follows the normal exception policy; completion
under allocation failure is not guaranteed.

Local-file results may exceed the retained-result budget. One best-effort Notice
per run is posted to main; it can be omitted if its run or batch is discarded.
URL and network-buffer admission limits remain in effect; see the API cards.

End a TrussC app with `exitApp()`; the request is posted to main, whose
shutdown drains resource workers. Don't use `std::exit`; from any non-main
thread it is unsupported. See [EXIT_PROTOCOL.md › Request app shutdown with exitApp](EXIT_PROTOCOL.md#request-app-shutdown-with-exitapp).

```cpp
// End the app from anywhere (update, a worker thread, an MCP reply thunk):
exitApp(0);
```
