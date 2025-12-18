#pragma once

// tc::EasyCam - oF互換の3Dカメラ
// マウス操作で回転・ズーム・パンができるインタラクティブカメラ

#include <cmath>

namespace trussc {

class EasyCam {
public:
    EasyCam()
        : target_{0.0f, 0.0f, 0.0f}
        , distance_(400.0f)
        , rotationX_(0.0f)
        , rotationY_(0.0f)
        , fov_(0.785398f)  // 45度（ラジアン）
        , nearClip_(0.1f)
        , farClip_(10000.0f)
        , mouseInputEnabled_(true)
        , isDragging_(false)
        , isPanning_(false)
        , lastMouseX_(0.0f)
        , lastMouseY_(0.0f)
        , sensitivity_(1.0f)
        , zoomSensitivity_(10.0f)
        , panSensitivity_(1.0f)
    {}

    // ---------------------------------------------------------------------------
    // カメラ制御
    // ---------------------------------------------------------------------------

    // カメラモードを開始（3Dパースペクティブ + ビュー行列を設定）
    void begin() {
        // 3Dパイプラインを有効化
        if (internal::pipeline3dInitialized) {
            sgl_load_pipeline(internal::pipeline3d);
        }

        // プロジェクション行列を設定
        sgl_matrix_mode_projection();
        sgl_load_identity();

        float dpiScale = sapp_dpi_scale();
        float w = (float)sapp_width() / dpiScale;
        float h = (float)sapp_height() / dpiScale;
        float aspect = w / h;
        sgl_perspective(fov_, aspect, nearClip_, farClip_);

        // ビュー行列を設定（lookAtをシミュレート）
        sgl_matrix_mode_modelview();
        sgl_load_identity();

        // カメラ位置を計算（ターゲットからdistance_だけ離れた位置）
        float cosX = cos(rotationX_);
        float sinX = sin(rotationX_);
        float cosY = cos(rotationY_);
        float sinY = sin(rotationY_);

        // 球面座標からカメラ位置を計算
        float camX = target_.x + distance_ * sinY * cosX;
        float camY = target_.y + distance_ * sinX;
        float camZ = target_.z + distance_ * cosY * cosX;

        // lookAt 行列を手動で構築
        Vec3 eye = {camX, camY, camZ};
        Vec3 center = target_;
        Vec3 up = {0.0f, 1.0f, 0.0f};

        // lookAt計算
        Vec3 f = {center.x - eye.x, center.y - eye.y, center.z - eye.z};
        float fLen = sqrt(f.x*f.x + f.y*f.y + f.z*f.z);
        f.x /= fLen; f.y /= fLen; f.z /= fLen;

        // sをf×upで計算
        Vec3 s = {
            f.y * up.z - f.z * up.y,
            f.z * up.x - f.x * up.z,
            f.x * up.y - f.y * up.x
        };
        float sLen = sqrt(s.x*s.x + s.y*s.y + s.z*s.z);
        s.x /= sLen; s.y /= sLen; s.z /= sLen;

        // uをs×fで計算
        Vec3 u = {
            s.y * f.z - s.z * f.y,
            s.z * f.x - s.x * f.z,
            s.x * f.y - s.y * f.x
        };

        // lookAt行列を構築
        float m[16] = {
             s.x,  u.x, -f.x, 0.0f,
             s.y,  u.y, -f.y, 0.0f,
             s.z,  u.z, -f.z, 0.0f,
            -s.x*eye.x - s.y*eye.y - s.z*eye.z,
            -u.x*eye.x - u.y*eye.y - u.z*eye.z,
             f.x*eye.x + f.y*eye.y + f.z*eye.z,
            1.0f
        };

        sgl_load_matrix(m);
    }

    // カメラモードを終了（2D描画モードに戻す）
    void end() {
        sgl_load_default_pipeline();
        // 2D正射影に戻す
        beginFrame();
    }

    // カメラをリセット
    void reset() {
        target_ = {0.0f, 0.0f, 0.0f};
        distance_ = 400.0f;
        rotationX_ = 0.0f;
        rotationY_ = 0.0f;
    }

    // ---------------------------------------------------------------------------
    // パラメータ設定
    // ---------------------------------------------------------------------------

    // ターゲット位置を設定
    void setTarget(float x, float y, float z) {
        target_ = {x, y, z};
    }

    void setTarget(const Vec3& t) {
        target_ = t;
    }

    Vec3 getTarget() const {
        return target_;
    }

    // カメラとターゲットの距離を設定
    void setDistance(float d) {
        distance_ = d;
        if (distance_ < 0.1f) distance_ = 0.1f;
    }

    float getDistance() const {
        return distance_;
    }

    // 視野角（FOV）を設定（ラジアン）
    void setFov(float fov) {
        fov_ = fov;
    }

    float getFov() const {
        return fov_;
    }

    // 視野角（FOV）を度数で設定
    void setFovDeg(float degrees) {
        fov_ = degrees * PI / 180.0f;
    }

    // クリッピング面を設定
    void setNearClip(float nearClip) {
        nearClip_ = nearClip;
    }

    void setFarClip(float farClip) {
        farClip_ = farClip;
    }

    // 感度設定
    void setSensitivity(float s) {
        sensitivity_ = s;
    }

    void setZoomSensitivity(float s) {
        zoomSensitivity_ = s;
    }

    void setPanSensitivity(float s) {
        panSensitivity_ = s;
    }

    // ---------------------------------------------------------------------------
    // マウス操作
    // ---------------------------------------------------------------------------

    void enableMouseInput() {
        mouseInputEnabled_ = true;
    }

    void disableMouseInput() {
        mouseInputEnabled_ = false;
    }

    bool isMouseInputEnabled() const {
        return mouseInputEnabled_;
    }

    // マウスボタンが押された時（App::mousePressedから呼ぶ）
    void mousePressed(int x, int y, int button) {
        if (!mouseInputEnabled_) return;

        lastMouseX_ = (float)x;
        lastMouseY_ = (float)y;

        if (button == MOUSE_BUTTON_LEFT) {
            isDragging_ = true;
        } else if (button == MOUSE_BUTTON_MIDDLE) {
            isPanning_ = true;
        }
    }

    // マウスボタンが離された時（App::mouseReleasedから呼ぶ）
    void mouseReleased(int x, int y, int button) {
        if (button == MOUSE_BUTTON_LEFT) {
            isDragging_ = false;
        } else if (button == MOUSE_BUTTON_MIDDLE) {
            isPanning_ = false;
        }
    }

    // マウスドラッグ時（App::mouseDraggedから呼ぶ）
    void mouseDragged(int x, int y, int button) {
        if (!mouseInputEnabled_) return;

        float dx = (float)x - lastMouseX_;
        float dy = (float)y - lastMouseY_;

        if (isDragging_ && button == MOUSE_BUTTON_LEFT) {
            // 回転（Y上下ドラッグで仰角、X左右ドラッグで方位角）
            rotationY_ -= dx * 0.01f * sensitivity_;
            rotationX_ += dy * 0.01f * sensitivity_;  // 上下を直感的に

            // X軸回転を制限（真上・真下に近づきすぎると反転するので約80度で制限）
            float maxAngle = 1.4f;  // 約80度
            if (rotationX_ > maxAngle) rotationX_ = maxAngle;
            if (rotationX_ < -maxAngle) rotationX_ = -maxAngle;
        } else if (isPanning_ && button == MOUSE_BUTTON_MIDDLE) {
            // パン（XY平面上の移動）
            float cosY = cos(rotationY_);
            float sinY = sin(rotationY_);

            // カメラの右方向と上方向を計算
            float rightX = cosY;
            float rightZ = -sinY;

            // パン量をスケール
            float panX = dx * 0.5f * panSensitivity_;
            float panY = -dy * 0.5f * panSensitivity_;

            target_.x -= rightX * panX;
            target_.z -= rightZ * panX;
            target_.y += panY;
        }

        lastMouseX_ = (float)x;
        lastMouseY_ = (float)y;
    }

    // スクロール時（App::mouseScrolledから呼ぶ）
    void mouseScrolled(float dx, float dy) {
        if (!mouseInputEnabled_) return;

        // ズーム（距離を変更）
        distance_ -= dy * zoomSensitivity_;
        if (distance_ < 0.1f) distance_ = 0.1f;
    }

    // ---------------------------------------------------------------------------
    // カメラ情報取得
    // ---------------------------------------------------------------------------

    // カメラ位置を取得
    Vec3 getPosition() const {
        float cosX = cos(rotationX_);
        float sinX = sin(rotationX_);
        float cosY = cos(rotationY_);
        float sinY = sin(rotationY_);

        return {
            target_.x + distance_ * sinY * cosX,
            target_.y + distance_ * sinX,
            target_.z + distance_ * cosY * cosX
        };
    }

private:
    Vec3 target_;         // 注視点
    float distance_;      // ターゲットからの距離
    float rotationX_;     // X軸回転（仰角）
    float rotationY_;     // Y軸回転（方位角）

    float fov_;           // 視野角（ラジアン）
    float nearClip_;      // 近クリップ面
    float farClip_;       // 遠クリップ面

    bool mouseInputEnabled_;
    bool isDragging_;     // 左ボタンドラッグ中
    bool isPanning_;      // 中ボタンドラッグ中
    float lastMouseX_;
    float lastMouseY_;

    float sensitivity_;       // 回転感度
    float zoomSensitivity_;   // ズーム感度
    float panSensitivity_;    // パン感度
};

} // namespace trussc
