#pragma once

#include <TrussC.h>
#include "EncodingSession.h"

using namespace std;
using namespace tc;

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
    // Application state
    enum class State {
        Idle,
        Encoding,
        Done,
        Exiting
    };
    State state_ = State::Idle;

    // Current encoding session
    EncodingSession session_;

    // CLI mode
    bool cliMode_ = false;
    vector<string> filesToEncode_;
    int currentFileIndex_ = 0;

    // Settings (from CLI or defaults)
    EncodingSession::Settings settings_;

    // Methods
    void startEncoding(const string& inputPath);
    string getOutputPath(const string& inputPath);
    void parseCommandLine();
    void showHelp();
};
