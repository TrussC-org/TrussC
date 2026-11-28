# tcxHap

HAP video playback addon. Add `tcxHap` to your project's `addons.make`.

## Read Hap frames as pixels

`HapPlayer::getPixels()` is always empty: frames stay BC/DXT-compressed on the
GPU, and the call performs no CPU decode. For pixels, allocate an Fbo at the
loaded video's size, draw into it, and call `readPixels()`. With `hap`, `fbo`,
and `pixels` declared as `HapPlayer`, `Fbo`, and `Pixels` members of `tcApp`:

```cpp
void tcApp::setup() {
    if (hap.load("video.mov")) {
        fbo.allocate(hap.getWidth(), hap.getHeight());
        hap.play();
    }
}

void tcApp::update() {
    hap.update();
    if (hap.isFrameNew()) {
        fbo.begin();
        hap.draw(0, 0);
        fbo.end();
        if (fbo.readPixels(pixels)) {
            // use pixels
        }
    }
}
```

`readPixels()` returns false on WebGPU, where synchronous readback is
unsupported, and when called inside another active Fbo; check its return
value as above. On WebGPU, use the frame on the GPU:

```cpp
void tcApp::draw() {
    fbo.getTexture().draw(0, 0);
}
```

## Hot reload on Windows

Windows hot-reload guests do not link tcxHap's snappy dependency.
`tcxHapDecoder.h` (also included by `tcxHapPlayer.h`) declares this limitation
with `TC_NO_HOT_RELOAD`, so the guest
fails to compile with the reason instead of failing at link time. Remove
`TC_HOT_RELOAD` from your app to use tcxHap on Windows. Ordinary Windows builds
and hot reload on Linux/macOS are unaffected by this declaration.

## GPU requirements

HAP movies store BC-compressed textures, such as BC1 for Hap and BC3 for
Hap Alpha / Hap Q. `HapPlayer` requires GPU sampling support for the movie's
BC format. Without it, loading fails with
`Compressed texture format not supported on this GPU`.

On iOS, TrussC's Metal backend enables BC texture formats when the GPU
reports BC support. Hap1 and Hap Q playback with correct colors was checked
on an iPhone 16 in the #645 experiment. Where BC is not supported, for example
on the Simulator, loading fails cleanly with the message above, without a crash.

C++ OOM recovery requires Release builds, or iterator debugging off (`_ITERATOR_DEBUG_LEVEL=0` on MSVC). With iterator debugging enabled, even empty string/container construction and moves can allocate a proxy on an error path.
