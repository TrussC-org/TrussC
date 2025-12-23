// =============================================================================
// tcApp.cpp - OSC Event-based Example
// =============================================================================
// This sample receives OSC using "event handler" style.
//
// [IMPORTANT] About async processing and thread safety
// OSC reception runs on a separate thread, so event handlers are called
// from a different thread than the main thread (update/draw).
// Therefore, mutex protection is required when accessing shared data.
//
// Example: This sample protects receiveLogs_ with mutex
//   - addReceiveLog(): writes with lock_guard<mutex>
//   - draw(): reads with lock_guard<mutex>
//
// For synchronous processing, see example-osc-polling.
// =============================================================================

#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    // Initialize ImGui
    imguiSetup();

    // ---------------------------------------------------------------------------
    // Register receive events
    // ---------------------------------------------------------------------------
    // [NOTE] Listener must be stored or it will be unregistered immediately!
    // [NOTE] This callback is called from a different thread!
    //        Always use mutex when accessing shared data
    // ---------------------------------------------------------------------------

    // Lambda version
    receiver_.onMessageReceived.listen(messageListener_, [this](OscMessage& msg) {
        // This is called from the receive thread!
        addReceiveLog("[RECEIVED] " + msg.toString());  // mutex-protected function
    });

    // Member function pointer version (oF style)
    receiver_.onParseError.listen(errorListener_, this, &tcApp::onParseError);

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
    // No update processing needed
}

void tcApp::draw() {
    clear(0.12f);

    // ImGui フレーム開始
    imguiBegin();

    // ImGui ウィンドウを画面全体に配置
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)getWindowWidth(), (float)getWindowHeight()));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("OSC Example", nullptr, flags)) {
        // タイトル
        ImGui::Text("OSC Example - Port %d", port_);
        ImGui::Separator();

        // 左右ペイン
        float panelWidth = (ImGui::GetContentRegionAvail().x - 20) / 2;

        // ==== 左ペイン: 送信 ====
        ImGui::BeginChild("Sender", ImVec2(panelWidth, -30), true);
        {
            ImGui::Text("SENDER");
            ImGui::Separator();
            ImGui::Spacing();

            // アドレス入力
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

            // 送信ボタン
            if (ImGui::Button("SEND MESSAGE", ImVec2(-1, 30))) {
                sendMessage();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // バンドル機能
            ImGui::Text("Bundle (%d messages)", bundleMessageCount_);

            if (ImGui::Button("ADD TO BUNDLE", ImVec2(-1, 30))) {
                addToBundle();
            }

            // バンドルにメッセージがある時だけ送信ボタン表示
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

            // 送信ログ
            ImGui::Text("Log:");
            ImGui::BeginChild("SendLog", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
                lock_guard<mutex> lock(sendLogMutex_);
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

        // ==== 右ペイン: 受信 ====
        ImGui::BeginChild("Receiver", ImVec2(panelWidth, -30), true);
        {
            ImGui::Text("RECEIVER");
            ImGui::Separator();
            ImGui::Spacing();

            // クリアボタン
            if (ImGui::Button("Clear")) {
                lock_guard<mutex> lock(receiveLogMutex_);
                receiveLogs_.clear();
            }
            ImGui::Separator();

            // 受信ログ（スクロール可能）
            ImGui::BeginChild("ReceiveLog", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
                lock_guard<mutex> lock(receiveLogMutex_);
                for (const auto& msg : receiveLogs_) {
                    ImGui::TextUnformatted(msg.c_str());
                }
                // 自動スクロール
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // ステータスバー
        ImGui::Separator();
        if (receiver_.isListening()) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Status: Listening on port %d", port_);
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Status: Not listening");
        }
    }
    ImGui::End();

    // ImGui フレーム終了
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
    lock_guard<mutex> lock(sendLogMutex_);
    sendLogs_.push_back(msg);
    while (sendLogs_.size() > MAX_LOG_LINES) {
        sendLogs_.pop_front();
    }
}

void tcApp::addReceiveLog(const string& msg) {
    lock_guard<mutex> lock(receiveLogMutex_);
    receiveLogs_.push_back(msg);
    while (receiveLogs_.size() > MAX_LOG_LINES) {
        receiveLogs_.pop_front();
    }
}

void tcApp::onParseError(string& error) {
    addReceiveLog("[ERROR] " + error);
}
