#pragma once

// =============================================================================
// tcxNodeInspector - Unity-like runtime hierarchy + reflected property inspector
// for TrussC's Node tree.
//
// Drop it into any app that already uses tcxImGui:
//
//     NodeInspector inspector;          // a member of your App
//
//     void draw() override {
//         imguiBegin();
//         inspector.draw(*sceneRoot);   // tree + inspector + gizmo
//         imguiEnd();
//     }
//
// It renders three things: a Hierarchy tree, an Inspector panel that edits the
// selected node's reflected members (TC_REFLECT), and an on-canvas gizmo at the
// selection. Selection IS the Node system's own selection
// (getSelectedNode/setSelectedNode), so clicking a node on the canvas and
// clicking it in the tree drive the same thing — and clicking the inspector
// itself never clears the selection (overlay input arbitration in core).
// =============================================================================

#include <TrussC.h>
#include <tcxImGui.h>
#include <cstdio>
#include <memory>
#include <vector>

namespace tcx {

// Renders each reflected member as the matching ImGui widget. Public so an app
// can subclass and override a visit() to customize how a given type is edited.
// Read-only values (getter-only TC_VALUE) render greyed out; enums
// render as a combo of their labels.
struct ImGuiReflector : ::trussc::Reflector {
    bool visit(const char* n, float& v) override       { return edit([&] { return ImGui::DragFloat(n, &v, 0.5f); }); }
    bool visit(const char* n, int& v) override         { return edit([&] { return ImGui::DragInt(n, &v); }); }
    bool visit(const char* n, bool& v) override        { return edit([&] { return ImGui::Checkbox(n, &v); }); }
    bool visit(const char* n, ::tc::Vec2& v) override  { return edit([&] { return ImGui::DragFloat2(n, &v.x, 0.5f); }); }
    bool visit(const char* n, ::tc::Vec3& v) override  { return edit([&] { return ImGui::DragFloat3(n, &v.x, 0.5f); }); }
    bool visit(const char* n, ::tc::Color& v) override { return edit([&] { return ImGui::ColorEdit4(n, &v.r); }); }
    bool visit(const char* n, std::string& v) override {
        return edit([&] {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", v.c_str());
            if (ImGui::InputText(n, buf, sizeof(buf))) { v = buf; return true; }
            return false;
        });
    }
    bool visit(const char* n, int& v, const ::trussc::EnumLabelSpan& labels) override {
        return edit([&] {
            const char* preview = (v >= 0 && v < labels.count) ? labels.labels[v] : "(?)";
            bool changed = false;
            if (ImGui::BeginCombo(n, preview)) {
                for (int i = 0; i < labels.count; i++) {
                    if (ImGui::Selectable(labels.labels[i], i == v)) { v = i; changed = true; }
                }
                ImGui::EndCombo();
            }
            return changed;
        });
    }

protected:
    // Run a widget; in a read-only scope, grey it out and discard edits.
    template <class F>
    bool edit(F&& widget) {
        if (!isReadOnly()) return widget();
        ImGui::BeginDisabled(true);
        widget();
        ImGui::EndDisabled();
        return false;
    }
};

// A single debug overlay per app — NodeInspector is a singleton, not something
// you instantiate. The init / autonomous API is static (attach / toggle keys);
// everything else (style, selection, manual draw) hangs off instance(). Drop it
// in with one line in setup() and no member:
//
//     NodeInspector::attach(KEY_F1);                 // autonomous, F1 toggles
//     NodeInspector::attach(KEY_F1).setAccent(...);  // static call chains into instance
//
class NodeInspector {
public:
    // The one inspector. Constructed on first use (Meyers singleton). Use this
    // to reach the instance-level API (style, selection, manual draw).
    static NodeInspector& instance() {
        static NodeInspector inst;
        return inst;
    }

    // Look & feel. Inspectors get cramped fast, so the defaults are compact and
    // the panel background is semi-transparent. The accent recolors the title
    // bars / selection so an inspector reads as visually distinct from an app's
    // own ImGui windows.
    struct Style {
        bool        compact     = true;                    // tight padding / spacing
        float       windowAlpha = 0.65f;                   // panel bg transparency (0..1)
        ::tc::Color accent{0.16f, 0.55f, 0.62f, 1.0f};     // teal — title bars / selection

        // Gizmo (Unity-style handles at the selected node; hold SHIFT for the
        // rotation rings, plain for the move axes — the mode is locked while a
        // drag is in flight)
        bool  showGizmo      = true;    // draw the gizmo at the selected node
        bool  gizmoDraggable = true;    // grab a handle to move / rotate the node
        float gizmoLength    = 35.0f;   // handle length / ring radius on screen (px)
        float gizmoThickness = 2.0f;    // handle line width (px)
        float gizmoScale     = 1.0f;    // multiplier on all gizmo dimensions
    };

    Style& style() { return style_; }
    NodeInspector& setAccent(const ::tc::Color& c) { style_.accent = c; return *this; }
    NodeInspector& setWindowAlpha(float a)         { style_.windowAlpha = a; return *this; }
    NodeInspector& setCompact(bool b)              { style_.compact = b; return *this; }
    NodeInspector& setGizmoScale(float s)          { style_.gizmoScale = s; return *this; }

    // Master on/off (default on). While disabled, draw() and the individual
    // panel methods render nothing — no Hierarchy / Inspector windows and no
    // selection gizmo — so the whole tool disappears with one call. Selection
    // itself is untouched (it's the Node system's), so re-enabling picks up
    // where you left off. Typical use: toggle from a key handler.
    NodeInspector& setEnabled(bool e) { enabled_ = e; return *this; }
    bool isEnabled() const            { return enabled_; }
    NodeInspector& toggle()           { enabled_ = !enabled_; return *this; }

    // Everything: hierarchy tree + inspector + gizmo. Call between
    // imguiBegin() / imguiEnd().
    void draw(::tc::Node& root);

    // The individual panels, if you'd rather place them yourself.
    void drawHierarchy(::tc::Node& root);
    void drawInspector();
    void drawGizmo();

    // Register the gizmo's mouse listeners (consume + capture: a press that
    // grabs a handle owns the whole gesture, the node tree never sees it).
    // Called automatically on the first draw(); idempotent. Public so headless
    // tests / manual setups can wire input without an ImGui frame.
    void enableGizmoInput();

    bool isGizmoDragging() const { return dragAxis_ >= 0; }

    // -------------------------------------------------------------------------
    // Autonomous mode (static — drives the singleton)
    // -------------------------------------------------------------------------
    // attach() makes the inspector draw itself every frame — call it once in
    // setup() and write nothing in your app's draw(). It ensures imgui is
    // initialized (imguiSetup) and then drives imguiBegin()/draw(root)/imguiEnd()
    // from an onRender listener — after the scene is drawn, so the gizmo's camera
    // projection is current-frame, and before tcxImGui's render. Root defaults to
    // getRootNode() (the running App); pass one to inspect a subtree instead.
    // Each returns the singleton, so a call chains into the instance API
    // (e.g. NodeInspector::attach(KEY_F1).setAccent(...)).
    //
    // attach() OWNS the imgui frame. If your app also drives imgui itself, don't
    // attach() — call instance().draw(root) inside your own imguiBegin()/imguiEnd().
    static NodeInspector& attach();
    static NodeInspector& attach(::tc::Node& root);
    static NodeInspector& attach(int toggleKey);                  // attach() + setToggleKey
    static NodeInspector& attach(::tc::Node& root, int toggleKey);
    static void detach();                                         // stop drawing itself
    static bool isAttached() { return static_cast<bool>(instance().autoDraw_); }

    // Toggle key(s): pressing any registered key flips the inspector on/off
    // (toggle()). The key listener is installed on the first set/add and stays
    // for the app's lifetime; these just edit the key set, so a cleared set
    // simply never matches. Works with or without attach().
    static NodeInspector& setToggleKey(int key);     // replace the set with one key
    static NodeInspector& addToggleKey(int key);     // add one key
    static NodeInspector& clearToggleKeys();         // forget all keys (listener stays)

    // -------------------------------------------------------------------------
    // Multi-selection
    // -------------------------------------------------------------------------
    // Lives in the inspector, NOT in core: core's selection stays the single
    // "last picked node" (the PRIMARY — what the Inspector panel shows), and
    // this list extends it. Cmd/Ctrl+click toggles membership (canvas and
    // tree); a plain click anywhere collapses back to a single node (observed
    // each frame — no core hooks). Entries are weak, so destroyed nodes drop
    // out by themselves.
    //
    // With 2+ nodes selected the gizmo moves to the selection centroid; its
    // axes use the nodes' shared frame when all global orientations match,
    // otherwise the world axes. Dragging translates every selected node.
    // Rotation rings stay single-node.
    std::vector<::tc::Node*> getSelection();          // pruned, insertion order
    bool isSelected(const ::tc::Node* n);
    void select(::tc::Node* n);                       // replace ( = plain click)
    void toggleInSelection(::tc::Node* n);            // = Cmd/Ctrl+click
    void clearSelection();

private:
    Style    style_;
    bool     enabled_      = true;
    char     nameBuf_[128] = "";
    uint64_t nameBufFor_   = 0;

    // Singleton: no user-constructed instances (use instance() / the static API).
    NodeInspector() = default;
    NodeInspector(const NodeInspector&) = delete;
    NodeInspector& operator=(const NodeInspector&) = delete;

    void drawTreeNode(const ::tc::Node::Ptr& node);
    void syncNameBuf(::tc::Node* node);

    // --- autonomous mode -----------------------------------------------------
    void doAttach();                          // imguiSetup + onRender listener
    void doDetach();                          // drop the frame driver
    void ensureToggleKeyListener();
    void ensureExitGuard();                   // drop listeners at exit (before teardown)
    ::tc::EventListener autoDraw_;             // onRender frame driver (attach)
    ::tc::Node*         attachRoot_ = nullptr; // null => getRootNode() each frame
    std::vector<int>    toggleKeys_;
    ::tc::EventListener toggleKeyListener_;    // installed once, then lives on
    ::tc::EventListener exitListener_;         // clears the above while events() is alive

    // --- gizmo ---------------------------------------------------------------
    enum class GizmoMode { Translate, Rotate };

    // Screen-space geometry of the gizmo for one node, projected through the
    // node's own camera context. Translate mode fills the axis handles; rotate
    // mode fills the rings (sampled world-space circles around each local axis).
    struct AxisGeom {
        bool      valid = false;
        ::tc::Vec3 dir;       // world-space unit direction of the local axis
        ::tc::Vec2 p0, p1;    // screen-space handle segment (translate mode)
        std::vector<::tc::Vec2> ring;   // closed polyline (rotate mode)
    };
    struct GizmoGeom {
        bool       valid = false;
        ::tc::Vec3 origin;        // node origin, world space
        ::tc::Vec2 screenOrigin;
        AxisGeom   axis[3];       // local X / Y / Z
    };

    GizmoGeom computeGizmoGeom(::tc::Node* node, GizmoMode mode) const;
    GizmoGeom computeGizmoGeomFrame(const ::tc::Vec3& origin, const ::tc::Vec3 axes[3],
                                    const std::shared_ptr<const ::tc::CameraContext>& ctx,
                                    GizmoMode mode) const;
    GizmoGeom computeGizmoGeomMulti(const std::vector<::tc::Node*>& sel, GizmoMode mode) const;
    int  pickAxis(const GizmoGeom& g, const ::tc::Vec2& mouse) const;
    int  pickRing(const GizmoGeom& g, const ::tc::Vec2& mouse) const;
    bool axisParamForMouse(const std::shared_ptr<const ::tc::CameraContext>& ctx,
                           const ::tc::Vec2& mouse, float& outS) const;
    bool ringAngleForMouse(const std::shared_ptr<const ::tc::CameraContext>& ctx,
                           const ::tc::Vec2& mouse, ::tc::Vec3& outDirFromOrigin) const;
    bool gizmoPressHandler(const ::tc::MouseEventArgs& e);
    void gizmoDragHandler(::tc::MouseDragEventArgs& e);
    void gizmoReleaseHandler(::tc::MouseEventArgs& e);

    bool gizmoInputEnabled_ = false;
    ::tc::EventListener gizmoPress_, gizmoDrag_, gizmoRelease_;
    int        hoverAxis_ = -1;       // visual feedback (computed in drawGizmo)
    int        dragAxis_  = -1;       // claimed at press; -1 = not dragging
    GizmoMode  dragMode_  = GizmoMode::Translate;   // locked for the whole gesture
    ::tc::Node* dragNode_ = nullptr;  // primary at press (cancel if selection dies)
    ::tc::Vec3 dragWorldStart_;       // gizmo origin at press (node / centroid)
    ::tc::Vec3 dragAxisDir_;          // world unit axis (move dir / rotation normal)
    float      dragS0_ = 0.0f;        // translate: axis parameter of the grab point
    ::tc::Vec3 dragV0_;               // rotate: unit origin->grab dir in the ring plane
    ::tc::Vec3 dragVPrev_;            // rotate: previous event's dir (for accumulation)
    float      dragAngleAccum_ = 0.0f;   // rotate: unwrapped angle since press
    ::tc::Vec3 dragEulerStart_;       // rotate: node euler at press (the channel base)

    // translate: every selected node's world position at press (weak: a node
    // destroyed mid-drag silently drops out)
    std::vector<std::pair<std::weak_ptr<::tc::Node>, ::tc::Vec3>> dragStarts_;

    // --- multi-selection -------------------------------------------------------
    std::vector<std::weak_ptr<::tc::Node>> selection_;
    ::tc::Node* lastPrimary_ = nullptr;   // last core selectedNode we synced with
    void reconcileSelection();            // prune dead + collapse on external change
};

} // namespace tcx
