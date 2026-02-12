#pragma once

// tc::EasyCam - oF-compatible 3D camera
// Interactive camera with mouse-controlled rotation, zoom, and pan

#include <cmath>
#include "tc/events/tcCoreEvents.h"

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
        , mouseInputEnabled_(false)  // Call enableMouseInput() to enable
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

        float dpiScale = sapp_dpi_scale();
        float w = (float)sapp_width() / dpiScale;
        float h = (float)sapp_height() / dpiScale;
        float aspect = w / h;

        // Calculate camera position from spherical coordinates
        float cosX = cos(rotationX_);
        float sinX = sin(rotationX_);
        float cosY = cos(rotationY_);
        float sinY = sin(rotationY_);
        Vec3 eye = {
            target_.x + distance_ * sinY * cosX,
            target_.y + distance_ * sinX,
            target_.z + distance_ * cosY * cosX
        };
        Vec3 up = {0.0f, 1.0f, 0.0f};

        // Create matrices using Mat4 (row-major)
        Mat4 projection = Mat4::perspective(fov_, aspect, nearClip_, farClip_);
        Mat4 view = Mat4::lookAt(eye, target_, up);

        // Save for worldToScreen/screenToWorld
        internal::currentProjectionMatrix = projection;
        internal::currentViewMatrix = view;
        internal::currentViewW = w;
        internal::currentViewH = h;

        // Apply to SGL (needs column-major, so transpose)
        Mat4 projT = projection.transposed();
        Mat4 viewT = view.transposed();

        sgl_matrix_mode_projection();
        sgl_load_identity();
        sgl_mult_matrix(projT.m);

        sgl_matrix_mode_modelview();
        sgl_load_identity();
        sgl_mult_matrix(viewT.m);
    }

    // End camera mode (return to 2D drawing mode)
    void end() {
        internal::restoreCurrentPipeline();
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
    // Mouse input (auto-subscribe to events)
    // ---------------------------------------------------------------------------

    void enableMouseInput() {
        if (mouseInputEnabled_) return;
        mouseInputEnabled_ = true;

        // Subscribe to mouse events
        listenerPressed_ = events().mousePressed.listen([this](MouseEventArgs& e) {
            onMousePressed(e.x, e.y, e.button);
        });
        listenerReleased_ = events().mouseReleased.listen([this](MouseEventArgs& e) {
            onMouseReleased(e.x, e.y, e.button);
        });
        listenerDragged_ = events().mouseDragged.listen([this](MouseDragEventArgs& e) {
            onMouseDragged(e.x, e.y, e.button);
        });
        listenerScrolled_ = events().mouseScrolled.listen([this](ScrollEventArgs& e) {
            onMouseScrolled(e.scrollX, e.scrollY);
        });
    }

    void disableMouseInput() {
        if (!mouseInputEnabled_) return;
        mouseInputEnabled_ = false;

        // Disconnect listeners
        listenerPressed_.disconnect();
        listenerReleased_.disconnect();
        listenerDragged_.disconnect();
        listenerScrolled_.disconnect();

        isDragging_ = false;
        isPanning_ = false;
    }

    bool isMouseInputEnabled() const {
        return mouseInputEnabled_;
    }

    // Manual mouse handlers (for custom routing or override scenarios)
    void mousePressed(int x, int y, int button) { onMousePressed((float)x, (float)y, button); }
    void mouseReleased(int x, int y, int button) { onMouseReleased((float)x, (float)y, button); }
    void mouseDragged(int x, int y, int button) { onMouseDragged((float)x, (float)y, button); }
    void mouseScrolled(float dx, float dy) { onMouseScrolled(dx, dy); }

private:
    // Internal mouse handlers
    void onMousePressed(float x, float y, int button) {
        lastMouseX_ = x;
        lastMouseY_ = y;

        if (button == MOUSE_BUTTON_LEFT) {
            isDragging_ = true;
        } else if (button == MOUSE_BUTTON_MIDDLE) {
            isPanning_ = true;
        }
    }

    void onMouseReleased(float x, float y, int button) {
        (void)x; (void)y;
        if (button == MOUSE_BUTTON_LEFT) {
            isDragging_ = false;
        } else if (button == MOUSE_BUTTON_MIDDLE) {
            isPanning_ = false;
        }
    }

    void onMouseDragged(float x, float y, int button) {
        float dx = x - lastMouseX_;
        float dy = y - lastMouseY_;

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

        lastMouseX_ = x;
        lastMouseY_ = y;
    }

    void onMouseScrolled(float dx, float dy) {
        (void)dx;
        // Zoom (change distance)
        distance_ -= dy * zoomSensitivity_;
        if (distance_ < 0.1f) distance_ = 0.1f;
    }

public:

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

    // Event listeners (RAII - auto disconnect on destruction)
    EventListener listenerPressed_;
    EventListener listenerReleased_;
    EventListener listenerDragged_;
    EventListener listenerScrolled_;
};

} // namespace trussc
