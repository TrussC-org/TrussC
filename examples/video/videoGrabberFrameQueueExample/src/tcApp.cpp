// =============================================================================
// videoGrabberFrameQueueExample - Timestamped frame queue (measurement capture)
// =============================================================================
// getPixels()/isFrameNew() only ever see the LATEST frame: a camera running
// faster than the app loop silently loses frames, and a timestamp you take in
// update() lies whenever the main loop stalls. The frame queue fixes both:
// every frame is delivered exactly once, stamped on the CAPTURE thread with a
// monotonic (steady_clock) time that stays truthful even if the app freezes.
// Use it to sync camera frames against sensor logs, audio, or other clocks,
// or to use the camera as a measurement device.
//
// Press S to stall the main thread for 2 seconds: the preview freezes, but
// the filmstrip keeps its capture-time intervals at camera rate - no lost
// frames while the queue has room, no lying timestamps ever.
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // Opt-in: queue EVERY frame, timestamped on the capture thread.
    // Size it to at least ceil(cameraFps / appFps) + headroom.
    grabber_.setFrameQueueSize(16);
    grabber_.setup(1280, 720);
}

void tcApp::update() {
    grabber_.update();  // classic path: live preview texture (unaffected)

    // Drain all frames captured since the last call (0..n per update).
    // Each GrabberFrame carries pixels + width/height + timestampUs together.
    frames_.clear();
    grabber_.getBufferFrames(frames_);
    for (auto& f : frames_) {
        Texture& t = tex_[head_];  // rotating filmstrip slot
        if (t.getWidth() != f.width) {
            t.allocate(f.width, f.height, 4, TextureUsage::Stream);
        }
        t.loadData(f.pixels.data(), f.width, f.height, 4);
        tUs_[head_] = f.timestampUs;
        head_ = (head_ + 1) % kStrip;
        count_ = min(count_ + 1, (int)kStrip);
        total_++;
    }
}

void tcApp::draw() {
    clear(0.15f);
    setColor(1.0f);

    if (!grabber_.isInitialized()) {
        drawBitmapString("Waiting for camera...", 20, 30);
        return;
    }

    // live preview (top-left, camera aspect)
    float w = getWidth() * 0.8f;
    float h = w * grabber_.getHeight() / grabber_.getWidth();
    grabber_.draw(0, 0, w, h);

    // filmstrip: last kStrip frames, oldest -> newest, with the interval
    // between their CAPTURE timestamps (not draw time)
    float tw = getWidth() / (float)kStrip;
    float th = tw * grabber_.getHeight() / grabber_.getWidth();
    for (int i = 0; i < count_; i++) {
        int idx = (head_ + kStrip - count_ + i) % kStrip;
        int prev = (idx + kStrip - 1) % kStrip;
        setColor(1.0f);
        tex_[idx].draw(i * tw, h + 8, tw - 2, th);
        if (i > 0) {
            setColor(colors::yellow);
            drawBitmapString(format("+{:.0f}ms", (tUs_[idx] - tUs_[prev]) / 1000.0),
                             i * tw + 2, h + th + 22);
        }
    }

    setColor(1.0f);
    drawBitmapString(format("{} frames queued & drained    "
                            "[S] stall main thread 2s - intervals stay at "
                            "camera rate, timestamps never lie",
                            total_),
                     10, h + th + 44);
}

void tcApp::keyPressed(int key) {
    // Block the main thread on purpose; the capture thread keeps stamping.
    if (key == 'S') sleepMillis(2000);
}
