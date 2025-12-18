#pragma once

// =============================================================================
// tcImGui.h - Dear ImGui 統合
// sokol_imgui のラッパー
// =============================================================================

#include "imgui/imgui.h"
#include "sokol/sokol_imgui.h"
#include "tc/utils/tcLog.h"

namespace trussc {

// internal 名前空間の imguiEnabled フラグへのアクセス
namespace internal {
    extern bool imguiEnabled;
}

// ---------------------------------------------------------------------------
// ImGui 管理クラス
// ---------------------------------------------------------------------------
class ImGuiManager {
public:
    // 初期化（setup で呼ぶ）
    void setup() {
        if (initialized_) return;

        simgui_desc_t desc = {};
        desc.logger.func = slog_func;
        simgui_setup(&desc);

        initialized_ = true;
        internal::imguiEnabled = true;
        tcLogVerbose() << "ImGui 初期化完了";
    }

    // 終了処理（自動で呼ばれる）
    void shutdown() {
        if (!initialized_) return;
        simgui_shutdown();
        initialized_ = false;
        internal::imguiEnabled = false;
        tcLogVerbose() << "ImGui 終了";
    }

    // フレーム開始（draw の最初に呼ぶ）
    void begin(int width, int height, float deltaTime) {
        if (!initialized_) return;

        simgui_frame_desc_t desc = {};
        desc.width = width;
        desc.height = height;
        desc.delta_time = deltaTime;
        desc.dpi_scale = 1.0f;  // TODO: DPI スケーリング対応
        simgui_new_frame(&desc);
    }

    // フレーム終了（draw の最後に呼ぶ）
    void end() {
        if (!initialized_) return;
        simgui_render();
    }

    // イベント処理（内部で自動呼び出し）
    bool handleEvent(const sapp_event* event) {
        if (!initialized_) return false;
        return simgui_handle_event(event);
    }

    // 初期化済みか
    bool isInitialized() const { return initialized_; }

    // シングルトンアクセス
    static ImGuiManager& instance() {
        static ImGuiManager mgr;
        return mgr;
    }

private:
    ImGuiManager() = default;
    ~ImGuiManager() { shutdown(); }

    bool initialized_ = false;
};

// ---------------------------------------------------------------------------
// 便利関数
// ---------------------------------------------------------------------------

// ImGui 初期化
inline void imguiSetup() {
    ImGuiManager::instance().setup();
}

// ImGui 終了
inline void imguiShutdown() {
    ImGuiManager::instance().shutdown();
}

// フレーム開始
inline void imguiBegin() {
    int w = sapp_width();
    int h = sapp_height();
    float dt = static_cast<float>(sapp_frame_duration());
    ImGuiManager::instance().begin(w, h, dt);
}

// フレーム終了（描画）
inline void imguiEnd() {
    ImGuiManager::instance().end();
}

// イベント処理（内部用）
inline bool imguiHandleEvent(const sapp_event* event) {
    return ImGuiManager::instance().handleEvent(event);
}

// ImGui がマウス入力を使用中か
inline bool imguiWantsMouse() {
    return ImGui::GetIO().WantCaptureMouse;
}

// ImGui がキーボード入力を使用中か
inline bool imguiWantsKeyboard() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace trussc
