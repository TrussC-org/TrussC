#include "tcApp.h"

void tcApp::setup() {
    logNotice("TcvEncoder") << "TCV Encoder - Phase 1 (BC7 only)";

    // Check for command line arguments (CLI mode)
    int argc = getArgCount();
    char** argv = getArgValues();

    if (argc > 1) {
        // Parse CLI arguments
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
                if (q == "fast" || q == "0") quality_ = 0;
                else if (q == "balanced" || q == "1") quality_ = 1;
                else if (q == "high" || q == "2") quality_ = 2;
            } else if ((arg == "-j" || arg == "--jobs") && i + 1 < argc) {
                jobs_ = std::stoi(argv[++i]);
            } else if (arg == "--partitions" && i + 1 < argc) {
                partitions_ = std::stoi(argv[++i]);
            } else if (arg == "--uber" && i + 1 < argc) {
                uber_ = std::stoi(argv[++i]);
            } else if (arg == "-h" || arg == "--help") {
                logNotice("TcvEncoder") << "Usage: tcvEncoder -i <input> [-o <output>] [-q <quality>]";
                logNotice("TcvEncoder") << "  -i, --input      Input video file";
                logNotice("TcvEncoder") << "  -o, --output     Output .tcv file (default: input with .tcv extension)";
                logNotice("TcvEncoder") << "  -q, --quality    Encoding quality: fast, balanced, high (default: balanced)";
                logNotice("TcvEncoder") << "  -j, --jobs N     Number of threads (0=auto, default)";
                logNotice("TcvEncoder") << "  --partitions N   BC7 max partitions (0-64, overrides -q)";
                logNotice("TcvEncoder") << "  --uber N         BC7 uber level (0-4, overrides -q)";
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
                outputPath_ = outputPath;
            }
            logNotice("TcvEncoder") << "Input: " << inputPath;
            logNotice("TcvEncoder") << "Output: " << (outputPath.empty() ? getOutputPath(inputPath) : outputPath);
            startEncoding(inputPath);
        } else {
            logNotice("TcvEncoder") << "No input file specified. Use -i <file> or drag & drop.";
        }
    } else {
        logNotice("TcvEncoder") << "Drag & drop a video file or press O to open";
    }
}

void tcApp::update() {
    if (state_ == State::Encoding) {
        // Encode one frame per update
        encodeNextFrame();
    }

    if (cliMode_ && state_ == State::Done) {
        // Move to next file or exit
        currentFileIndex_++;
        if (currentFileIndex_ < static_cast<int>(filesToEncode_.size())) {
            startEncoding(filesToEncode_[currentFileIndex_]);
        } else {
            logNotice("TcvEncoder") << "All files encoded";
            state_ = State::Exiting;  // Prevent re-entry
            exitApp();
        }
    }
}

void tcApp::draw() {
    clear(0.15f);

    if (state_ == State::Idle) {
        // Show instructions
        setColor(1.0f);
        drawBitmapString("TCV Encoder", 20, 30);
        drawBitmapString("Drag & drop a video file to encode", 20, 60);
        drawBitmapString("Press O to open file dialog", 20, 80);
    }
    else if (state_ == State::Encoding) {
        // Show preview and progress
        float previewW = getWindowWidth() - 40;
        float previewH = previewW * source_.getHeight() / source_.getWidth();
        if (previewH > getWindowHeight() - 120) {
            previewH = getWindowHeight() - 120;
            previewW = previewH * source_.getWidth() / source_.getHeight();
        }

        float x = (getWindowWidth() - previewW) / 2;
        float y = 20;

        setColor(1.0f);
        source_.draw(x, y, previewW, previewH);

        // Progress bar
        float barY = y + previewH + 20;
        float barW = previewW;
        float barH = 20;

        setColor(0.3f);
        drawRect(x, barY, barW, barH);

        setColor(0.2f, 0.8f, 0.4f);
        drawRect(x, barY, barW * progress_, barH);

        // Progress text
        setColor(1.0f);
        string progressText = "Encoding: " + to_string(currentFrame_) + " / " + to_string(totalFrames_);
        drawBitmapString(progressText, x, barY + barH + 20);
    }
    else if (state_ == State::Done) {
        setColor(1.0f);
        drawBitmapString("Encoding complete!", 20, 30);
        drawBitmapString("Press O to encode another file", 20, 60);
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

void tcApp::startEncoding(const string& path) {
    sourcePath_ = path;

    // Load source video
    if (!source_.load(path)) {
        logError("TcvEncoder") << "Failed to load video: " << path;
        return;
    }

    totalFrames_ = source_.getTotalFrames();
    if (totalFrames_ == 0) {
        logError("TcvEncoder") << "Video has no frames";
        return;
    }

    // Get FPS (estimate from duration if not available)
    float duration = source_.getDuration();
    float fps = (duration > 0) ? totalFrames_ / duration : 30.0f;

    // Start encoder
    encoder_.setQuality(quality_);
    if (partitions_ >= 0) encoder_.setPartitions(partitions_);
    if (uber_ >= 0) encoder_.setUberLevel(uber_);
    encoder_.setThreadCount(jobs_);
    
    string outputPath = getOutputPath(path);
    if (!encoder_.begin(outputPath,
                        static_cast<int>(source_.getWidth()),
                        static_cast<int>(source_.getHeight()),
                        fps)) {
        logError("TcvEncoder") << "Failed to start encoder";
        return;
    }

    const char* qualityNames[] = {"fast", "balanced", "high"};
    logNotice("TcvEncoder") << "Starting encode: " << path;
    logNotice("TcvEncoder") << "Output: " << outputPath;
    logNotice("TcvEncoder") << "Size: " << source_.getWidth() << "x" << source_.getHeight();
    logNotice("TcvEncoder") << "Frames: " << totalFrames_ << " @ " << fps << " fps";
    logNotice("TcvEncoder") << "Quality: " << qualityNames[quality_];

    currentFrame_ = 0;
    progress_ = 0.0f;
    state_ = State::Encoding;
    waitingForFrame_ = false;

    // Set to first frame
    source_.setFrame(0);
}

void tcApp::encodeNextFrame() {
    if (currentFrame_ >= totalFrames_) {
        finishEncoding();
        return;
    }

    // Request frame if not already waiting
    if (!waitingForFrame_) {
        if (currentFrame_ == 0) {
            source_.setFrame(0);
        } else {
            source_.nextFrame();
        }
        waitingForFrame_ = true;
        waitCounter_ = 0;
    } else {
        // Increment wait counter
        waitCounter_++;
        
        // If we've waited too long, try forcing a re-request with setFrame
        if (waitCounter_ > 100) {
            retryCount_++;
            if (retryCount_ > 3) {
                logError("TcvEncoder") << "Fatal: Failed to decode frame " << currentFrame_ << " after multiple retries. Finishing early.";
                finishEncoding();
                return;
            }
            logWarning("TcvEncoder") << "Timeout waiting for frame " << currentFrame_ << ". Retrying with setFrame... (Retry " << retryCount_ << ")";
            source_.setFrame(currentFrame_); // Fallback to absolute seek
            waitCounter_ = 0;
            return;
        }
    }
    
    // Always update the player
    source_.update();

    // Wait for new frame to be decoded
    if (!source_.isFrameNew()) {
        return;  // Try again next update
    }

    // Reset wait/retry flags since we got the frame
    waitingForFrame_ = false;
    retryCount_ = 0;

    const unsigned char* pixels = source_.getPixels();
    if (pixels) {
        encoder_.addFrame(pixels);
    }

    currentFrame_++;
    progress_ = static_cast<float>(currentFrame_) / totalFrames_;

    // Log progress periodically
    if (currentFrame_ % 100 == 0 || currentFrame_ == totalFrames_) {
        logNotice("TcvEncoder") << "Frame " << currentFrame_ << " / " << totalFrames_
                                << " (" << static_cast<int>(progress_ * 100) << "%)";
    }
}

void tcApp::finishEncoding() {
    encoder_.end();
    source_.close();

    logNotice("TcvEncoder") << "Encoding complete: " << encoder_.getFrameCount() << " frames";

    state_ = State::Done;
}

string tcApp::getOutputPath(const string& inputPath) {
    // Use custom output path if specified via -o option
    if (!outputPath_.empty()) {
        return outputPath_;
    }
    // Default: replace extension with .tcv
    size_t dotPos = inputPath.rfind('.');
    if (dotPos != string::npos) {
        return inputPath.substr(0, dotPos) + ".tcv";
    }
    return inputPath + ".tcv";
}
