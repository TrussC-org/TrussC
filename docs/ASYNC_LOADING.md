# Asynchronous resource loading

`Image::loadAsync`, `SoundBuffer::loadAsync` and `Font::loadAsync` prepare data
privately and apply it on main in a later frame. Keep an `onLoad` listener for
the result. A handle may be destroyed while loading or inside its own completion
listener; destroying it cancels further delivery without waiting for the decoder.

Copying, assigning or moving a loading Image, SoundBuffer or Font is not supported; the destination is not loading.
Images remain noncopyable. Copy and move operations transfer only committed data;
listeners remain attached to their original handles. Serialize handle operations
on main and wait for a load to finish before copying, assigning or moving it.

Decoder allocation failure is a failed load with an Error. Allocation failure in
queueing or other bookkeeping follows the normal exception policy; completion
under allocation failure is not guaranteed.

Local-file results may exceed the retained-result budget. One best-effort Notice
per run is posted to main; it can be omitted if its run or batch is discarded.
URL and network-buffer admission limits remain in effect; see the API cards.

Background code requests orderly shutdown with `exitApp()` or `requestExitApp()`;
the request is posted to main, whose shutdown drains resource workers. Calling
`std::exit` directly from a resource worker is unsupported.
