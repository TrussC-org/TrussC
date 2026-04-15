#pragma once

// tc::EasyCam - oF-compatible 3D camera
// Interactive camera with mouse-controlled rotation, zoom, and pan
// Supports configurable up axis (Y-up default, Z-up for scientific/VQF coords)

#include <cmath>
#include "tc/events/tcCoreEvents.h"

namespace trussc {

class EasyCam {
public:
    EasyCam()
        : target_{0.0f, 0.0f, 0.0f}
        , upAxis_{0.0f, 1.0f, 0.0f}
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
        Vec3 eye = calcEyePosition();

        // Create matrices using Mat4 (row-major)
        Mat4 projection = Mat4::perspective(fov_, aspect, nearClip_, farClip_);
        Mat4 view = Mat4::lookAt(eye, target_, upAxis_);

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

    // Set up axis for the camera coordinate system.
    // Default is Y-up (0,1,0). Use (0,0,1) for Z-up (scientific/VQF convention).
    void setUpAxis(const Vec3& up) {
        upAxis_ = up;
    }

    void setUpAxis(float x, float y, float z) {
        upAxis_ = {x, y, z};
    }

    Vec3 getUpAxis() const {
        return upAxis_;
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

    // Constrain mouse input to a specific screen area.
    // Only mouse events inside this rect will be processed.
    // Pass an empty rect (width or height <= 0) to clear the constraint.
    void setControlArea(const Rect& area) {
        controlArea_ = area;
        hasControlArea_ = (area.width > 0 && area.height > 0);
    }

    void clearControlArea() {
        hasControlArea_ = false;
    }

    // ---------------------------------------------------------------------------
    // Mouse input (auto-subscribe to events)
    // ---------------------------------------------------------------------------

    void enableMouseInput() {
        if (mouseInputEnabled_) return;
        mouseInputEnabled_ = true;

        // Subscribe to mouse events
        listenerMoved_ = events().mouseMoved.listen([this](MouseMoveEventArgs& e) {
            lastMouseX_ = e.x;
            lastMouseY_ = e.y;
        });
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
        listenerMoved_.disconnect();
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
    // Compute right and forward axes from upAxis
    void getOrbitAxes(Vec3& right, Vec3& forward) const {
        // Choose a reference axis that isn't parallel to upAxis
        if (std::fabs(upAxis_.z) > 0.9f) {
            // Z-up: right=X, forward=-Y
            right   = {1.0f, 0.0f, 0.0f};
            forward = {0.0f, -1.0f, 0.0f};
        } else {
            // Y-up (default): right=X, forward=Z
            right   = {1.0f, 0.0f, 0.0f};
            forward = {0.0f, 0.0f, 1.0f};
        }
    }

    // Calculate eye position from spherical coordinates + upAxis
    Vec3 calcEyePosition() const {
        Vec3 right, forward;
        getOrbitAxes(right, forward);

        float cosEl = cos(rotationX_);
        float sinEl = sin(rotationX_);
        float cosAz = cos(rotationY_);
        float sinAz = sin(rotationY_);

        // Orbit: horizontal circle in right/forward plane, vertical along upAxis
        return {
            target_.x + distance_ * (right.x * sinAz * cosEl + forward.x * cosAz * cosEl + upAxis_.x * sinEl),
            target_.y + distance_ * (right.y * sinAz * cosEl + forward.y * cosAz * cosEl + upAxis_.y * sinEl),
            target_.z + distance_ * (right.z * sinAz * cosEl + forward.z * cosAz * cosEl + upAxis_.z * sinEl)
        };
    }

    bool isInsideControlArea(float x, float y) const {
        if (!hasControlArea_) return true;
        return x >= controlArea_.x && x <= controlArea_.x + controlArea_.width
            && y >= controlArea_.y && y <= controlArea_.y + controlArea_.height;
    }

    // Internal mouse handlers
    void onMousePressed(float x, float y, int button) {
        if (!isInsideControlArea(x, y)) return;

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

            // Limit elevation to ~80 degrees to prevent flipping near poles
            float maxAngle = 1.4f;
            if (rotationX_ > maxAngle) rotationX_ = maxAngle;
            if (rotationX_ < -maxAngle) rotationX_ = -maxAngle;
        } else if (isPanning_ && button == MOUSE_BUTTON_MIDDLE) {
            Vec3 right, forward;
            getOrbitAxes(right, forward);

            // Camera's horizontal right direction (rotated by azimuth)
            float cosAz = cos(rotationY_);
            float sinAz = sin(rotationY_);
            Vec3 camRight = {
                right.x * cosAz + forward.x * sinAz,
                right.y * cosAz + forward.y * sinAz,
                right.z * cosAz + forward.z * sinAz
            };

            float panX = dx * 0.5f * panSensitivity_;
            float panY = -dy * 0.5f * panSensitivity_;

            // Pan: horizontal along camRight, vertical along upAxis
            target_.x -= camRight.x * panX - upAxis_.x * panY;
            target_.y -= camRight.y * panX - upAxis_.y * panY;
            target_.z -= camRight.z * panX - upAxis_.z * panY;
        }

        lastMouseX_ = x;
        lastMouseY_ = y;
    }

    void onMouseScrolled(float dx, float dy) {
        (void)dx;
        // Check control area using current mouse position
        float mx = lastMouseX_;
        float my = lastMouseY_;
        if (!isInsideControlArea(mx, my)) return;

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
        return calcEyePosition();
    }

private:
    Vec3 target_;         // Look-at target
    Vec3 upAxis_;         // Up axis (default Y-up, set to Z-up for scientific coords)
    float distance_;      // Distance from target
    float rotationX_;     // Elevation angle (radians)
    float rotationY_;     // Azimuth angle (radians)

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

    Rect controlArea_;        // Constraint area for mouse input
    bool hasControlArea_ = false;

    // Event listeners (RAII - auto disconnect on destruction)
    EventListener listenerMoved_;
    EventListener listenerPressed_;
    EventListener listenerReleased_;
    EventListener listenerDragged_;
    EventListener listenerScrolled_;
};

} // namespace trussc
