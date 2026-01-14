#pragma once

#include <TrussC.h>
#include <tcxTcv.h>

using namespace std;
using namespace tc;
using namespace tcx;

// Access command line args from main.cpp
int getArgCount();
char** getArgValues();

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void filesDropped(const vector<string>& files) override;

private:
    // Encoding state
    enum class State {
        Idle,
        Encoding,
        Done,
        Exiting
    };
    State state_ = State::Idle;

    // Source video
    VideoPlayer source_;
    string sourcePath_;

    // Encoder
    TcvEncoder encoder_;

    // Progress
    int currentFrame_ = 0;
    int totalFrames_ = 0;
    float progress_ = 0.0f;

    // CLI mode
    bool cliMode_ = false;
    vector<string> filesToEncode_;
    int currentFileIndex_ = 0;
    string outputPath_;  // Custom output path from -o option
    int quality_ = 1;    // 0=fast, 1=balanced, 2=high
    int partitions_ = -1;  // Manual override (-1 = use quality preset)
    int uber_ = -1;        // Manual override (-1 = use quality preset)
    int jobs_ = 0;         // 0 = auto/max cores

    bool waitingForFrame_ = false; // Flag to prevent repeated setFrame() calls
    int waitCounter_ = 0;          // Timeout counter for frame requests
    int retryCount_ = 0;           // Number of times we've retried the current frame

    // Methods
    void startEncoding(const string& path);
    void encodeNextFrame();
    void finishEncoding();
    string getOutputPath(const string& inputPath);
};
