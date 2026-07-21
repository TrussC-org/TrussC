#pragma once

#include <TrussC.h>
#include <atomic>
using namespace std;
using namespace tc;

// =============================================================================
// videoRecordAudioExample - screen recording WITH the master audio mix
//
// startRecording(path, {.audio = true}) records the window AND the engine's
// master output as an AAC track. While audio is recorded, the video PTS runs
// on the AUDIO DEVICE clock, so A/V stay aligned no matter how unstable the
// frame rate is.
//
// The scene is built to make sync measurable: a ball bounces between the
// screen edges and every wall hit fires a short beep. In the recorded file
// the beep onsets line up with the frames where the ball touches a wall.
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

    void audioOut(AudioOutBuffer& buf) override;

private:
    // Ball state (main thread).
    float ballX = 200.0f, ballV = 900.0f;   // px, px/sec

    // Beep envelope handed to the audio thread: update() sets it to 1.0 on a
    // bounce, audioOut() decays it exponentially.
    std::atomic<float> beepEnv{0.0f};
    double beepPhase = 0.0;   // audio-thread only
    double tonePhase = 0.0;   // audio-thread only (continuous quiet tone)

    float stopAt = 5.0f;      // auto-stop the recording (seconds)
};
