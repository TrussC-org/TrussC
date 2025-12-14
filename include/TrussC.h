#pragma once

// =============================================================================
// TrussC - Thin, Modern, and Native Creative Coding Framework
// Version 0.0.1
// =============================================================================

// sokol ヘッダー
#include "sokol/sokol_log.h"
#define SOKOL_NO_ENTRY  // main() を自分で定義するため
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_gl.h"

// 標準ライブラリ
#include <cstdint>
#include <cmath>
#include <string>

// =============================================================================
// trussc 名前空間
// =============================================================================
namespace trussc {

// バージョン情報
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 1;

// 数学定数
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = PI * 2.0f;
constexpr float HALF_PI = PI / 2.0f;

// ---------------------------------------------------------------------------
// LoopMode
// ---------------------------------------------------------------------------
enum class LoopMode {
    Game,   // 毎フレーム自動的にupdate/drawが呼ばれる（デフォルト）
    Demand  // redraw()が呼ばれた時だけupdate/drawが呼ばれる（省電力モード）
};

// ---------------------------------------------------------------------------
// 内部状態
// ---------------------------------------------------------------------------
namespace internal {
    // 現在の描画色
    inline float currentR = 1.0f;
    inline float currentG = 1.0f;
    inline float currentB = 1.0f;
    inline float currentA = 1.0f;

    // 塗りつぶし / ストローク
    inline bool fillEnabled = true;
    inline bool strokeEnabled = false;
    inline float strokeWeight = 1.0f;

    // 円の分割数
    inline int circleResolution = 32;

    // LoopMode
    inline LoopMode loopMode = LoopMode::Game;
    inline bool needsRedraw = true;

    // マウス状態
    inline float mouseX = 0.0f;
    inline float mouseY = 0.0f;
    inline float pmouseX = 0.0f;  // 前フレームのマウス位置
    inline float pmouseY = 0.0f;
    inline int mouseButton = -1;  // 現在押されているボタン (-1 = なし)
    inline bool mousePressed = false;
}

// ---------------------------------------------------------------------------
// 初期化・終了
// ---------------------------------------------------------------------------

// sokol_gfx + sokol_gl を初期化（setup コールバック内で呼ぶ）
inline void setup() {
    // sokol_gfx 初期化
    sg_desc sgdesc = {};
    sgdesc.environment = sglue_environment();
    sgdesc.logger.func = slog_func;
    sg_setup(&sgdesc);

    // sokol_gl 初期化
    sgl_desc_t sgldesc = {};
    sgldesc.logger.func = slog_func;
    sgl_setup(&sgldesc);
}

// sokol_gfx + sokol_gl を終了（cleanup コールバック内で呼ぶ）
inline void cleanup() {
    sgl_shutdown();
    sg_shutdown();
}

// ---------------------------------------------------------------------------
// フレーム制御
// ---------------------------------------------------------------------------

// フレーム開始時に呼ぶ（clearの前に）
inline void beginFrame() {
    // デフォルトのビューポート設定
    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

// 画面クリア (RGB float: 0.0 ~ 1.0)
inline void clear(float r, float g, float b, float a = 1.0f) {
    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = { r, g, b, a };
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
}

// 画面クリア (グレースケール)
inline void clear(float gray, float a = 1.0f) {
    clear(gray, gray, gray, a);
}

// 画面クリア (8bit RGB: 0 ~ 255)
inline void clear(int r, int g, int b, int a = 255) {
    clear(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

// パス終了 & コミット（draw の最後で呼ぶ）
inline void present() {
    sgl_draw();
    sg_end_pass();
    sg_commit();
}

// ---------------------------------------------------------------------------
// 色設定
// ---------------------------------------------------------------------------

// 描画色設定 (float: 0.0 ~ 1.0)
inline void setColor(float r, float g, float b, float a = 1.0f) {
    internal::currentR = r;
    internal::currentG = g;
    internal::currentB = b;
    internal::currentA = a;
}

// 描画色設定 (int: 0 ~ 255)
inline void setColor(int r, int g, int b, int a = 255) {
    setColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

// グレースケール
inline void setColor(float gray, float a = 1.0f) {
    setColor(gray, gray, gray, a);
}

inline void setColor(int gray, int a = 255) {
    setColor(gray, gray, gray, a);
}

// 塗りつぶしを有効化
inline void fill() {
    internal::fillEnabled = true;
}

// 塗りつぶしを無効化
inline void noFill() {
    internal::fillEnabled = false;
}

// ストロークを有効化
inline void stroke() {
    internal::strokeEnabled = true;
}

// ストロークを無効化
inline void noStroke() {
    internal::strokeEnabled = false;
}

// ストロークの太さ
inline void setStrokeWeight(float weight) {
    internal::strokeWeight = weight;
}

// ---------------------------------------------------------------------------
// 変形
// ---------------------------------------------------------------------------

inline void pushMatrix() {
    sgl_push_matrix();
}

inline void popMatrix() {
    sgl_pop_matrix();
}

inline void translate(float x, float y) {
    sgl_translate(x, y, 0.0f);
}

inline void rotate(float radians) {
    sgl_rotate(radians, 0.0f, 0.0f, 1.0f);
}

// 度数法でも回転できる
inline void rotateDeg(float degrees) {
    rotate(degrees * PI / 180.0f);
}

inline void scale(float s) {
    sgl_scale(s, s, 1.0f);
}

inline void scale(float sx, float sy) {
    sgl_scale(sx, sy, 1.0f);
}

// ---------------------------------------------------------------------------
// 基本図形描画
// ---------------------------------------------------------------------------

// 四角形 (左上座標 + サイズ)
inline void drawRect(float x, float y, float w, float h) {
    if (internal::fillEnabled) {
        sgl_begin_quads();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x, y);
        sgl_v2f(x + w, y);
        sgl_v2f(x + w, y + h);
        sgl_v2f(x, y + h);
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x, y);
        sgl_v2f(x + w, y);
        sgl_v2f(x + w, y + h);
        sgl_v2f(x, y + h);
        sgl_v2f(x, y);
        sgl_end();
    }
}

// 円
inline void drawCircle(float cx, float cy, float radius) {
    int segments = internal::circleResolution;

    if (internal::fillEnabled) {
        sgl_begin_triangle_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TWO_PI;
            float px = cx + cos(angle) * radius;
            float py = cy + sin(angle) * radius;
            sgl_v2f(cx, cy);
            sgl_v2f(px, py);
        }
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TWO_PI;
            float px = cx + cos(angle) * radius;
            float py = cy + sin(angle) * radius;
            sgl_v2f(px, py);
        }
        sgl_end();
    }
}

// 楕円
inline void drawEllipse(float cx, float cy, float rx, float ry) {
    int segments = internal::circleResolution;

    if (internal::fillEnabled) {
        sgl_begin_triangle_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TWO_PI;
            float px = cx + cos(angle) * rx;
            float py = cy + sin(angle) * ry;
            sgl_v2f(cx, cy);
            sgl_v2f(px, py);
        }
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TWO_PI;
            float px = cx + cos(angle) * rx;
            float py = cy + sin(angle) * ry;
            sgl_v2f(px, py);
        }
        sgl_end();
    }
}

// 線
inline void drawLine(float x1, float y1, float x2, float y2) {
    sgl_begin_lines();
    sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
    sgl_v2f(x1, y1);
    sgl_v2f(x2, y2);
    sgl_end();
}

// 三角形
inline void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    if (internal::fillEnabled) {
        sgl_begin_triangles();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x1, y1);
        sgl_v2f(x2, y2);
        sgl_v2f(x3, y3);
        sgl_end();
    }
    if (internal::strokeEnabled) {
        sgl_begin_line_strip();
        sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
        sgl_v2f(x1, y1);
        sgl_v2f(x2, y2);
        sgl_v2f(x3, y3);
        sgl_v2f(x1, y1);
        sgl_end();
    }
}

// 点
inline void drawPoint(float x, float y) {
    sgl_begin_points();
    sgl_c4f(internal::currentR, internal::currentG, internal::currentB, internal::currentA);
    sgl_v2f(x, y);
    sgl_end();
}

// 円の分割数を設定
inline void setCircleResolution(int res) {
    internal::circleResolution = res;
}

// ---------------------------------------------------------------------------
// ウィンドウ制御
// ---------------------------------------------------------------------------

// ウィンドウタイトルを設定
inline void setWindowTitle(const std::string& title) {
    sapp_set_window_title(title.c_str());
}

// フルスクリーン切り替え
inline void setFullscreen(bool full) {
    if (full != sapp_is_fullscreen()) {
        sapp_toggle_fullscreen();
    }
}

// フルスクリーン状態を取得
inline bool isFullscreen() {
    return sapp_is_fullscreen();
}

// フルスクリーンをトグル
inline void toggleFullscreen() {
    sapp_toggle_fullscreen();
}

// ---------------------------------------------------------------------------
// ウィンドウ情報
// ---------------------------------------------------------------------------

inline int getWindowWidth() {
    return sapp_width();
}

inline int getWindowHeight() {
    return sapp_height();
}

inline float getAspectRatio() {
    return static_cast<float>(sapp_width()) / static_cast<float>(sapp_height());
}

// ---------------------------------------------------------------------------
// 時間
// ---------------------------------------------------------------------------

inline double getElapsedTime() {
    return sapp_frame_count() * sapp_frame_duration();
}

inline uint64_t getFrameCount() {
    return sapp_frame_count();
}

inline double getDeltaTime() {
    return sapp_frame_duration();
}

// ---------------------------------------------------------------------------
// マウス状態（グローバル / ウィンドウ座標）
// ---------------------------------------------------------------------------

// 現在のマウスX座標（ウィンドウ座標）
inline float getGlobalMouseX() {
    return internal::mouseX;
}

// 現在のマウスY座標（ウィンドウ座標）
inline float getGlobalMouseY() {
    return internal::mouseY;
}

// 前フレームのマウスX座標（ウィンドウ座標）
inline float getGlobalPMouseX() {
    return internal::pmouseX;
}

// 前フレームのマウスY座標（ウィンドウ座標）
inline float getGlobalPMouseY() {
    return internal::pmouseY;
}

// マウスボタンが押されているか
inline bool isMousePressed() {
    return internal::mousePressed;
}

// 現在押されているマウスボタン (-1 = なし)
inline int getMouseButton() {
    return internal::mouseButton;
}

// ---------------------------------------------------------------------------
// LoopMode 制御
// ---------------------------------------------------------------------------

// LoopModeを設定
inline void setLoopMode(LoopMode mode) {
    internal::loopMode = mode;
    if (mode == LoopMode::Game) {
        internal::needsRedraw = true;
    }
}

// 現在のLoopModeを取得
inline LoopMode getLoopMode() {
    return internal::loopMode;
}

// 再描画をリクエスト（Demandモードで使用）
inline void redraw() {
    internal::needsRedraw = true;
}

// ---------------------------------------------------------------------------
// 数学ユーティリティ
// ---------------------------------------------------------------------------

inline float map(float value, float start1, float stop1, float start2, float stop2) {
    return start2 + (stop2 - start2) * ((value - start1) / (stop1 - start1));
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

inline float radians(float degrees) {
    return degrees * PI / 180.0f;
}

inline float degrees(float radians) {
    return radians * 180.0f / PI;
}

// ---------------------------------------------------------------------------
// キーコード定数（sokol_app のキーコードをラップ）
// ---------------------------------------------------------------------------

// 特殊キー
constexpr int KEY_SPACE = SAPP_KEYCODE_SPACE;
constexpr int KEY_ESCAPE = SAPP_KEYCODE_ESCAPE;
constexpr int KEY_ENTER = SAPP_KEYCODE_ENTER;
constexpr int KEY_TAB = SAPP_KEYCODE_TAB;
constexpr int KEY_BACKSPACE = SAPP_KEYCODE_BACKSPACE;
constexpr int KEY_DELETE = SAPP_KEYCODE_DELETE;

// 矢印キー
constexpr int KEY_RIGHT = SAPP_KEYCODE_RIGHT;
constexpr int KEY_LEFT = SAPP_KEYCODE_LEFT;
constexpr int KEY_DOWN = SAPP_KEYCODE_DOWN;
constexpr int KEY_UP = SAPP_KEYCODE_UP;

// 修飾キー
constexpr int KEY_LEFT_SHIFT = SAPP_KEYCODE_LEFT_SHIFT;
constexpr int KEY_RIGHT_SHIFT = SAPP_KEYCODE_RIGHT_SHIFT;
constexpr int KEY_LEFT_CONTROL = SAPP_KEYCODE_LEFT_CONTROL;
constexpr int KEY_RIGHT_CONTROL = SAPP_KEYCODE_RIGHT_CONTROL;
constexpr int KEY_LEFT_ALT = SAPP_KEYCODE_LEFT_ALT;
constexpr int KEY_RIGHT_ALT = SAPP_KEYCODE_RIGHT_ALT;
constexpr int KEY_LEFT_SUPER = SAPP_KEYCODE_LEFT_SUPER;
constexpr int KEY_RIGHT_SUPER = SAPP_KEYCODE_RIGHT_SUPER;

// ファンクションキー
constexpr int KEY_F1 = SAPP_KEYCODE_F1;
constexpr int KEY_F2 = SAPP_KEYCODE_F2;
constexpr int KEY_F3 = SAPP_KEYCODE_F3;
constexpr int KEY_F4 = SAPP_KEYCODE_F4;
constexpr int KEY_F5 = SAPP_KEYCODE_F5;
constexpr int KEY_F6 = SAPP_KEYCODE_F6;
constexpr int KEY_F7 = SAPP_KEYCODE_F7;
constexpr int KEY_F8 = SAPP_KEYCODE_F8;
constexpr int KEY_F9 = SAPP_KEYCODE_F9;
constexpr int KEY_F10 = SAPP_KEYCODE_F10;
constexpr int KEY_F11 = SAPP_KEYCODE_F11;
constexpr int KEY_F12 = SAPP_KEYCODE_F12;

// マウスボタン
constexpr int MOUSE_BUTTON_LEFT = SAPP_MOUSEBUTTON_LEFT;
constexpr int MOUSE_BUTTON_RIGHT = SAPP_MOUSEBUTTON_RIGHT;
constexpr int MOUSE_BUTTON_MIDDLE = SAPP_MOUSEBUTTON_MIDDLE;

// ---------------------------------------------------------------------------
// ウィンドウ設定
// ---------------------------------------------------------------------------

struct WindowSettings {
    int width = 1280;
    int height = 720;
    std::string title = "TrussC App";
    bool highDpi = true;
    int sampleCount = 4;  // MSAA
    bool fullscreen = false;
    // bool headless = false;  // 将来用

    WindowSettings& setSize(int w, int h) {
        width = w;
        height = h;
        return *this;
    }

    WindowSettings& setTitle(const std::string& t) {
        title = t;
        return *this;
    }

    WindowSettings& setHighDpi(bool enabled) {
        highDpi = enabled;
        return *this;
    }

    WindowSettings& setSampleCount(int count) {
        sampleCount = count;
        return *this;
    }

    WindowSettings& setFullscreen(bool enabled) {
        fullscreen = enabled;
        return *this;
    }
};

// ---------------------------------------------------------------------------
// アプリケーション実行（内部実装）
// ---------------------------------------------------------------------------

namespace internal {
    // アプリインスタンス（void* で保持）
    inline void* appInstance = nullptr;
    inline int currentMouseButton = -1;

    // コールバック関数ポインタ
    inline void (*appSetupFunc)() = nullptr;
    inline void (*appUpdateFunc)() = nullptr;
    inline void (*appDrawFunc)() = nullptr;
    inline void (*appCleanupFunc)() = nullptr;
    inline void (*appKeyPressedFunc)(int) = nullptr;
    inline void (*appKeyReleasedFunc)(int) = nullptr;
    inline void (*appMousePressedFunc)(int, int, int) = nullptr;
    inline void (*appMouseReleasedFunc)(int, int, int) = nullptr;
    inline void (*appMouseMovedFunc)(int, int) = nullptr;
    inline void (*appMouseDraggedFunc)(int, int, int) = nullptr;
    inline void (*appMouseScrolledFunc)(float, float) = nullptr;
    inline void (*appWindowResizedFunc)(int, int) = nullptr;

    inline void _setup_cb() {
        setup();
        if (appSetupFunc) appSetupFunc();
    }

    inline void _frame_cb() {
        if (loopMode == LoopMode::Game || needsRedraw) {
            beginFrame();
            if (appUpdateFunc) appUpdateFunc();
            if (appDrawFunc) appDrawFunc();
            present();
            needsRedraw = false;
        }
        pmouseX = mouseX;
        pmouseY = mouseY;
    }

    inline void _cleanup_cb() {
        if (appCleanupFunc) appCleanupFunc();
        cleanup();
    }

    inline void _event_cb(const sapp_event* ev) {
        switch (ev->type) {
            case SAPP_EVENTTYPE_KEY_DOWN:
                if (!ev->key_repeat && appKeyPressedFunc) {
                    appKeyPressedFunc(ev->key_code);
                }
                break;
            case SAPP_EVENTTYPE_KEY_UP:
                if (appKeyReleasedFunc) appKeyReleasedFunc(ev->key_code);
                break;
            case SAPP_EVENTTYPE_MOUSE_DOWN:
                currentMouseButton = ev->mouse_button;
                mouseX = ev->mouse_x;
                mouseY = ev->mouse_y;
                mouseButton = ev->mouse_button;
                mousePressed = true;
                if (appMousePressedFunc) appMousePressedFunc((int)ev->mouse_x, (int)ev->mouse_y, ev->mouse_button);
                break;
            case SAPP_EVENTTYPE_MOUSE_UP:
                currentMouseButton = -1;
                mouseX = ev->mouse_x;
                mouseY = ev->mouse_y;
                mouseButton = -1;
                mousePressed = false;
                if (appMouseReleasedFunc) appMouseReleasedFunc((int)ev->mouse_x, (int)ev->mouse_y, ev->mouse_button);
                break;
            case SAPP_EVENTTYPE_MOUSE_MOVE:
                mouseX = ev->mouse_x;
                mouseY = ev->mouse_y;
                if (currentMouseButton >= 0) {
                    if (appMouseDraggedFunc) appMouseDraggedFunc((int)ev->mouse_x, (int)ev->mouse_y, currentMouseButton);
                } else {
                    if (appMouseMovedFunc) appMouseMovedFunc((int)ev->mouse_x, (int)ev->mouse_y);
                }
                break;
            case SAPP_EVENTTYPE_MOUSE_SCROLL:
                if (appMouseScrolledFunc) appMouseScrolledFunc(ev->scroll_x, ev->scroll_y);
                break;
            case SAPP_EVENTTYPE_RESIZED:
                if (appWindowResizedFunc) appWindowResizedFunc(ev->window_width, ev->window_height);
                break;
            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// アプリケーション実行
// ---------------------------------------------------------------------------

template<typename AppClass>
int runApp(const WindowSettings& settings = WindowSettings()) {
    // アプリインスタンスを生成
    static AppClass* app = nullptr;

    // コールバックを設定
    internal::appSetupFunc = []() {
        app = new AppClass();
        app->setup();
    };
    internal::appUpdateFunc = []() {
        if (app) app->updateTree();  // シーングラフ全体を更新
    };
    internal::appDrawFunc = []() {
        if (app) app->drawTree();  // シーングラフ全体を描画
    };
    internal::appCleanupFunc = []() {
        if (app) {
            app->cleanup();
            delete app;
            app = nullptr;
        }
    };
    internal::appKeyPressedFunc = [](int key) {
        if (app) app->keyPressed(key);
    };
    internal::appKeyReleasedFunc = [](int key) {
        if (app) app->keyReleased(key);
    };
    internal::appMousePressedFunc = [](int x, int y, int button) {
        if (app) app->mousePressed(x, y, button);
    };
    internal::appMouseReleasedFunc = [](int x, int y, int button) {
        if (app) app->mouseReleased(x, y, button);
    };
    internal::appMouseMovedFunc = [](int x, int y) {
        if (app) app->mouseMoved(x, y);
    };
    internal::appMouseDraggedFunc = [](int x, int y, int button) {
        if (app) app->mouseDragged(x, y, button);
    };
    internal::appMouseScrolledFunc = [](float dx, float dy) {
        if (app) app->mouseScrolled(dx, dy);
    };
    internal::appWindowResizedFunc = [](int w, int h) {
        if (app) app->windowResized(w, h);
    };

    // sapp_desc を構築
    sapp_desc desc = {};
    desc.width = settings.width;
    desc.height = settings.height;
    desc.window_title = settings.title.c_str();
    desc.high_dpi = settings.highDpi;
    desc.sample_count = settings.sampleCount;
    desc.fullscreen = settings.fullscreen;
    desc.init_cb = internal::_setup_cb;
    desc.frame_cb = internal::_frame_cb;
    desc.cleanup_cb = internal::_cleanup_cb;
    desc.event_cb = internal::_event_cb;
    desc.logger.func = slog_func;

    // アプリを実行
    sapp_run(&desc);

    return 0;
}

} // namespace trussc

// 短縮エイリアス
namespace tc = trussc;
