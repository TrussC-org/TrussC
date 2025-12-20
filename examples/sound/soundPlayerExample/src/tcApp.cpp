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
    setVsync(true);

    // Audio file path (CC0 sample audio in data folder)
    musicPath = getDataPath("beat_loop.wav");
    printf("Trying to load: %s\n", musicPath.c_str());

    // Load music
    if (music.load(musicPath)) {
        musicLoaded = true;
        music.setLoop(true);
        printf("Music loaded: %s (%.1f sec)\n", musicPath.c_str(), music.getDuration());
    } else {
        printf("Music not found: %s - using test tone\n", musicPath.c_str());
        music.loadTestTone(440.0f, 3.0f);  // A4 (440Hz), 3 seconds
        music.setLoop(true);
        musicLoaded = true;
    }

    // Sound effect (using test tone)
    sfx.loadTestTone(880.0f, 0.2f);  // A5 (880Hz), 0.2 seconds
    sfxLoaded = true;

    printf("\n=== Controls ===\n");
    printf("SPACE: Play/Stop music\n");
    printf("P: Pause/Resume music\n");
    printf("S: Play sound effect\n");
    printf("UP/DOWN: Volume control\n");
    printf("LEFT/RIGHT: Pan control\n");
    printf("+/-: Speed control\n");
    printf("================\n\n");
}

void tcApp::draw() {
    clear(30);

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
        char buf[128];
        snprintf(buf, sizeof(buf), "Position: %.1f / %.1f sec",
                music.getPosition(), music.getDuration());
        drawBitmapString(buf, 50, y);
        y += 20;

        snprintf(buf, sizeof(buf), "Volume: %.0f%%", music.getVolume() * 100);
        drawBitmapString(buf, 50, y);
        y += 20;

        snprintf(buf, sizeof(buf), "Pan: %.1f (%s)", music.getPan(),
                music.getPan() < -0.1f ? "Left" : music.getPan() > 0.1f ? "Right" : "Center");
        drawBitmapString(buf, 50, y);
        y += 20;

        snprintf(buf, sizeof(buf), "Speed: %.1fx", music.getSpeed());
        drawBitmapString(buf, 50, y);
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
    drawBitmapString("FPS: " + std::to_string((int)getFrameRate()), 50, 560);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        // Space: Play/Stop music
        if (musicLoaded) {
            if (music.isPlaying() || music.isPaused()) {
                music.stop();
                printf("Music stopped\n");
            } else {
                music.play();
                printf("Music playing\n");
            }
        }
    }
    else if (key == 'p' || key == 'P') {
        // P: Pause/Resume
        if (musicLoaded) {
            if (music.isPaused()) {
                music.resume();
                printf("Music resumed\n");
            } else if (music.isPlaying()) {
                music.pause();
                printf("Music paused\n");
            }
        }
    }
    else if (key == 's' || key == 'S') {
        // S: Play sound effect
        if (sfxLoaded) {
            sfx.play();
            printf("SFX playing\n");
        }
    }
    else if (key == SAPP_KEYCODE_UP) {
        // Volume up
        float vol = music.getVolume() + 0.1f;
        if (vol > 1.0f) vol = 1.0f;
        music.setVolume(vol);
        printf("Volume: %.0f%%\n", vol * 100);
    }
    else if (key == SAPP_KEYCODE_DOWN) {
        // Volume down
        float vol = music.getVolume() - 0.1f;
        if (vol < 0.0f) vol = 0.0f;
        music.setVolume(vol);
        printf("Volume: %.0f%%\n", vol * 100);
    }
    else if (key == SAPP_KEYCODE_LEFT) {
        // Pan left
        float pan = music.getPan() - 0.1f;
        music.setPan(pan);
        printf("Pan: %.1f\n", music.getPan());
    }
    else if (key == SAPP_KEYCODE_RIGHT) {
        // Pan right
        float pan = music.getPan() + 0.1f;
        music.setPan(pan);
        printf("Pan: %.1f\n", music.getPan());
    }
    else if (key == '+' || key == '=' || key == SAPP_KEYCODE_KP_ADD) {
        // Speed up
        float speed = music.getSpeed() + 0.1f;
        music.setSpeed(speed);
        printf("Speed: %.1fx\n", music.getSpeed());
    }
    else if (key == '-' || key == SAPP_KEYCODE_KP_SUBTRACT) {
        // Speed down
        float speed = music.getSpeed() - 0.1f;
        music.setSpeed(speed);
        printf("Speed: %.1fx\n", music.getSpeed());
    }
}
