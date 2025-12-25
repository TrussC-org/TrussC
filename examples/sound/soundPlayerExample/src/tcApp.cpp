// =============================================================================
// soundPlayerExample - Sound Player Sample
//
// Audio source:
//   "113 2b loose-pants 4.2 mono" by astro_denticle
//   https://freesound.org/
//   License: CC0 (Public Domain)
//   Thanks to astro_denticle for sharing this great beat loop!
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    // Audio file path (CC0 sample audio in data folder)
    musicPath = getDataPath("beat_loop.wav");
    tcLogNotice("tcApp") << "Trying to load: " << musicPath;

    // Load music
    if (music.load(musicPath)) {
        musicLoaded = true;
        music.setLoop(true);
        tcLogNotice("tcApp") << "Music loaded: " << musicPath << " (" << music.getDuration() << " sec)";
    } else {
        tcLogNotice("tcApp") << "Music not found: " << musicPath << " - using test tone";
        music.loadTestTone(440.0f, 3.0f);  // A4 (440Hz), 3 seconds
        music.setLoop(true);
        musicLoaded = true;
    }

    // Sound effect (using test tone)
    sfx.loadTestTone(880.0f, 0.2f);  // A5 (880Hz), 0.2 seconds
    sfxLoaded = true;

    tcLogNotice("tcApp") << "=== Controls ===";
    tcLogNotice("tcApp") << "SPACE: Play/Stop music";
    tcLogNotice("tcApp") << "P: Pause/Resume music";
    tcLogNotice("tcApp") << "S: Play sound effect";
    tcLogNotice("tcApp") << "UP/DOWN: Volume control";
    tcLogNotice("tcApp") << "LEFT/RIGHT: Pan control";
    tcLogNotice("tcApp") << "+/-: Speed control";
    tcLogNotice("tcApp") << "================";
}

void tcApp::draw() {
    clear(0.12f);

    float y = 50;

    // Title
    setColor(colors::white);
    drawBitmapString("TrussC Sound Player Example", 50, y);
    y += 40;

    // Control instructions
    setColor(0.7f);
    drawBitmapString("Controls:", 50, y);
    y += 25;
    drawBitmapString("  SPACE - Play/Stop music", 50, y);
    y += 20;
    drawBitmapString("  P - Pause/Resume music", 50, y);
    y += 20;
    drawBitmapString("  S - Play sound effect", 50, y);
    y += 20;
    drawBitmapString("  UP/DOWN - Volume control", 50, y);
    y += 20;
    drawBitmapString("  LEFT/RIGHT - Pan control", 50, y);
    y += 20;
    drawBitmapString("  +/- - Speed control", 50, y);
    y += 40;

    // Music status
    setColor(colors::white);
    drawBitmapString("=== Music ===", 50, y);
    y += 25;

    if (musicLoaded) {
        std::string status = music.isPlaying() ? "Playing" :
                            (music.isPaused() ? "Paused" : "Stopped");
        setColor(music.isPlaying() ? colors::lime : colors::gray);
        drawBitmapString("Status: " + status, 50, y);
        y += 20;

        setColor(0.7f);
        drawBitmapString(format("Position: {:.1f} / {:.1f} sec",
                music.getPosition(), music.getDuration()), 50, y);
        y += 20;

        drawBitmapString(format("Volume: {:.0f}%", music.getVolume() * 100), 50, y);
        y += 20;

        drawBitmapString(format("Pan: {:.1f} ({})", music.getPan(),
                music.getPan() < -0.1f ? "Left" : music.getPan() > 0.1f ? "Right" : "Center"), 50, y);
        y += 20;

        drawBitmapString(format("Speed: {:.1f}x", music.getSpeed()), 50, y);
        y += 20;

        drawBitmapString(std::string("Loop: ") + (music.isLoop() ? "ON" : "OFF"), 50, y);
        y += 30;

        // Progress bar
        float progress = music.getDuration() > 0 ?
                        music.getPosition() / music.getDuration() : 0;
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

    // Sound effect status
    setColor(colors::white);
    drawBitmapString("=== Sound Effect ===", 50, y);
    y += 25;

    std::string status = sfx.isPlaying() ? "Playing" : "Ready";
    setColor(sfx.isPlaying() ? colors::lime : colors::gray);
    drawBitmapString("Status: " + status, 50, y);

    // FPS
    setColor(0.4f);
    drawBitmapString("FPS: " + std::to_string((int)getFrameRate()), 50, getWindowHeight() - 40);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        // Space: Play/Stop music
        if (musicLoaded) {
            if (music.isPlaying() || music.isPaused()) {
                music.stop();
                tcLogNotice("tcApp") << "Music stopped";
            } else {
                music.play();
                tcLogNotice("tcApp") << "Music playing";
            }
        }
    }
    else if (key == 'p' || key == 'P') {
        // P: Pause/Resume
        if (musicLoaded) {
            if (music.isPaused()) {
                music.resume();
                tcLogNotice("tcApp") << "Music resumed";
            } else if (music.isPlaying()) {
                music.pause();
                tcLogNotice("tcApp") << "Music paused";
            }
        }
    }
    else if (key == 's' || key == 'S') {
        // S: Play sound effect
        if (sfxLoaded) {
            sfx.play();
            tcLogNotice("tcApp") << "SFX playing";
        }
    }
    else if (key == SAPP_KEYCODE_UP) {
        // Volume up
        float vol = music.getVolume() + 0.1f;
        if (vol > 1.0f) vol = 1.0f;
        music.setVolume(vol);
        tcLogNotice("tcApp") << "Volume: " << (int)(vol * 100) << "%";
    }
    else if (key == SAPP_KEYCODE_DOWN) {
        // Volume down
        float vol = music.getVolume() - 0.1f;
        if (vol < 0.0f) vol = 0.0f;
        music.setVolume(vol);
        tcLogNotice("tcApp") << "Volume: " << (int)(vol * 100) << "%";
    }
    else if (key == SAPP_KEYCODE_LEFT) {
        // Pan left
        float pan = music.getPan() - 0.1f;
        music.setPan(pan);
        tcLogNotice("tcApp") << "Pan: " << music.getPan();
    }
    else if (key == SAPP_KEYCODE_RIGHT) {
        // Pan right
        float pan = music.getPan() + 0.1f;
        music.setPan(pan);
        tcLogNotice("tcApp") << "Pan: " << music.getPan();
    }
    else if (key == '+' || key == '=' || key == SAPP_KEYCODE_KP_ADD) {
        // Speed up
        float speed = music.getSpeed() + 0.1f;
        music.setSpeed(speed);
        tcLogNotice("tcApp") << "Speed: " << music.getSpeed() << "x";
    }
    else if (key == '-' || key == SAPP_KEYCODE_KP_SUBTRACT) {
        // Speed down
        float speed = music.getSpeed() - 0.1f;
        music.setSpeed(speed);
        tcLogNotice("tcApp") << "Speed: " << music.getSpeed() << "x";
    }
}
