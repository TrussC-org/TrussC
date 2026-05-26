// =============================================================================
// soundStreamExample - Streaming Sound Player Sample
//
// Demonstrates Sound::loadStream() — the file stays on disk and is decoded
// on demand by a background StreamWorker thread into a small ring buffer.
// Best for long files (BGM, podcasts) where full PCM in RAM is wasteful.
//
// Compare with soundPlayerExample (eager load, full PCM in RAM).
//
// Streaming constraints (vs eager load):
//   - setSpeed() is ignored (decoder outputs engine-rate frames).
//   - setPosition() incurs a seek + ring-buffer refill (~10 ms blackout).
//   - Supported formats: WAV / MP3 / FLAC. (.ogg / .aac use load() instead.)
//
// Audio source:
//   "113 2b loose-pants 4.2 mono" by astro_denticle
//   https://freesound.org/
//   License: CC0 (Public Domain)
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    // WAV is supported by the streaming decoder (AAC is not — use load()
    // for AAC sources).
    musicPath = getDataPath("beat_loop.wav");

    logNotice("tcApp") << "Trying to stream: " << musicPath;
    if (music.loadStream(musicPath)) {
        musicLoaded = true;
        music.setLoop(true);
        logNotice("tcApp") << "Stream ready: " << musicPath
                           << " (" << music.getDuration() << " sec, streaming="
                           << (music.isStreaming() ? "yes" : "no") << ")";
    } else {
        logWarning("tcApp") << "Stream load failed: " << musicPath
                            << " - falling back to test tone (eager)";
        music.loadTestTone(440.0f, 3.0f);
        music.setLoop(true);
        musicLoaded = true;
    }

    // SFX is eager (short test tone) — proves eager + stream voices mix
    // happily in the same engine.
    sfx.loadTestTone(880.0f, 0.2f);
    sfxLoaded = true;

    logNotice("tcApp") << "=== Controls ===";
    logNotice("tcApp") << "SPACE: Play/Stop streaming music";
    logNotice("tcApp") << "P: Pause/Resume music";
    logNotice("tcApp") << "S: Play eager SFX";
    logNotice("tcApp") << "UP/DOWN: Volume control";
    logNotice("tcApp") << "LEFT/RIGHT: Pan control";
    logNotice("tcApp") << "[ / ]: Seek -5s / +5s";
    logNotice("tcApp") << "+/-: Speed (0 ~ 10 on streams, 0 = freeze)";
    logNotice("tcApp") << "0: Reset speed to 1.0";
    logNotice("tcApp") << "================";
}

void tcApp::draw() {
    clear(0.12f);

    float y = 50;

    setColor(colors::white);
    drawBitmapString("TrussC Sound Stream Example", 50, y);
    y += 40;

    setColor(0.7f);
    drawBitmapString("Controls:", 50, y);
    y += 25;
    drawBitmapString("  SPACE - Play/Stop streaming music", 50, y);
    y += 20;
    drawBitmapString("  P - Pause/Resume music", 50, y);
    y += 20;
    drawBitmapString("  S - Play eager SFX (separate voice)", 50, y);
    y += 20;
    drawBitmapString("  UP/DOWN - Volume", 50, y);
    y += 20;
    drawBitmapString("  LEFT/RIGHT - Pan", 50, y);
    y += 20;
    drawBitmapString("  [ / ] - Seek -5s / +5s", 50, y);
    y += 20;
    drawBitmapString("  +/- - Speed (0 ~ 10 on streams, 0 = freeze)", 50, y);
    y += 20;
    drawBitmapString("  0   - Reset speed to 1.0", 50, y);
    y += 40;

    setColor(colors::white);
    drawBitmapString("=== Music (Streaming) ===", 50, y);
    y += 25;

    if (musicLoaded) {
        std::string status = music.isPlaying() ? "Playing" :
                            (music.isPaused() ? "Paused" : "Stopped");
        setColor(music.isPlaying() ? colors::lime : colors::gray);
        drawBitmapString("Status: " + status, 50, y);
        y += 20;

        setColor(0.7f);
        drawBitmapString(format("Mode: {}",
                music.isStreaming() ? "Streaming" : "Eager (fallback)"), 50, y);
        y += 20;

        drawBitmapString(format("Position: {:.1f} / {:.1f} sec",
                music.getPosition(), music.getDuration()), 50, y);
        y += 20;

        drawBitmapString(format("Volume: {:.0f}%", music.getVolume() * 100), 50, y);
        y += 20;

        drawBitmapString(format("Speed: {:.1f}x", music.getSpeed()), 50, y);
        y += 20;

        drawBitmapString(format("Pan: {:.1f} ({})", music.getPan(),
                music.getPan() < -0.1f ? "Left" : music.getPan() > 0.1f ? "Right" : "Center"), 50, y);
        y += 20;

        drawBitmapString(std::string("Loop: ") + (music.isLoop() ? "ON" : "OFF"), 50, y);
        y += 30;

        // Progress bar
        float pos = music.getPosition();
        float dur = music.getDuration();
        float progress = dur > 0 ? pos / dur : 0;

        setColor(0.24f);
        drawRect(50, y, 300, 20);
        setColor(colors::dodgerBlue);
        drawRect(50, y, 300 * progress, 20);
        y += 40;
    } else {
        setColor(colors::red);
        drawBitmapString("Music file not found: " + musicPath, 50, y);
        y += 40;
    }

    setColor(colors::white);
    drawBitmapString("=== SFX (Eager) ===", 50, y);
    y += 25;

    std::string sfxStatus = sfx.isPlaying() ? "Playing" : "Ready";
    setColor(sfx.isPlaying() ? colors::lime : colors::gray);
    drawBitmapString("Status: " + sfxStatus, 50, y);

    setColor(0.4f);
    drawBitmapString("FPS: " + std::to_string((int)getFrameRate()), 50, getWindowHeight() - 40);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        if (musicLoaded) {
            if (music.isPlaying() || music.isPaused()) {
                music.stop();
                logNotice("tcApp") << "Music stopped";
            } else {
                music.play();
                logNotice("tcApp") << "Music playing";
            }
        }
    }
    else if (key == 'p' || key == 'P') {
        if (musicLoaded) {
            if (music.isPaused()) {
                music.resume();
                logNotice("tcApp") << "Music resumed";
            } else if (music.isPlaying()) {
                music.pause();
                logNotice("tcApp") << "Music paused";
            }
        }
    }
    else if (key == 's' || key == 'S') {
        if (sfxLoaded) {
            sfx.play();
            logNotice("tcApp") << "SFX playing";
        }
    }
    else if (key == SAPP_KEYCODE_UP) {
        float vol = music.getVolume() + 0.1f;
        if (vol > 1.0f) vol = 1.0f;
        music.setVolume(vol);
        logNotice("tcApp") << "Volume: " << (int)(vol * 100) << "%";
    }
    else if (key == SAPP_KEYCODE_DOWN) {
        float vol = music.getVolume() - 0.1f;
        if (vol < 0.0f) vol = 0.0f;
        music.setVolume(vol);
        logNotice("tcApp") << "Volume: " << (int)(vol * 100) << "%";
    }
    else if (key == SAPP_KEYCODE_LEFT) {
        float pan = music.getPan() - 0.1f;
        music.setPan(pan);
        logNotice("tcApp") << "Pan: " << music.getPan();
    }
    else if (key == SAPP_KEYCODE_RIGHT) {
        float pan = music.getPan() + 0.1f;
        music.setPan(pan);
        logNotice("tcApp") << "Pan: " << music.getPan();
    }
    else if (key == '[') {
        // Seek back 5 seconds
        float newPos = music.getPosition() - 5.0f;
        if (newPos < 0) newPos = 0;
        music.setPosition(newPos);
        logNotice("tcApp") << "Seek to " << newPos << "s";
    }
    else if (key == ']') {
        // Seek forward 5 seconds
        float newPos = music.getPosition() + 5.0f;
        if (newPos > music.getDuration()) newPos = music.getDuration() - 0.1f;
        music.setPosition(newPos);
        logNotice("tcApp") << "Seek to " << newPos << "s";
    }
    else if (key == '+' || key == '=' || key == SAPP_KEYCODE_KP_ADD) {
        // Streams clamp negative to 0 internally — UI just feeds the value.
        music.setSpeed(music.getSpeed() + 0.5f);
        logNotice("tcApp") << "Speed: " << music.getSpeed() << "x";
    }
    else if (key == '-' || key == SAPP_KEYCODE_KP_SUBTRACT) {
        music.setSpeed(music.getSpeed() - 0.5f);
        logNotice("tcApp") << "Speed: " << music.getSpeed() << "x";
    }
    else if (key == '0') {
        music.setSpeed(1.0f);
        logNotice("tcApp") << "Speed reset to 1.0x";
    }
}
