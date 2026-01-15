#include "tcApp.h"
#include <filesystem>

void tcApp::setup() {
    // Enable ImGui
    imguiSetup();

    // Setup log listener
    setupLogListener();

    logNotice("TcvEncoder") << "TCV Encoder v4 - ImGui Edition";

    // Initialize default settings (balanced preset)
    settings_.quality = 1;
    settings_.partitions = 16;
    settings_.uber = 1;
    settings_.jobs = 0;

    parseCommandLine();
}

void tcApp::setupLogListener() {
    tcGetLogger().onLog.listen(logListener_, [this](LogEventArgs& e) {
        LogEntry entry;
        entry.level = e.level;
        entry.timestamp = e.timestamp;
        entry.message = e.message;
        logBuffer_.push_back(entry);

        // Limit buffer size
        if (logBuffer_.size() > MAX_LOG_ENTRIES) {
            logBuffer_.erase(logBuffer_.begin(), logBuffer_.begin() + 100);
        }
    });
}

void tcApp::exit() {
    logListener_.disconnect();
    imguiShutdown();
}

void tcApp::parseCommandLine() {
    int argc = getArgCount();
    char** argv = getArgValues();

    if (argc <= 1) {
        logNotice("TcvEncoder") << "Drag & drop video files to encode";
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
            settings_.jobs = stoi(argv[++i]);
        } else if (arg == "--partitions" && i + 1 < argc) {
            settings_.partitions = stoi(argv[++i]);
        } else if (arg == "--uber" && i + 1 < argc) {
            settings_.uber = stoi(argv[++i]);
        } else if (arg == "--all-i") {
            settings_.forceAllIFrames = true;
        } else if (arg == "--no-skip") {
            settings_.enableSkip = false;
        } else if (arg == "-h" || arg == "--help") {
            showHelp();
            exitApp();
            return;
        } else if (arg[0] != '-') {
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
        addToQueue(inputPath);
        startNextInQueue();
    }
}

void tcApp::showHelp() {
    logNotice("TcvEncoder") << "Usage: TrussC_Video_Codec_Encoder -i <input> [-o <output>] [-q <quality>]";
    logNotice("TcvEncoder") << "  -i, --input      Input video file";
    logNotice("TcvEncoder") << "  -o, --output     Output .tcv file";
    logNotice("TcvEncoder") << "  -q, --quality    fast(0), balanced(1), high(2)";
    logNotice("TcvEncoder") << "  -j, --jobs N     Number of threads (0=auto)";
}

void tcApp::update() {
    if (state_ == State::Encoding && currentQueueIndex_ >= 0) {
        session_.update();

        // Update current item's encoded frames
        queue_[currentQueueIndex_].encodedFrames = session_.getCurrentFrame();

        if (session_.isComplete()) {
            auto& item = queue_[currentQueueIndex_];
            item.status = QueueStatus::Done;
            item.encodedFrames = session_.getEncodedFrames();

            // Update output file size
            if (filesystem::exists(item.outputPath)) {
                item.outputSize = filesystem::file_size(item.outputPath);
            }

            logNotice("TcvEncoder") << "Complete: " << item.name;
            currentQueueIndex_ = -1;
            state_ = State::Idle;

            // Start next in queue
            startNextInQueue();
        } else if (session_.hasFailed()) {
            queue_[currentQueueIndex_].status = QueueStatus::Failed;
            logError("TcvEncoder") << "Failed: " << queue_[currentQueueIndex_].name;
            currentQueueIndex_ = -1;
            state_ = State::Idle;

            // Start next in queue
            startNextInQueue();
        }
    }

    if (cliMode_ && state_ == State::Idle) {
        // Check if all done
        bool allDone = true;
        for (const auto& item : queue_) {
            if (item.status == QueueStatus::Pending || item.status == QueueStatus::Encoding) {
                allDone = false;
                break;
            }
        }
        if (allDone) {
            logNotice("TcvEncoder") << "All files encoded";
            exitApp();
        }
    }
}

void tcApp::draw() {
    clear(0.12f);

    if (!cliMode_) {
        imguiBegin();
        drawGui();
        imguiEnd();
    }
}

void tcApp::drawGui() {
    // Main window covering full app
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(getWindowWidth(), getWindowHeight()));
    ImGui::Begin("TCV Encoder", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Layout: Left (settings + queue) | Right (preview + info + log)
    float leftWidth = 300;
    float rightWidth = getWindowWidth() - leftWidth - 20;

    // Left pane - Settings + Queue
    ImGui::BeginChild("LeftPane", ImVec2(leftWidth, 0), true);

    // Settings at top
    drawSettingsPane();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Queue list below settings
    drawQueuePane(leftWidth);

    ImGui::EndChild();

    ImGui::SameLine();

    // Right pane - Preview + File Info + Log (bordered)
    ImGui::BeginChild("RightPane", ImVec2(rightWidth, 0), true);

    // Preview at top
    drawPreviewPane();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // File information
    drawFileInfoPane();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Log at bottom
    drawLogPane();

    ImGui::EndChild();

    ImGui::End();
}

void tcApp::drawQueuePane(float width) {
    ImGui::Text("Encoding Queue");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Drop files or press O to add");
    ImGui::Separator();

    // Queue list with scrolling (takes remaining space)
    float listHeight = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("QueueList", ImVec2(0, listHeight), true);

    if (queue_.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "No files in queue");
    }

    for (size_t i = 0; i < queue_.size(); i++) {
        auto& item = queue_[i];

        // Status color and icon
        ImVec4 textColor;
        const char* statusIcon = "";

        switch (item.status) {
            case QueueStatus::Done:
                textColor = ImVec4(0.6f, 0.85f, 0.65f, 1.0f);  // Green
                statusIcon = "[Done] ";
                break;
            case QueueStatus::Failed:
                textColor = ImVec4(0.95f, 0.5f, 0.5f, 1.0f);  // Red
                statusIcon = "[Fail] ";
                break;
            case QueueStatus::Cancelled:
                textColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Gray
                statusIcon = "[Cancel] ";
                break;
            case QueueStatus::Encoding:
                textColor = ImVec4(0.7f, 0.8f, 1.0f, 1.0f);  // Blue
                statusIcon = "[...] ";
                break;
            case QueueStatus::Pending:
            default:
                textColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Neutral
                statusIcon = "";
                break;
        }

        ImGui::PushID(static_cast<int>(i));

        // Combine into single line for simpler click handling
        string label = string(statusIcon) + item.name;
        if (item.width > 0) {
            label += " (" + to_string(item.width) + "x" + to_string(item.height) + ")";
        }

        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::Selectable(label.c_str(), false);
        ImGui::PopStyleColor();

        // Context menu
        if (ImGui::BeginPopupContextItem("ctx")) {
            if (item.status == QueueStatus::Encoding) {
                if (ImGui::MenuItem("Cancel")) {
                    cancelCurrentEncoding();
                }
            } else if (item.status == QueueStatus::Pending) {
                if (ImGui::MenuItem("Remove")) {
                    ImGui::EndPopup();
                    ImGui::PopID();
                    queue_.erase(queue_.begin() + i);
                    i--;
                    continue;
                }
            } else {
                if (ImGui::MenuItem("Remove from list")) {
                    ImGui::EndPopup();
                    ImGui::PopID();
                    queue_.erase(queue_.begin() + i);
                    i--;
                    continue;
                }
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

void tcApp::drawPreviewPane() {
    // Preview section (150px height max)
    float previewHeight = 150;
    float availWidth = ImGui::GetContentRegionAvail().x;

    ImGui::Text("Preview");

    if (currentQueueIndex_ >= 0 && session_.hasSourceTexture()) {
        const Texture& tex = session_.getSourceTexture();
        if (tex.isAllocated()) {
            float aspect = static_cast<float>(tex.getHeight()) / tex.getWidth();
            float previewW = min(availWidth, previewHeight / aspect);
            float previewH = previewW * aspect;
            if (previewH > previewHeight) {
                previewH = previewHeight;
                previewW = previewH / aspect;
            }

            // Center the preview
            float offsetX = (availWidth - previewW) * 0.5f;
            if (offsetX > 0) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            }

            ImTextureID texId = simgui_imtextureid(tex.getView());
            ImGui::Image(texId, ImVec2(previewW, previewH));
        }

        // Progress bar (thin)
        float progress = session_.getProgress();
        ImGui::ProgressBar(progress, ImVec2(-1, 8), "");

        // Frame info
        ImGui::Text("Frame: %d / %d  |  %s",
            session_.getCurrentFrame(), session_.getTotalFrames(),
            session_.getPhaseString().c_str());
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "No encoding in progress");
        ImGui::Dummy(ImVec2(0, previewHeight - 20));
    }
}

void tcApp::drawSettingsPane() {
    ImGui::Text("TCV Encoder v4");
    ImGui::Separator();

    // Quality presets
    ImGui::Text("Quality Preset:");
    if (ImGui::Button("Q0 Fast", ImVec2(85, 0))) {
        settings_.partitions = 0;
        settings_.uber = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Q1 Balanced", ImVec2(95, 0))) {
        settings_.partitions = 16;
        settings_.uber = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Q2 High", ImVec2(75, 0))) {
        settings_.partitions = 64;
        settings_.uber = 4;
    }

    // P/U sliders
    ImGui::SliderInt("Partitions", &settings_.partitions, 0, 64);
    ImGui::SliderInt("Uber", &settings_.uber, 0, 4);

    ImGui::Spacing();

    // Advanced settings (collapsed by default)
    if (ImGui::CollapsingHeader("Advanced Settings")) {
        ImGui::SliderInt("Threads", &settings_.jobs, 0, 16, settings_.jobs == 0 ? "Auto" : "%d");
        ImGui::Checkbox("Force All I-Frames", &settings_.forceAllIFrames);
        ImGui::Checkbox("Enable SKIP", &settings_.enableSkip);
    }
}

void tcApp::drawFileInfoPane() {
    ImGui::Text("File Information");
    ImGui::Separator();

    if (currentQueueIndex_ >= 0) {
        const auto& item = queue_[currentQueueIndex_];

        ImGui::Text("Name: %s", item.name.c_str());
        ImGui::Text("Video: %dx%d @ %.2f fps, %d frames", item.width, item.height, item.fps, item.totalFrames);

        // Audio info
        if (session_.hasAudio()) {
            // Convert FourCC to string
            uint32_t codec = session_.getAudioCodec();
            char codecStr[5] = {0};
            codecStr[0] = static_cast<char>((codec >> 24) & 0xFF);
            codecStr[1] = static_cast<char>((codec >> 16) & 0xFF);
            codecStr[2] = static_cast<char>((codec >> 8) & 0xFF);
            codecStr[3] = static_cast<char>(codec & 0xFF);

            ImGui::Text("Audio: %s, %d Hz, %d ch",
                codecStr,
                session_.getAudioSampleRate(),
                session_.getAudioChannels());
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Audio: none");
        }

        ImGui::Text("Output: %s", item.outputPath.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No file being encoded");
    }
}

void tcApp::drawLogPane() {
    ImGui::Text("Log");
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScrollLog_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        logBuffer_.clear();
    }

    float logHeight = ImGui::GetContentRegionAvail().y - 5;
    ImGui::BeginChild("LogWindow", ImVec2(0, logHeight), true,
        ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& entry : logBuffer_) {
        ImVec4 color;
        switch (entry.level) {
            case LogLevel::Error:
            case LogLevel::Fatal:
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                break;
            case LogLevel::Warning:
                color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
                break;
            case LogLevel::Notice:
                color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                break;
            default:
                color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                break;
        }
        ImGui::TextColored(color, "%s", entry.message.c_str());
    }

    if (autoScrollLog_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

void tcApp::keyPressed(int key) {
    if (key == 'o' || key == 'O') {
        auto result = loadDialog("Select video file");
        if (result.success && !result.filePath.empty()) {
            addToQueue(result.filePath);
            if (state_ == State::Idle) {
                startNextInQueue();
            }
        }
    }
}

void tcApp::filesDropped(const vector<string>& files) {
    for (const auto& file : files) {
        addToQueue(file);
    }

    if (state_ == State::Idle) {
        startNextInQueue();
    }
}

void tcApp::addToQueue(const string& inputPath) {
    QueueItem item;
    item.inputPath = inputPath;
    item.outputPath = getOutputPath(inputPath);
    item.name = filesystem::path(inputPath).filename().string();
    item.status = QueueStatus::Pending;

    if (filesystem::exists(inputPath)) {
        item.inputSize = filesystem::file_size(inputPath);
    }

    queue_.push_back(item);
    logNotice("TcvEncoder") << "Added to queue: " << item.name;
}

void tcApp::startNextInQueue() {
    // Find next pending item
    for (size_t i = 0; i < queue_.size(); i++) {
        if (queue_[i].status == QueueStatus::Pending) {
            currentQueueIndex_ = static_cast<int>(i);
            auto& item = queue_[i];

            settings_.inputPath = item.inputPath;
            settings_.outputPath = item.outputPath;

            if (session_.begin(settings_)) {
                item.status = QueueStatus::Encoding;
                item.width = session_.getVideoWidth();
                item.height = session_.getVideoHeight();
                item.fps = session_.getVideoFps();
                item.totalFrames = session_.getTotalFrames();
                state_ = State::Encoding;

                logNotice("TcvEncoder") << "Encoding: " << item.name;
            } else {
                item.status = QueueStatus::Failed;
                logError("TcvEncoder") << "Failed to start: " << item.name;
                currentQueueIndex_ = -1;
                // Try next
                startNextInQueue();
            }
            return;
        }
    }

    // Nothing to encode
    currentQueueIndex_ = -1;
}

void tcApp::cancelCurrentEncoding() {
    if (currentQueueIndex_ >= 0) {
        queue_[currentQueueIndex_].status = QueueStatus::Cancelled;
        session_.cancel();
        logNotice("TcvEncoder") << "Cancelled: " << queue_[currentQueueIndex_].name;
        currentQueueIndex_ = -1;
        state_ = State::Idle;

        // Start next
        startNextInQueue();
    }
}

string tcApp::getOutputPath(const string& inputPath) {
    filesystem::path p(inputPath);
    filesystem::path dir = p.parent_path();
    string stem = p.stem().string();
    string ext = ".tcv";

    // Try base name first
    filesystem::path output = dir / (stem + ext);
    if (!filesystem::exists(output)) {
        return output.string();
    }

    // Add suffix if collision
    for (int i = 1; i < 1000; i++) {
        output = dir / (stem + "-" + to_string(i) + ext);
        if (!filesystem::exists(output)) {
            return output.string();
        }
    }

    // Fallback
    return (dir / (stem + "-new" + ext)).string();
}
