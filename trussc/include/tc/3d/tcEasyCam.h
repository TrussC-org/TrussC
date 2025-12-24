#pragma once

// tc::EasyCam - oF-compatible 3D camera
// Interactive camera with mouse-controlled rotation, zoom, and pan

#include <cmath>

namespace trussc {

class EasyCam {
public:
    EasyCam()
        : target_{0.0f, 0.0f, 0.0f}
        , distance_(400.0f)
        , rotationX_(0.0f)
        , rotationY_(0.0f)
        , fov_(0.785398f)  // 45 degrees (radians)
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
    // Camera control
    // ---------------------------------------------------------------------------

    // Start camera mode (set 3D perspective + view matrix)
    void begin() {
        // Enable 3D pipeline
        if (internal::pipeline3dInitialized) {
            sgl_load_pipeline(internal::pipeline3d);
        }

        // Set projection matrix
        sgl_matrix_mode_projection();
        sgl_load_identity();

        float dpiScale = sapp_dpi_scale();
        float w = (float)sapp_width() / dpiScale;
        float h = (float)sapp_height() / dpiScale;
        float aspect = w / h;
        sgl_perspective(fov_, aspect, nearClip_, farClip_);

        // Set view matrix (simulate lookAt)
        sgl_matrix_mode_modelview();
        sgl_load_identity();

        // Calculate camera position (distance_ away from target)
        float cosX = cos(rotationX_);
        float sinX = sin(rotationX_);
        float cosY = cos(rotationY_);
        float sinY = sin(rotationY_);

        // Calculate camera position from spherical coordinates
        float camX = target_.x + distance_ * sinY * cosX;
        float camY = target_.y + distance_ * sinX;
        float camZ = target_.z + distance_ * cosY * cosX;

        // Manually construct lookAt matrix
        Vec3 eye = {camX, camY, camZ};
        Vec3 center = target_;
        Vec3 up = {0.0f, 1.0f, 0.0f};

        // lookAt calculation
        Vec3 f = {center.x - eye.x, center.y - eye.y, center.z - eye.z};
        float fLen = sqrt(f.x*f.x + f.y*f.y + f.z*f.z);
        f.x /= fLen; f.y /= fLen; f.z /= fLen;

        // Calculate s = f × up
        Vec3 s = {
            f.y * up.z - f.z * up.y,
            f.z * up.x - f.x * up.z,
            f.x * up.y - f.y * up.x
        };
        float sLen = sqrt(s.x*s.x + s.y*s.y + s.z*s.z);
        s.x /= sLen; s.y /= sLen; s.z /= sLen;

        // Calculate u = s × f
        Vec3 u = {
            s.y * f.z - s.z * f.y,
            s.z * f.x - s.x * f.z,
            s.x * f.y - s.y * f.x
        };

        // Construct lookAt matrix
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

    // End camera mode (return to 2D drawing mode)
    void end() {
        sgl_load_default_pipeline();
        // Return to 2D orthographic projection
        beginFrame();
    }

    // Reset camera
    void reset() {
        target_ = {0.0f, 0.0f, 0.0f};
        distance_ = 400.0f;
        rotationX_ = 0.0f;
        rotationY_ = 0.0f;
    }

    // ---------------------------------------------------------------------------
    // Parameter settings
    // ---------------------------------------------------------------------------

    // Set target position
    void setTarget(float x, float y, float z) {
        target_ = {x, y, z};
    }

    void setTarget(const Vec3& t) {
        target_ = t;
    }

    Vec3 getTarget() const {
        return target_;
    }

    // Set distance between camera and target
    void setDistance(float d) {
        distance_ = d;
        if (distance_ < 0.1f) distance_ = 0.1f;
    }

    float getDistance() const {
        return distance_;
    }

    // Set field of view (FOV) in radians
    void setFov(float fov) {
        fov_ = fov;
    }

    float getFov() const {
        return fov_;
    }

    // Set field of view (FOV) in degrees
    void setFovDeg(float degrees) {
        fov_ = deg2rad(degrees);
    }

    // Set clipping planes
    void setNearClip(float nearClip) {
        nearClip_ = nearClip;
    }

    void setFarClip(float farClip) {
        farClip_ = farClip;
    }

    // Sensitivity settings
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
    // Mouse input
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

    // When mouse button pressed (call from App::mousePressed)
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

    // When mouse button released (call from App::mouseReleased)
    void mouseReleased(int x, int y, int button) {
        if (button == MOUSE_BUTTON_LEFT) {
            isDragging_ = false;
        } else if (button == MOUSE_BUTTON_MIDDLE) {
            isPanning_ = false;
        }
    }

    // When mouse dragged (call from App::mouseDragged)
    void mouseDragged(int x, int y, int button) {
        if (!mouseInputEnabled_) return;

        float dx = (float)x - lastMouseX_;
        float dy = (float)y - lastMouseY_;

        if (isDragging_ && button == MOUSE_BUTTON_LEFT) {
            // Rotation (Y drag for elevation, X drag for azimuth)
            rotationY_ -= dx * 0.01f * sensitivity_;
            rotationX_ += dy * 0.01f * sensitivity_;  // Intuitive up/down

            // Limit X-axis rotation (restrict to ~80 degrees to prevent flipping near poles)
            float maxAngle = 1.4f;  // ~80 degrees
            if (rotationX_ > maxAngle) rotationX_ = maxAngle;
            if (rotationX_ < -maxAngle) rotationX_ = -maxAngle;
        } else if (isPanning_ && button == MOUSE_BUTTON_MIDDLE) {
            // Pan (movement in XY plane)
            float cosY = cos(rotationY_);
            float sinY = sin(rotationY_);

            // Calculate camera's right and up directions
            float rightX = cosY;
            float rightZ = -sinY;

            // Scale pan amount
            float panX = dx * 0.5f * panSensitivity_;
            float panY = -dy * 0.5f * panSensitivity_;

            target_.x -= rightX * panX;
            target_.z -= rightZ * panX;
            target_.y += panY;
        }

        lastMouseX_ = (float)x;
        lastMouseY_ = (float)y;
    }

    // When scrolled (call from App::mouseScrolled)
    void mouseScrolled(float dx, float dy) {
        if (!mouseInputEnabled_) return;

        // Zoom (change distance)
        distance_ -= dy * zoomSensitivity_;
        if (distance_ < 0.1f) distance_ = 0.1f;
    }

    // ---------------------------------------------------------------------------
    // Camera info
    // ---------------------------------------------------------------------------

    // Get camera position
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
    Vec3 target_;         // Look-at target
    float distance_;      // Distance from target
    float rotationX_;     // X-axis rotation (elevation)
    float rotationY_;     // Y-axis rotation (azimuth)

    float fov_;           // Field of view (radians)
    float nearClip_;      // Near clipping plane
    float farClip_;       // Far clipping plane

    bool mouseInputEnabled_;
    bool isDragging_;     // Left button dragging
    bool isPanning_;      // Middle button dragging
    float lastMouseX_;
    float lastMouseY_;

    float sensitivity_;       // Rotation sensitivity
    float zoomSensitivity_;   // Zoom sensitivity
    float panSensitivity_;    // Pan sensitivity
};

} // namespace trussc
