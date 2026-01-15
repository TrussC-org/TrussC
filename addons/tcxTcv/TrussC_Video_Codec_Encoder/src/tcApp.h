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
    void exit() override;

private:
    // Application state
    enum class State {
        Idle,
        Encoding,
        Done
    };
    State state_ = State::Idle;

    // Current encoding session
    EncodingSession session_;

    // CLI mode
    bool cliMode_ = false;
    vector<string> filesToEncode_;
    int currentFileIndex_ = 0;

    // Settings
    EncodingSession::Settings settings_;

    // Queue item status
    enum class QueueStatus {
        Pending,
        Encoding,
        Done,
        Failed,
        Cancelled
    };

    // Queue item
    struct QueueItem {
        string name;
        string inputPath;
        string outputPath;
        QueueStatus status = QueueStatus::Pending;
        int width = 0;
        int height = 0;
        float fps = 0;
        int totalFrames = 0;
        int encodedFrames = 0;
        size_t inputSize = 0;
        size_t outputSize = 0;
    };
    vector<QueueItem> queue_;
    int currentQueueIndex_ = -1;  // -1 = nothing encoding

    // Log buffer
    struct LogEntry {
        LogLevel level;
        string timestamp;
        string message;
    };
    vector<LogEntry> logBuffer_;
    EventListener logListener_;
    bool autoScrollLog_ = true;
    static constexpr size_t MAX_LOG_ENTRIES = 1000;

    // Methods
    void addToQueue(const string& inputPath);
    void startNextInQueue();
    void cancelCurrentEncoding();
    string getOutputPath(const string& inputPath);
    void parseCommandLine();
    void showHelp();

    // GUI
    void drawGui();
    void drawQueuePane(float width);
    void drawPreviewPane();
    void drawSettingsPane();
    void drawFileInfoPane();
    void drawLogPane();
    void setupLogListener();
};
