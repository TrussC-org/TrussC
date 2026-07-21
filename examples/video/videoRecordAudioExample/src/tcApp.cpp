// =============================================================================
// videoRecordAudioExample - ScreenRecorder + AAC audio track (macOS)
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    AudioSettings as;
    as.sampleRate = 48000;
    AudioEngine::getInstance().init(as);

    VideoRecordSettings s;
    s.fps = 60.0f;
    s.audio = true;            // record the master mix as an AAC track
    startRecording("recorded.mp4", s);
    logNotice("tcApp") << "recording 5 s with audio -> recorded.mp4";
}

void tcApp::update() {
    // Bounce the ball; every wall hit fires the beep.
    float dt = (float)getDeltaTime();
    ballX += ballV * dt;
    float r = 40.0f, w = (float)getWidth();
    if ((ballX > w - r && ballV > 0) || (ballX < r && ballV < 0)) {
        ballV = -ballV;
        ballX = clamp(ballX, r, w - r);
        beepEnv.store(1.0f);
    }

    if (stopAt > 0.0f && getElapsedTimef() >= stopAt) {
        stopAt = 0.0f;
        stopRecording();
    }
}

void tcApp::audioOut(AudioOutBuffer& buf) {
    // Quiet continuous tone (stream sanity check) + the bounce beep.
    const double dTone = (TAU * 440.0) / buf.sampleRate;
    const double dBeep = (TAU * 1200.0) / buf.sampleRate;
    const float decay = std::exp(-1.0f / (0.030f * buf.sampleRate));  // ~30 ms
    float env = beepEnv.load();
    for (int i = 0; i < buf.frameCount; i++) {
        float v = 0.05f * (float)std::sin(tonePhase)
                + 0.60f * env * (float)std::sin(beepPhase);
        for (int c = 0; c < buf.channels; c++) buf.data[i * buf.channels + c] += v;
        tonePhase += dTone; if (tonePhase >= TAU) tonePhase -= TAU;
        beepPhase += dBeep; if (beepPhase >= TAU) beepPhase -= TAU;
        env *= decay;
    }
    beepEnv.store(env);
}

void tcApp::draw() {
    clear(0.05f);

    setColor(colors::white);
    drawCircle(ballX, getHeight() * 0.5f, 40.0f);

    auto& rec = internal::globalScreenRecorder();
    if (rec.isRecording()) {
        setColor(1.0f, 0.3f, 0.3f);
        drawBitmapString("REC (audio on)", 20, 24);
    } else {
        setColor(0.55f);
        drawBitmapString("done -> bin/data/recorded.mp4 (R to record again)", 20, 24);
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        VideoRecordSettings s;
        s.fps = 60.0f;
        s.audio = true;
        startRecording("recorded.mp4", s);
        stopAt = getElapsedTimef() + 5.0f;
    }
}
