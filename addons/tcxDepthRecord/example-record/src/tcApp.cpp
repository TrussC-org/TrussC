#include "tcApp.h"

#include <filesystem>

void tcApp::setup() {
    setWindowTitle("tcxDepthRecord - record + playback");

    // A temp file keeps this demo self-contained (record then replay in one run).
    recPath = (std::filesystem::temp_directory_path() /
               "tcxDepthRecord_example.tcdc").string();

    // 1) Record the SyntheticDepthCamera (no hardware) to a .tcdc file.
    {
        SyntheticDepthCamera cam(640, 480);
        cam.enableDepth();
        cam.enableColor();
        cam.setup();   // inline (not threaded) -> deterministic capture
        DepthRecorder rec;
        rec.start(recPath);
        for (int i = 0; i < 90; ++i) {
            cam.update();
            if (cam.isFrameNew()) rec.record(cam);
        }
        rec.stop();
        recordedFrames = rec.getFrameCount();
        cam.close();
        logNotice("example") << "recorded " << recordedFrames
                             << " frames to " << recPath;
    }

    // 2) Play it back through the SAME DepthCamera interface.
    playback = make_shared<PlaybackDepthCamera>(recPath);
    playback->enableDepth();
    playback->enableColor();
    playback->setup();

    view.setDistance(400.0f);
    view.enableMouseInput();
}

void tcApp::update() {
    playback->update();
    if (playback->isFrameNew()) {
        // colors = true so the replayed (recorded) color is shown - if it
        // matches the synthetic, both depth AND color round-tripped.
        cloud = playback->toMesh({.colors = true});
        cloud.scale(100.0f, -100.0f, 100.0f);
        cloud.translate(0.0f, 0.0f, -200.0f);
    }
}

void tcApp::draw() {
    clear(0.08f, 0.09f, 0.11f);

    view.begin();
    pushMatrix();
    rotateY(getElapsedTimef() * 0.3f + TAU * 0.25f);
    cloud.draw();
    popMatrix();
    view.end();

    setColor(1.0f);
    string hud = "tcxDepthRecord - PLAYBACK\n";
    hud += "recorded " + to_string(recordedFrames) +
           " synthetic frames -> .tcdc -> replayed as a DepthCamera\n";
    hud += to_string(playback->getFrameCount()) + " frames in file (depth + color)";
    drawBitmapString(hud, 20, 20);
}
