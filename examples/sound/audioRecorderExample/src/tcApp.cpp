// =============================================================================
// audioRecorderExample - tc::AudioRecorder (master-mix WAV recording)
//
//   - App::audioOut() synthesizes a sine wave (the "content audio").
//   - AudioRecorder::start("recorded.wav") taps the master mix at
//     priority::Monitor and writes it to bin/data/recorded.wav on a
//     background thread; the audio thread never blocks.
//   - Recording starts automatically at launch and stops after 4 seconds,
//     so running the example always yields a verifiable file. R toggles
//     recording manually after that.
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    AudioSettings as;
    as.sampleRate = 48000;
    AudioEngine::getInstance().init(as);

    logNotice("tcApp") << "R: start/stop recording";

    recorder.start("recorded.wav");   // auto-record the first few seconds

    // Second simultaneous recorder: a channel-mapped float32 variant.
    // channelMap uses the same structure as Sound::setChannelMap() — here one
    // output channel summing engine channels 0+1 (unnormalized, so the mono
    // file peaks at 2x the per-channel amplitude).
    AudioRecordSettings mono;
    mono.format = AudioRecordSettings::SampleFormat::F32;
    mono.channelMap = { {0, 1} };
    monoRecorder.start("recorded_mono.wav", mono);
}

void tcApp::update() {
    if (autoStopAt > 0.0f && getElapsedTimef() >= autoStopAt) {
        autoStopAt = 0.0f;
        if (recorder.isRecording()) recorder.stop();
        if (monoRecorder.isRecording()) monoRecorder.stop();
    }
}

void tcApp::audioOut(AudioOutBuffer& buf) {
    const float amp = amplitude.load();
    const double dPhase = (TAU * (double)frequency.load()) / (double)buf.sampleRate;
    for (int i = 0; i < buf.frameCount; i++) {
        float v = amp * (float)std::sin(phase);
        for (int c = 0; c < buf.channels; c++) buf.data[i * buf.channels + c] += v;
        phase += dPhase;
        if (phase >= TAU) phase -= TAU;
    }
}

void tcApp::draw() {
    clear(0.12f);

    float y = 40;
    setColor(colors::white);
    drawBitmapString("TrussC Audio Recorder Example", 40, y); y += 32;

    setColor(0.7f);
    drawBitmapString(format("Synth: {:.0f} Hz sine, amp {:.2f}",
                            frequency.load(), amplitude.load()), 40, y); y += 22;
    drawBitmapString(format("Engine: {} Hz, {} ch",
            AudioEngine::getInstance().getSampleRate(),
            AudioEngine::getInstance().getChannels()), 40, y); y += 32;

    if (recorder.isRecording()) {
        setColor(1.0f, 0.3f, 0.3f);
        drawBitmapString(format("REC {:.1f} s -> {}",
                recorder.getRecordedSeconds(),
                recorder.getPath().filename().string()), 40, y);
    } else {
        setColor(0.55f);
        drawBitmapString("stopped - press R to record", 40, y);
    }
    y += 22;
    if (recorder.getDroppedFrames() > 0) {
        setColor(1.0f, 0.6f, 0.2f);
        drawBitmapString(format("dropped frames: {}", recorder.getDroppedFrames()), 40, y);
    }

    setColor(0.4f);
    drawBitmapString("FPS: " + to_string((int)getFrameRate()), 40, getHeight() - 30);
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        if (recorder.isRecording()) recorder.stop();
        else recorder.start("recorded.wav");
    }
}
