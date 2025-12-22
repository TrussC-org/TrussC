#include "tcApp.h"
#include <sstream>

void tcApp::setup() {
    setWindowTitle("noiseField2dExample");
    resetParticles();
}

void tcApp::resetParticles() {
    particles.clear();
    int w = getWindowWidth();
    int h = getWindowHeight();

    for (int i = 0; i < 2000; i++) {
        Particle p;
        p.x = (float)(rand() % w);
        p.y = (float)(rand() % h);
        p.prevX = p.x;
        p.prevY = p.y;
        particles.push_back(p);
    }
}

void tcApp::update() {
    time += getDeltaTime() * timeSpeed;

    // Update particles when in flow field mode
    if (mode == 2) {
        int w = getWindowWidth();
        int h = getWindowHeight();

        for (auto& p : particles) {
            p.prevX = p.x;
            p.prevY = p.y;

            // Get flow angle from noise
            float angle = noise(p.x * noiseScale, p.y * noiseScale, time) * TAU * 2;
            p.x += cos(angle) * 2.0f;
            p.y += sin(angle) * 2.0f;

            // Reset if particle goes off screen
            if (p.x < 0 || p.x > w || p.y < 0 || p.y > h) {
                p.x = (float)(rand() % w);
                p.y = (float)(rand() % h);
                p.prevX = p.x;
                p.prevY = p.y;
            }
        }
    }
}

void tcApp::draw() {
    // Clear background for all modes
    clear(mode == 2 ? 0 : 30);

    switch (mode) {
        case 0: drawNoiseTexture(); break;
        case 1: drawFlowField(); break;
        case 2: drawFlowParticles(); break;
        case 3: drawFbmTexture(); break;
    }

    // UI
    setColor(1.0f);
    std::stringstream ss;
    ss << "Mode " << (mode + 1) << "/" << NUM_MODES << ": ";
    switch (mode) {
        case 0: ss << "Noise Texture"; break;
        case 1: ss << "Flow Field"; break;
        case 2: ss << "Flow Particles"; break;
        case 3: ss << "FBM Texture"; break;
    }
    ss << "\n\nControls:\n";
    ss << "  1-4: Switch mode\n";
    ss << "  +/-: Noise scale (" << noiseScale << ")\n";
    ss << "  [/]: Time speed (" << timeSpeed << ")\n";
    ss << "  r: Reset particles";

    drawBitmapString(ss.str(), 20, 20);
}

void tcApp::updateNoiseImage(bool useFbm) {
    int w = getWindowWidth();
    int h = getWindowHeight();

    // Reallocate if size changed
    if (w != lastWidth || h != lastHeight) {
        noiseImage.allocate(w, h);
        lastWidth = w;
        lastHeight = h;
    }

    // Fill pixels with noise
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float n;
            if (useFbm) {
                n = fbm(x * noiseScale, y * noiseScale, time, 6, 2.0f, 0.5f);
            } else {
                n = noise(x * noiseScale, y * noiseScale, time);
            }
            unsigned char gray = (unsigned char)(n * 255);
            noiseImage.setColor(x, y, Color::fromBytes(gray, gray, gray));
        }
    }
    noiseImage.update();
}

void tcApp::drawNoiseTexture() {
    updateNoiseImage(false);
    setColor(1.0f);
    noiseImage.draw(0, 0);
}

void tcApp::drawFlowField() {
    int w = getWindowWidth();
    int h = getWindowHeight();
    int step = 20;

    for (int y = step; y < h; y += step) {
        for (int x = step; x < w; x += step) {
            float angle = noise(x * noiseScale, y * noiseScale, time) * TAU * 2;
            float len = 10.0f;

            float dx = cos(angle) * len;
            float dy = sin(angle) * len;

            // Change hue based on angle
            setColorHSB(angle, 0.7f, 0.9f);
            drawLine(x, y, x + dx, y + dy);

            // Arrow tip
            drawCircle(x + dx, y + dy, 2);
        }
    }
}

void tcApp::drawFlowParticles() {
    setColor(1.0f, 1.0f, 1.0f, 0.2f);
    for (const auto& p : particles) {
        drawLine(p.prevX, p.prevY, p.x, p.y);
    }
}

void tcApp::drawFbmTexture() {
    updateNoiseImage(true);
    setColor(1.0f);
    noiseImage.draw(0, 0);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case '1': mode = 0; break;
        case '2': mode = 1; break;
        case '3':
            mode = 2;
            break;
        case '4': mode = 3; break;

        case '=':
        case '+':
            noiseScale *= 1.1f;
            break;
        case '-':
            noiseScale /= 1.1f;
            if (noiseScale < 0.001f) noiseScale = 0.001f;
            break;

        case ']':
            timeSpeed *= 1.2f;
            break;
        case '[':
            timeSpeed /= 1.2f;
            if (timeSpeed < 0.01f) timeSpeed = 0.01f;
            break;

        case 'r':
        case 'R':
            resetParticles();
            break;
    }
}
