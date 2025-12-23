// =============================================================================
// tcApp.cpp - OSC Polling-style Example
// =============================================================================
// This sample receives OSC using "polling" style.
//
// [Features of polling style]
// - Messages are retrieved in update(), so processing happens on main thread
// - No mutex synchronization needed - simpler code
// - No event handler registration required
//
// For async processing, see example-osc-event.
// =============================================================================

#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    // Initialize ImGui
    imguiSetup();

    // Setup receiver
    if (!receiver_.setup(port_)) {
        addReceiveLog("[ERROR] Failed to bind port " + to_string(port_));
    }
    else {
        addReceiveLog("Listening on port " + to_string(port_));
    }

    // Setup sender (send to self)
    if (!sender_.setup("127.0.0.1", port_)) {
        addSendLog("[ERROR] Failed to setup sender");
    }
}

void tcApp::update() {
    // ---------------------------------------------------------------------------
    // Get OSC messages by polling
    // ---------------------------------------------------------------------------
    // Calling hasNewMessage() enables buffering
    // getNextMessage() retrieves one message at a time from the queue
    // ---------------------------------------------------------------------------
    while (receiver_.hasNewMessage()) {
        OscMessage msg;
        if (receiver_.getNextMessage(msg)) {
            // This is main thread, no mutex needed!
            addReceiveLog("[RECEIVED] " + msg.toString());
        }
    }
}

void tcApp::draw() {
    clear(0.12f);

    // ImGui frame start
    imguiBegin();

    // Position ImGui window to fill entire screen
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)getWindowWidth(), (float)getWindowHeight()));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("OSC Example (Polling)", nullptr, flags)) {
        // Title
        ImGui::Text("OSC Example (Polling) - Port %d", port_);
        ImGui::Separator();

        // Left/right panes
        float panelWidth = (ImGui::GetContentRegionAvail().x - 20) / 2;

        // ==== Left pane: Sender ====
        ImGui::BeginChild("Sender", ImVec2(panelWidth, -30), true);
        {
            ImGui::Text("SENDER");
            ImGui::Separator();
            ImGui::Spacing();

            // Address input
            ImGui::Text("Address:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##address", addressBuf_, sizeof(addressBuf_));

            ImGui::Spacing();
            ImGui::Text("Arguments:");

            // Int
            ImGui::Checkbox("int", &sendInt_);
            if (sendInt_) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("##int", &intValue_);
            }

            // Float
            ImGui::Checkbox("float", &sendFloat_);
            if (sendFloat_) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::InputFloat("##float", &floatValue_, 0.0f, 0.0f, "%.2f");
            }

            // String
            ImGui::Checkbox("string", &sendString_);
            if (sendString_) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                ImGui::InputText("##string", stringBuf_, sizeof(stringBuf_));
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Send button
            if (ImGui::Button("SEND MESSAGE", ImVec2(-1, 30))) {
                sendMessage();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Bundle feature
            ImGui::Text("Bundle (%d messages)", bundleMessageCount_);

            if (ImGui::Button("ADD TO BUNDLE", ImVec2(-1, 30))) {
                addToBundle();
            }

            // Only show send button when bundle has messages
            if (bundleMessageCount_ > 0) {
                if (ImGui::Button("SEND BUNDLE", ImVec2(-1, 30))) {
                    sendBundle();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(60, 30))) {
                    pendingBundle_.clear();
                    bundleMessageCount_ = 0;
                    addSendLog("[BUNDLE] Cleared");
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Send log
            ImGui::Text("Log:");
            ImGui::BeginChild("SendLog", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
                // No mutex needed (main thread only)
                for (const auto& msg : sendLogs_) {
                    ImGui::TextUnformatted(msg.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ==== Right pane: Receiver ====
        ImGui::BeginChild("Receiver", ImVec2(panelWidth, -30), true);
        {
            ImGui::Text("RECEIVER (Polling)");
            ImGui::Separator();
            ImGui::Spacing();

            // Clear button
            if (ImGui::Button("Clear")) {
                receiveLogs_.clear();
            }
            ImGui::Separator();

            // Receive log (scrollable)
            ImGui::BeginChild("ReceiveLog", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
                // No mutex needed (main thread only)
                for (const auto& msg : receiveLogs_) {
                    ImGui::TextUnformatted(msg.c_str());
                }
                // Auto scroll
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // Status bar
        ImGui::Separator();
        if (receiver_.isListening()) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Status: Listening on port %d (Polling)", port_);
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Status: Not listening");
        }
    }
    ImGui::End();

    // ImGui frame end
    imguiEnd();
}

void tcApp::cleanup() {
    sender_.close();
    receiver_.close();
    imguiShutdown();
}

void tcApp::keyPressed(int key) {
    // Send with Enter key
    if (key == KEY_ENTER) {
        sendMessage();
    }
}

OscMessage tcApp::createMessage() {
    OscMessage msg(addressBuf_);

    if (sendInt_) {
        msg.addInt(intValue_);
    }
    if (sendFloat_) {
        msg.addFloat(floatValue_);
    }
    if (sendString_) {
        msg.addString(stringBuf_);
    }

    return msg;
}

void tcApp::sendMessage() {
    OscMessage msg = createMessage();

    if (sender_.send(msg)) {
        addSendLog("[SENT] " + msg.toString());
    }
    else {
        addSendLog("[ERROR] Failed to send");
    }
}

void tcApp::addToBundle() {
    OscMessage msg = createMessage();
    pendingBundle_.addMessage(msg);
    bundleMessageCount_++;
    addSendLog("[BUNDLE+] " + msg.toString());
}

void tcApp::sendBundle() {
    if (bundleMessageCount_ == 0) return;

    if (sender_.send(pendingBundle_)) {
        addSendLog("[BUNDLE SENT] " + to_string(bundleMessageCount_) + " messages");
    }
    else {
        addSendLog("[ERROR] Failed to send bundle");
    }

    pendingBundle_.clear();
    bundleMessageCount_ = 0;
}

void tcApp::addSendLog(const string& msg) {
    // No mutex needed (main thread only)
    sendLogs_.push_back(msg);
    while (sendLogs_.size() > MAX_LOG_LINES) {
        sendLogs_.pop_front();
    }
}

void tcApp::addReceiveLog(const string& msg) {
    // No mutex needed (main thread only)
    receiveLogs_.push_back(msg);
    while (receiveLogs_.size() > MAX_LOG_LINES) {
        receiveLogs_.pop_front();
    }
}
