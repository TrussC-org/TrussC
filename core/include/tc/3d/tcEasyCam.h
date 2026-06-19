#pragma once

// tc::EasyCam - oF-compatible 3D camera
// Interactive camera with mouse-controlled rotation, zoom, and pan
// Supports configurable up axis (Y-up default, Z-up for scientific/VQF coords)

#include <cmath>
#include "tc/events/tcCoreEvents.h"

namespace trussc {

class EasyCam {
public:
    // Modifier key that must be held for camera mouse input (orbit / pan /
    // zoom). Lets the camera share the mouse with scene interaction — e.g.
    // require Shift for the camera so plain drags go to gizmos / nodes.
    enum class Modifier { None, Shift, Ctrl, Alt, Super };

    EasyCam()
        : target_{0.0f, 0.0f, 0.0f}
        , upAxis_{0.0f, 1.0f, 0.0f}
        , distance_(400.0f)
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
        // Enable 3D pipeline. Inside an FBO pass, use the FBO-context 3D pipeline
        // (the swapchain `pipeline3d` mismatches the FBO format/sample count and
        // corrupts color/alpha — same FBO-awareness as setupScreenPerspective).
        if (internal::inFboPass && internal::currentFboPipeline3d.id != 0) {
            sgl_load_pipeline(internal::currentFboPipeline3d);
        } else if (internal::pipeline3dInitialized) {
            sgl_load_pipeline(internal::pipeline3d);
        }

        float dpiScale = sapp_dpi_scale();
        float w = (float)sapp_width() / dpiScale;
        float h = (float)sapp_height() / dpiScale;
        float aspect = w / h;

        // Camera basis from orientation quaternion (no gimbal lock / no pole
        // singularity, so straight-down top-down views are valid).
        Vec3 eye   = eyePosition();
        Vec3 camUp = orientation_.rotate(Vec3(0.0f, 1.0f, 0.0f));

        // Create matrices using Mat4 (row-major)
        Mat4 projection;
        if (orthoEnabled_) {
            // Zoom is driven by distance_ in both modes, so toggling ortho /
            // perspective keeps the same apparent size at the target plane.
            // Perspective shows a world height of 2*distance*tan(fov/2) there;
            // ortho uses the same extent (no orthoZoom_ — distance_ is the one
            // source of zoom).
            float halfH = distance_ * std::tan(fov_ * 0.5f);
            float halfW = halfH * aspect;
            projection = Mat4::ortho(-halfW, halfW, -halfH, halfH, nearClip_, farClip_);
        } else {
            projection = Mat4::perspective(fov_, aspect, nearClip_, farClip_);
        }

        // EasyCam builds its own projection Mat4 (used for both rendering and
        // worldToScreen), bypassing sgl_ortho/sgl_perspective which normally
        // adapt to the backend's clip-space convention. Mat4::ortho/perspective
        // emit the GL convention (NDC z in [-1, 1]); D3D11/Metal/WebGPU expect
        // [0, 1]. Without remapping, ortho geometry near the target plane lands
        // in the clipped near half on D3D11 and disappears (perspective only
        // survives because its non-linear depth keeps far geometry inside [0,1]).
        switch (sg_query_backend()) {
            case SG_BACKEND_D3D11:
            case SG_BACKEND_METAL_IOS:
            case SG_BACKEND_METAL_MACOS:
            case SG_BACKEND_METAL_SIMULATOR:
            case SG_BACKEND_WGPU: {
                // Remap clip-space z from [-1, 1] to [0, 1]: z' = 0.5*z + 0.5*w.
                const Mat4 zeroToOne(
                    1, 0, 0,    0,
                    0, 1, 0,    0,
                    0, 0, 0.5f, 0.5f,
                    0, 0, 0,    1
                );
                projection = zeroToOne * projection;
                break;
            }
            default: break;  // GLCORE / GLES3 already use [-1, 1]
        }

        // camUp is orthogonal to the view direction by construction, so lookAt
        // never degenerates (even looking straight down the world up axis).
        Mat4 view = Mat4::lookAt(eye, target_, camUp);

        // Save for worldToScreen/screenToWorld
        internal::currentProjectionMatrix = projection;
        internal::currentViewMatrix = view;
        internal::currentViewW = w;
        internal::currentViewH = h;

        // Register this camera scope: nodes drawn between begin()/end() stamp
        // it, so mouse picking unprojects through THIS camera for them.
        internal::registerCameraContext(view, projection, w, h, true);

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
        orientation_ = baseOrientation();   // azimuth 0, elevation 0
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
    // Rebuilds orientation to the azimuth-0 / elevation-0 pose for the new up
    // axis. Call this at setup time, before any setAzimuth/setElevation.
    void setUpAxis(const Vec3& up) {
        upAxis_ = up;
        orientation_ = baseOrientation();
    }

    void setUpAxis(float x, float y, float z) {
        setUpAxis(Vec3{x, y, z});
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

    // ---------------------------------------------------------------------------
    // Orbit angles
    // ---------------------------------------------------------------------------
    // The camera orbits the target on a sphere, placed by two angles:
    //   azimuth   - horizontal angle (yaw): which side you view from, spinning
    //               around the up axis. 0 looks along the forward axis.
    //   elevation - vertical angle (pitch): how high above the target you look
    //               down from. 0 = level (side-on); positive looks downward.
    // Both in radians. Set them to frame an oblique 3/4 view at startup instead
    // of the default head-on side view. With the quaternion orientation there is
    // no pole singularity, so elevation is NOT clamped: deg2rad(90) gives a true
    // straight-down top-down view.
    void setAzimuth(float radians) {
        setOrbit(radians, getElevation());
    }

    void setElevation(float radians) {
        setOrbit(getAzimuth(), radians);
    }

    // Derived live from the orientation (consistent with mouse-drag too).
    float getElevation() const {
        Vec3 back = orientation_.rotate(Vec3(0.0f, 0.0f, 1.0f));
        float d = back.dot(upAxis_);
        if (d >  1.0f) d =  1.0f;
        if (d < -1.0f) d = -1.0f;
        return std::asin(d);
    }

    float getAzimuth() const {
        Vec3 right, forward;
        getOrbitAxes(right, forward);
        Vec3 back = orientation_.rotate(Vec3(0.0f, 0.0f, 1.0f));
        return std::atan2(back.dot(right), back.dot(forward));
    }

    // ---------------------------------------------------------------------------
    // Orthographic projection (oF-compatible: ofCamera::enableOrtho)
    // ---------------------------------------------------------------------------
    // When enabled, begin() uses an orthographic projection instead of
    // perspective. distance_ (mouse wheel / setDistance) still acts as zoom.
    void enableOrtho()  { orthoEnabled_ = true; }
    void disableOrtho() { orthoEnabled_ = false; }
    void setOrtho(bool ortho) { orthoEnabled_ = ortho; }
    bool getOrtho() const { return orthoEnabled_; }

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
    // Mouse bindings — which inputs drive the camera
    // ---------------------------------------------------------------------------

    // Mouse button that orbits (default: left)
    EasyCam& setOrbitButton(int button) { orbitButton_ = button; return *this; }
    int getOrbitButton() const { return orbitButton_; }

    // Mouse button that pans (default: middle)
    EasyCam& setPanButton(int button) { panButton_ = button; return *this; }
    int getPanButton() const { return panButton_; }

    // Modifier key required for camera input (orbit / pan / zoom). Checked at
    // press / scroll time; a drag that started with the modifier held keeps
    // the camera until release even if the modifier is let go mid-gesture.
    // Default: Modifier::None (no modifier needed).
    EasyCam& setDragModifier(Modifier m) { dragModifier_ = m; return *this; }
    Modifier getDragModifier() const { return dragModifier_; }

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
            lastMouseX_ = e.pos.x;
            lastMouseY_ = e.pos.y;
        });
        // The camera CONSUMES the gestures it claims (same consume + capture
        // contract as other overlay consumers): a press that starts an orbit /
        // pan owns the whole gesture, so the node tree never sees a click,
        // drag or release that was camera input.
        listenerPressed_ = events().mousePressed.listen([this](MouseEventArgs& e) {
            // Gestures are claimed at press time: no modifier, no camera.
            if (!modifierHeld(e.shift, e.ctrl, e.alt, e.super)) return;
            if (onMousePressed(e.pos.x, e.pos.y, e.button)) e.consumed = true;
        });
        listenerReleased_ = events().mouseReleased.listen([this](MouseEventArgs& e) {
            // Never gated on the modifier — a release must always be able to
            // end an in-flight gesture (even if the modifier was let go).
            if (onMouseReleased(e.pos.x, e.pos.y, e.button)) e.consumed = true;
        });
        listenerDragged_ = events().mouseDragged.listen([this](MouseDragEventArgs& e) {
            if (onMouseDragged(e.pos.x, e.pos.y, e.button)) e.consumed = true;
        });
        listenerScrolled_ = events().mouseScrolled.listen([this](ScrollEventArgs& e) {
            if (!modifierHeld(e.shift, e.ctrl, e.alt, e.super)) return;
            if (onMouseScrolled(e.scroll.x, e.scroll.y)) e.consumed = true;
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

    // Shortest-arc rotation that takes unit vector a onto unit vector b.
    static Quaternion rotationFromTo(Vec3 a, Vec3 b) {
        a = a.normalized();
        b = b.normalized();
        float d = a.dot(b);
        if (d >  0.999999f) return Quaternion();                 // already aligned
        if (d < -0.999999f) {                                    // opposite: 180°
            Vec3 axis = Vec3(1.0f, 0.0f, 0.0f).cross(a);
            if (axis.dot(axis) < 1e-6f) axis = Vec3(0.0f, 0.0f, 1.0f).cross(a);
            return Quaternion::fromAxisAngle(axis.normalized(), HALF_TAU);
        }
        Vec3 axis = a.cross(b).normalized();
        return Quaternion::fromAxisAngle(axis, std::acos(d));
    }

    // Orientation for azimuth 0 / elevation 0 with the current up axis.
    // Maps local +Y -> upAxis (and consistently +Z -> forward, +X -> right).
    Quaternion baseOrientation() const {
        return rotationFromTo(Vec3(0.0f, 1.0f, 0.0f), upAxis_);
    }

    // Build orientation_ from spherical orbit angles via composed axis-angle
    // rotations (valid at every angle, including the poles).
    void setOrbit(float azimuth, float elevation) {
        Quaternion base    = baseOrientation();
        Quaternion qAz     = Quaternion::fromAxisAngle(upAxis_, azimuth);
        Quaternion afterAz = (qAz * base);
        Vec3 right         = afterAz.rotate(Vec3(1.0f, 0.0f, 0.0f));
        Quaternion qEl     = Quaternion::fromAxisAngle(right, -elevation);
        orientation_       = (qEl * afterAz).normalized();
    }

    // Eye position: target plus the camera's "back" direction times distance.
    Vec3 eyePosition() const {
        Vec3 back = orientation_.rotate(Vec3(0.0f, 0.0f, 1.0f));
        return target_ + back * distance_;
    }

    bool isInsideControlArea(float x, float y) const {
        if (!hasControlArea_) return true;
        return x >= controlArea_.x && x <= controlArea_.x + controlArea_.width
            && y >= controlArea_.y && y <= controlArea_.y + controlArea_.height;
    }

    // Required drag modifier held? (None -> always true)
    bool modifierHeld(bool shift, bool ctrl, bool alt, bool super) const {
        switch (dragModifier_) {
            case Modifier::Shift: return shift;
            case Modifier::Ctrl:  return ctrl;
            case Modifier::Alt:   return alt;
            case Modifier::Super: return super;
            case Modifier::None:  return true;
        }
        return true;
    }

    // Internal mouse handlers. Each returns true when the camera claimed the
    // input (drives event consumption in the listeners above).
    bool onMousePressed(float x, float y, int button) {
        if (!isInsideControlArea(x, y)) return false;

        lastMouseX_ = x;
        lastMouseY_ = y;

        if (button == orbitButton_) {
            isDragging_ = true;
            return true;
        } else if (button == panButton_) {
            isPanning_ = true;
            return true;
        }
        return false;
    }

    bool onMouseReleased(float x, float y, int button) {
        (void)x; (void)y;
        if (button == orbitButton_ && isDragging_) {
            isDragging_ = false;
            return true;
        } else if (button == panButton_ && isPanning_) {
            isPanning_ = false;
            return true;
        }
        return false;
    }

    bool onMouseDragged(float x, float y, int button) {
        float dx = x - lastMouseX_;
        float dy = y - lastMouseY_;
        bool claimed = false;

        if (isDragging_ && button == orbitButton_) {
            claimed = true;
            // Yaw about the world up axis, pitch about the camera's right axis.
            // Accumulated on the quaternion, so it can orbit fully overhead with
            // no gimbal lock and no elevation clamp.
            Quaternion qYaw   = Quaternion::fromAxisAngle(upAxis_, -dx * 0.01f * sensitivity_);
            Vec3 camRight     = orientation_.rotate(Vec3(1.0f, 0.0f, 0.0f));
            Quaternion qPitch = Quaternion::fromAxisAngle(camRight, -dy * 0.01f * sensitivity_);
            orientation_ = (qYaw * qPitch * orientation_).normalized();
        } else if (isPanning_ && button == panButton_) {
            claimed = true;
            // Pan in the camera's own right/up plane.
            Vec3 camRight = orientation_.rotate(Vec3(1.0f, 0.0f, 0.0f));
            Vec3 camUp    = orientation_.rotate(Vec3(0.0f, 1.0f, 0.0f));

            float panX = dx * 0.5f * panSensitivity_;
            float panY = dy * 0.5f * panSensitivity_;

            target_ = target_ - camRight * panX + camUp * panY;
        }

        lastMouseX_ = x;
        lastMouseY_ = y;
        return claimed;
    }

    bool onMouseScrolled(float dx, float dy) {
        (void)dx;
        // Check control area using current mouse position
        float mx = lastMouseX_;
        float my = lastMouseY_;
        if (!isInsideControlArea(mx, my)) return false;

        // Wheel changes distance to the target in both modes; ortho derives its
        // extent from distance_, so the zoom feel stays consistent across toggle.
        distance_ -= dy * zoomSensitivity_;
        if (distance_ < 0.1f) distance_ = 0.1f;
        return true;
    }

public:

    // ---------------------------------------------------------------------------
    // Camera info
    // ---------------------------------------------------------------------------

    // Get camera position
    Vec3 getPosition() const {
        return eyePosition();
    }

    // Direct access to the camera orientation quaternion.
    Quaternion getOrientation() const { return orientation_; }
    void setOrientation(const Quaternion& q) { orientation_ = q.normalized(); }

private:
    Vec3 target_;         // Look-at target
    Vec3 upAxis_;         // Up axis (default Y-up, set to Z-up for scientific coords)
    float distance_;      // Distance from target
    Quaternion orientation_;  // Camera orientation (gimbal-lock free)

    float fov_;           // Field of view (radians)
    float nearClip_;      // Near clipping plane
    float farClip_;       // Far clipping plane

    bool orthoEnabled_ = false;  // Orthographic projection (vs perspective)
    bool mouseInputEnabled_;
    bool isDragging_;     // Orbit-button dragging
    bool isPanning_;      // Pan-button dragging
    float lastMouseX_;
    float lastMouseY_;

    // Mouse bindings (see setOrbitButton / setPanButton / setDragModifier)
    int orbitButton_ = (int)MOUSE_BUTTON_LEFT;
    int panButton_ = (int)MOUSE_BUTTON_MIDDLE;
    Modifier dragModifier_ = Modifier::None;

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
