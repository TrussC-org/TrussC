#pragma once

#include <TrussC.h>
#include <tcxDepthRecord.h>   // also brings tcxDepthCamera
using namespace std;
using namespace tc;
using namespace tcx;

// Records the SyntheticDepthCamera to a .tcdc file, then plays it back through
// the same DepthCamera interface and draws the replayed point cloud. Proves the
// record -> .tcdc -> playback round-trip (depth + color) with no hardware.
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    shared_ptr<PlaybackDepthCamera> playback;
    EasyCam view;
    Mesh cloud;
    int recordedFrames = 0;
    string recPath;
};
