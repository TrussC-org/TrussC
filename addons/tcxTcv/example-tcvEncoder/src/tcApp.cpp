#include "tcApp.h"

void tcApp::setup() {
    logNotice("TcvEncoder") << "TCV Encoder - Phase 1 (BC7 only)";

    // Initialize default settings
    settings_.quality = 1;  // balanced
    settings_.partitions = -1;
    settings_.uber = -1;
    settings_.jobs = 0;

    parseCommandLine();
}

void tcApp::parseCommandLine() {
    int argc = getArgCount();
    char** argv = getArgValues();

    if (argc <= 1) {
        logNotice("TcvEncoder") << "Drag & drop a video file or press O to open";
        return;
    }

    string inputPath;
    string outputPath;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            inputPath = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputPath = argv[++i];
        } else if ((arg == "-q" || arg == "--quality") && i + 1 < argc) {
            string q = argv[++i];
            if (q == "fast" || q == "0") settings_.quality = 0;
            else if (q == "balanced" || q == "1") settings_.quality = 1;
            else if (q == "high" || q == "2") settings_.quality = 2;
        } else if ((arg == "-j" || arg == "--jobs") && i + 1 < argc) {
            settings_.jobs = std::stoi(argv[++i]);
        } else if (arg == "--partitions" && i + 1 < argc) {
            settings_.partitions = std::stoi(argv[++i]);
        } else if (arg == "--uber" && i + 1 < argc) {
            settings_.uber = std::stoi(argv[++i]);
        } else if (arg == "--all-i") {
            settings_.forceAllIFrames = true;
        } else if (arg == "--no-skip") {
            settings_.enableSkip = false;
        } else if (arg == "--no-solid") {
            settings_.enableSolid = false;
        } else if (arg == "-h" || arg == "--help") {
            showHelp();
            exitApp();
            return;
        } else if (arg[0] != '-') {
            // Positional argument (legacy support)
            if (inputPath.empty()) {
                inputPath = arg;
            }
        }
    }

    if (!inputPath.empty()) {
        cliMode_ = true;
        filesToEncode_.push_back(inputPath);
        if (!outputPath.empty()) {
            settings_.outputPath = outputPath;
        }
        logNotice("TcvEncoder") << "Input: " << inputPath;
        logNotice("TcvEncoder") << "Output: " << (outputPath.empty() ? getOutputPath(inputPath) : outputPath);
        startEncoding(inputPath);
    } else {
        logNotice("TcvEncoder") << "No input file specified. Use -i <file> or drag & drop.";
    }
}

void tcApp::showHelp() {
    logNotice("TcvEncoder") << "Usage: tcvEncoder -i <input> [-o <output>] [-q <quality>]";
    logNotice("TcvEncoder") << "  -i, --input      Input video file";
    logNotice("TcvEncoder") << "  -o, --output     Output .tcv file (default: input with .tcv extension)";
    logNotice("TcvEncoder") << "  -q, --quality    Encoding quality: fast, balanced, high (default: balanced)";
    logNotice("TcvEncoder") << "  -j, --jobs N     Number of threads (0=auto, default)";
    logNotice("TcvEncoder") << "  --partitions N   BC7 max partitions (0-64, overrides -q)";
    logNotice("TcvEncoder") << "  --uber N         BC7 uber level (0-4, overrides -q)";
}

void tcApp::update() {
    if (state_ == State::Encoding) {
        session_.update();

        if (session_.isComplete()) {
            state_ = State::Done;
        } else if (session_.hasFailed()) {
            state_ = State::Done; // Still go to done state
        }
    }

    if (cliMode_ && state_ == State::Done) {
        // Move to next file or exit
        currentFileIndex_++;
        if (currentFileIndex_ < static_cast<int>(filesToEncode_.size())) {
            startEncoding(filesToEncode_[currentFileIndex_]);
        } else {
            logNotice("TcvEncoder") << "All files encoded";
            state_ = State::Exiting;
            exitApp();
        }
    }
}

void tcApp::draw() {
    clear(0.15f);

    float margin = 20.0f;
    float contentW = getWindowWidth() - margin * 2;
    float contentH = getWindowHeight() - margin * 2 - 60; // Leave room for text

    if (state_ == State::Idle) {
        setColor(1.0f);
        drawBitmapString("TCV Encoder", margin, 30);
        drawBitmapString("Drag & drop a video file to encode", margin, 60);
        drawBitmapString("Press O to open file dialog", margin, 80);
    }
    else if (state_ == State::Encoding) {
        session_.draw(margin, margin, contentW, contentH);
    }
    else if (state_ == State::Done) {
        setColor(1.0f);
        drawBitmapString("Encoding complete!", margin, 30);
        drawBitmapString("Encoded " + to_string(session_.getEncodedFrames()) + " frames", margin, 50);
        drawBitmapString("Output: " + session_.getOutputPath(), margin, 70);
        drawBitmapString("Press O to encode another file", margin, 100);
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'o' || key == 'O') {
        auto result = loadDialog("Select video file");
        if (result.success && !result.filePath.empty()) {
            startEncoding(result.filePath);
        }
    }
}

void tcApp::filesDropped(const vector<string>& files) {
    if (!files.empty() && state_ != State::Encoding) {
        startEncoding(files[0]);
    }
}

void tcApp::startEncoding(const string& inputPath) {
    settings_.inputPath = inputPath;
    if (settings_.outputPath.empty()) {
        settings_.outputPath = getOutputPath(inputPath);
    }

    if (session_.begin(settings_)) {
        state_ = State::Encoding;
    } else {
        logError("TcvEncoder") << "Failed to start encoding";
    }

    // Clear custom output path for next file
    settings_.outputPath.clear();
}

string tcApp::getOutputPath(const string& inputPath) {
    size_t dotPos = inputPath.rfind('.');
    if (dotPos != string::npos) {
        return inputPath.substr(0, dotPos) + ".tcv";
    }
    return inputPath + ".tcv";
}
