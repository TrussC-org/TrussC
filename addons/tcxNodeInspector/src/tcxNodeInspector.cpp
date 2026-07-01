#include "tcxNodeInspector.h"

using namespace std;
using namespace tc;

namespace tcx::nodeinspector {

namespace {

// Push the inspector styling for one window, pop on scope exit. Pushed BEFORE
// Begin() so window bg / padding / title-bar colors apply to the window itself.
struct StyleScope {
    int colors = 0, vars = 0;
    explicit StyleScope(const NodeInspector::Style& s) {
        auto col  = [&](ImGuiCol id, ImVec4 c)      { ImGui::PushStyleColor(id, c); ++colors; };
        auto var2 = [&](ImGuiStyleVar id, ImVec2 v) { ImGui::PushStyleVar(id, v);   ++vars; };
        auto var1 = [&](ImGuiStyleVar id, float v)  { ImGui::PushStyleVar(id, v);   ++vars; };

        const Color& a = s.accent;
        ImVec4 accent     (a.r,         a.g,         a.b,         1.0f);
        ImVec4 accentDim  (a.r * 0.45f, a.g * 0.45f, a.b * 0.45f, 1.0f);
        ImVec4 accentHover(a.r * 0.75f, a.g * 0.75f, a.b * 0.75f, 1.0f);

        col(ImGuiCol_WindowBg,         ImVec4(0.06f, 0.07f, 0.08f, s.windowAlpha));
        col(ImGuiCol_TitleBg,          accentDim);
        col(ImGuiCol_TitleBgActive,    accent);
        col(ImGuiCol_TitleBgCollapsed, accentDim);
        col(ImGuiCol_Header,           accentDim);    // tree selection + SeparatorText
        col(ImGuiCol_HeaderHovered,    accentHover);
        col(ImGuiCol_HeaderActive,     accent);

        if (s.compact) {
            var2(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
            var2(ImGuiStyleVar_FramePadding,  ImVec2(4, 2));
            var2(ImGuiStyleVar_ItemSpacing,   ImVec2(5, 3));
            var1(ImGuiStyleVar_IndentSpacing, 14.0f);
        }
    }
    ~StyleScope() {
        ImGui::PopStyleVar(vars);
        ImGui::PopStyleColor(colors);
    }
};

} // namespace

void NodeInspector::syncNameBuf(Node* node) {
    snprintf(nameBuf_, sizeof(nameBuf_), "%s", node->getName().c_str());
    nameBufFor_ = node->getInstanceId();
}

// =============================================================================
// Multi-selection — inspector-local list on top of core's single selection.
// Core's selectedNode is the PRIMARY (what the Inspector panel shows); this
// list extends it. There are no core hooks: any change of the primary that we
// didn't make ourselves (plain canvas click, MCP select_node, ...) is observed
// in reconcileSelection() and collapses the list to that node — so the two
// notions can never fight over ownership.
// =============================================================================

void NodeInspector::reconcileSelection() {
    selection_.erase(remove_if(selection_.begin(), selection_.end(),
                               [](const weak_ptr<Node>& w) { return w.expired(); }),
                     selection_.end());
    Node* primary = getSelectedNode();
    if (primary != lastPrimary_) {
        selection_.clear();
        if (primary) selection_.push_back(primary->weak_from_this());
        lastPrimary_ = primary;
    }
}

std::vector<Node*> NodeInspector::getSelection() {
    reconcileSelection();
    std::vector<Node*> out;
    out.reserve(selection_.size());
    for (auto& w : selection_) {
        if (auto sp = w.lock()) out.push_back(sp.get());
    }
    return out;
}

bool NodeInspector::isSelected(const Node* n) {
    for (auto& w : selection_) {
        if (auto sp = w.lock(); sp.get() == n) return true;
    }
    return false;
}

void NodeInspector::select(Node* n) {
    selection_.clear();
    if (n) selection_.push_back(n->weak_from_this());
    setSelectedNode(n);
    lastPrimary_ = n;
}

void NodeInspector::toggleInSelection(Node* n) {
    if (!n) return;
    reconcileSelection();
    auto it = find_if(selection_.begin(), selection_.end(),
                      [n](const weak_ptr<Node>& w) { return w.lock().get() == n; });
    if (it != selection_.end()) {
        selection_.erase(it);
        // If the primary left the set, promote the most recent remaining node.
        if (getSelectedNode() == n) {
            Node* next = selection_.empty() ? nullptr : selection_.back().lock().get();
            setSelectedNode(next);
            lastPrimary_ = next;
        }
    } else {
        selection_.push_back(n->weak_from_this());
        setSelectedNode(n);
        lastPrimary_ = n;
    }
}

void NodeInspector::clearSelection() {
    selection_.clear();
    setSelectedNode(nullptr);
    lastPrimary_ = nullptr;
}

void NodeInspector::draw(Node& root) {
    reconcileSelection();
    drawHierarchy(root);
    drawInspector();
    if (style_.showGizmo) drawGizmo();
}

void NodeInspector::drawHierarchy(Node& root) {
    if (!enabled_) return;
    StyleScope ss(style_);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(240, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("Hierarchy");
    ImGui::TextDisabled("%zu node(s)", root.getChildCount());
    ImGui::Separator();
    for (auto& child : root.getChildren()) drawTreeNode(child);
    ImGui::End();
}

void NodeInspector::drawTreeNode(const Node::Ptr& node) {
    auto children = node->getChildren();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (isSelected(node.get())) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(static_cast<int>(node->getInstanceId()));
    bool open = ImGui::TreeNodeEx(node->getDisplayName().c_str(), flags);
    if (ImGui::IsItemClicked()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeySuper || io.KeyCtrl) toggleInSelection(node.get());   // multi
        else                           select(node.get());
        if (Node* p = getSelectedNode()) syncNameBuf(p);
    }
    if (open) {
        for (auto& c : children) drawTreeNode(c);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void NodeInspector::drawInspector() {
    if (!enabled_) return;
    StyleScope ss(style_);
    ImGui::SetNextWindowPos(ImVec2(260, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");

    Node* node = getSelectedNode();
    if (!node) {
        ImGui::TextDisabled("(no selection)");
        ImGui::TextDisabled("click a node in the tree");
        ImGui::TextDisabled("or on the canvas");
        ImGui::End();
        return;
    }

    auto sel = getSelection();
    if (sel.size() > 1) {
        ImGui::TextDisabled("%zu nodes selected / showing primary", sel.size());
    }
    ImGui::TextDisabled("%s  #%llu", node->getTypeName().c_str(),
                        static_cast<unsigned long long>(node->getInstanceId()));

    if (nameBufFor_ != node->getInstanceId()) syncNameBuf(node);
    if (ImGui::InputText("name", nameBuf_, sizeof(nameBuf_))) node->setName(nameBuf_);

    ImGui::SeparatorText("Members");
    ImGuiReflector r;
    node->reflectMembers(r);

    // One section per attached mod, edited through the same reflector. The
    // header carries the mod's type; widget IDs are scoped per mod so two mods
    // with same-named members don't collide.
    for (Mod* mod : node->getMods()) {
        Mod& m = *mod;
        const std::string& modType = shortTypeName(typeid(m));
        ImGui::SeparatorText(modType.c_str());
        ImGui::PushID(mod);
        mod->reflectMembers(r);
        ImGui::PopID();
    }

    ImGui::SeparatorText("Operations");
    if (ImGui::SmallButton("Destroy")) {
        node->destroy();
        setSelectedNode(nullptr);
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("To front")) node->moveToFront();
    ImGui::SameLine();
    if (ImGui::SmallButton("To back"))  node->moveToBack();

    ImGui::End();
}

// =============================================================================
// Move gizmo — Unity-style XYZ handles at the selected node's origin.
//
// Geometry: the node's LOCAL axes are taken to world space through its global
// matrix (so the handles rotate with the node), then projected through the
// node's own camera context — the same camera it was rendered with, so the
// gizmo is correct under the default screen, off the z=0 plane, or inside an
// EasyCam scope. Handle length is constant in SCREEN pixels (the world length
// is solved per frame from pixels-per-world-unit at the node's depth). An axis
// pointing almost straight at the camera projects to nothing and is skipped —
// it couldn't be dragged meaningfully anyway.
//
// Dragging: solved in world space, not screen space. The cursor ray
// (CameraContext::screenPointToRay) and the axis line have a unique pair of
// closest points; the axis-line parameter of that point is tracked from press
// to drag, and the node moves by the delta along the axis. Exact under
// perspective, no cursor drift.
// =============================================================================

namespace {

// World units spanned by ONE screen pixel at `worldPos`, on the
// constant-depth plane through the point. This is the gizmo's single size
// basis: it does not depend on any axis direction, so handle sizes are stable
// while the node rotates, and there is no per-direction solve to misbehave on
// view-aligned axes — those simply foreshorten naturally on screen and hide
// below the length threshold (Unity behaves the same way).
//
// Measured by FORWARD projection only (worldToScreen finite difference along
// the camera-right axis). Never through screenPointToRay: the inverse-ray
// construction unprojects two clip-space points only `near` apart, and with
// an extreme near/far (EasyCam defaults to 0.1/10000) float cancellation puts
// ~1% noise on the ray direction — extrapolated to the node's depth that made
// the whole gizmo flicker ±15% frame to frame. The forward path has no
// cancellation. Sampling along camera-right also keeps the measurement on the
// constant-depth plane (a uniform scale under perspective), so the result is
// independent of where the node sits on screen — no pulse while it orbits.
float worldPerPixelAt(const CameraContext& ctx, const Vec3& worldPos, const Vec2& /*screenPos*/) {
    Vec4 r4 = ctx.view.inverted() * Vec4(1.0f, 0.0f, 0.0f, 0.0f);   // camera right in world
    Vec3 right = Vec3(r4.x, r4.y, r4.z).normalized();
    Vec3 s0 = ctx.worldToScreen(worldPos);
    // Two passes: measure with a 1-unit step, then re-aim the step to ~4 px
    // so the finite difference is local but well above float screen noise.
    float step = 1.0f;
    for (int pass = 0; pass < 2; ++pass) {
        Vec3 s1 = ctx.worldToScreen(worldPos + right * step);
        if (s1.z < 0.0f) return 0.0f;
        float px = Vec2(s1.x - s0.x, s1.y - s0.y).length();
        if (px < 1e-6f) return 0.0f;
        if (pass == 1) return step / px;
        step *= 4.0f / px;
    }
    return 0.0f;
}

} // namespace

// Handle directions for one node, in world space.
// - Translate: the node's LOCAL frame (normalized global-matrix columns).
// - Rotate: the GIMBAL axes of the Y-X-Z euler decomposition — yaw = the
//   parent frame's Y, pitch = that Y-rotated X, roll = the node's local Z.
//   Dragging ring i then changes EXACTLY euler component i (same numeric
//   path as editing the inspector's rotation field), and each ring's axis
//   is invariant under its own channel, so the dragged ring never drifts.
//   (Blender's "gimbal" gizmo orientation works the same way.)
NodeInspector::GizmoGeom NodeInspector::computeGizmoGeom(Node* node, GizmoMode mode) const {
    Vec3 origin = node->getGlobalPos();
    Vec3 axes[3];
    if (mode == GizmoMode::Translate) {
        for (int i = 0; i < 3; ++i) {
            Vec3 axisLocal(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f);
            Vec3 dir = node->localToGlobal(axisLocal) - origin;
            float len = dir.length();
            axes[i] = (len < 1e-6f) ? Vec3(0, 0, 0) : dir * (1.0f / len);
        }
    } else {
        Vec3 e = node->getEuler();
        Quaternion qYaw = Quaternion::fromAxisAngle(Vec3(0, 1, 0), e.y);
        Vec3 inParent[3];
        inParent[0] = qYaw.rotate(Vec3(1, 0, 0));                 // pitch (X channel)
        inParent[1] = Vec3(0, 1, 0);                              // yaw (Y channel)
        inParent[2] = node->getQuaternion().rotate(Vec3(0, 0, 1)); // roll (Z channel)
        auto parent = node->getParent();
        for (int i = 0; i < 3; ++i) {
            Vec3 dir = inParent[i];
            if (parent) {   // parent-space direction -> world
                dir = parent->localToGlobal(dir) - parent->localToGlobal(Vec3(0, 0, 0));
            }
            float len = dir.length();
            axes[i] = (len < 1e-6f) ? Vec3(0, 0, 0) : dir * (1.0f / len);
        }
    }
    return computeGizmoGeomFrame(origin, axes, node->getCameraContext(), mode);
}

// Gizmo for a multi-selection: origin = centroid of the selected nodes; axes =
// the nodes' shared local frame when every global orientation matches,
// otherwise the world axes ("give up", like Unity's Global mode); projection
// through the PRIMARY node's camera context. Translate only.
NodeInspector::GizmoGeom NodeInspector::computeGizmoGeomMulti(const std::vector<Node*>& sel,
                                                              GizmoMode mode) const {
    GizmoGeom g;
    if (sel.empty()) return g;
    (void)mode;   // rings are single-node; a multi gizmo always translates

    Vec3 centroid(0, 0, 0);
    for (Node* n : sel) centroid = centroid + n->getGlobalPos();
    centroid = centroid * (1.0f / (float)sel.size());

    // Per-node world frame = normalized global-matrix columns. Shared if every
    // node's every axis agrees with the first node's within a tight cone.
    auto frameOf = [](Node* n, Vec3 out[3]) {
        Vec3 o = n->getGlobalPos();
        for (int i = 0; i < 3; ++i) {
            Vec3 axisLocal(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f);
            Vec3 dir = n->localToGlobal(axisLocal) - o;
            float len = dir.length();
            out[i] = (len < 1e-6f) ? Vec3(0, 0, 0) : dir * (1.0f / len);
        }
    };
    Vec3 axes[3];
    frameOf(sel[0], axes);
    bool shared = true;
    for (size_t k = 1; k < sel.size() && shared; ++k) {
        Vec3 f[3];
        frameOf(sel[k], f);
        for (int i = 0; i < 3; ++i) {
            if (axes[i].dot(f[i]) < 0.9999f) { shared = false; break; }
        }
    }
    if (!shared) {
        axes[0] = Vec3(1, 0, 0);
        axes[1] = Vec3(0, 1, 0);
        axes[2] = Vec3(0, 0, 1);
    }

    Node* primary = getSelectedNode();
    Node* ctxNode = (primary && find(sel.begin(), sel.end(), primary) != sel.end())
                  ? primary : sel[0];
    return computeGizmoGeomFrame(centroid, axes, ctxNode->getCameraContext(),
                                 GizmoMode::Translate);
}

// Shared geometry builder: project `origin`, size the handles in screen px,
// and build axis segments / rings from the given world-space frame.
NodeInspector::GizmoGeom NodeInspector::computeGizmoGeomFrame(
        const Vec3& origin, const Vec3 axes[3],
        const std::shared_ptr<const CameraContext>& ctx, GizmoMode mode) const {
    GizmoGeom g;
    g.origin = origin;

    Vec2 so;
    if (ctx) {
        Vec3 s = ctx->worldToScreen(origin);
        if (s.z < 0.0f || s.z > 1.0f) return g;   // behind the camera
        so = Vec2(s.x, s.y);
    } else {
        so = Vec2(origin.x, origin.y);            // never drawn: raw screen fallback
    }
    g.screenOrigin = so;

    const float scale = style_.gizmoScale;
    const float handlePx = style_.gizmoLength * scale;

    // One size basis for the whole gizmo: the world length of `handlePx`
    // screen pixels at the node's depth. Direction-independent, so handles
    // don't breathe while the node rotates.
    float worldLen;
    if (ctx) {
        float wpp = worldPerPixelAt(*ctx, origin, so);
        if (wpp <= 0.0f) return g;
        worldLen = handlePx * wpp;
    } else {
        worldLen = handlePx;
    }

    for (int i = 0; i < 3; ++i) {
        const Vec3& dir = axes[i];
        if (dir.length() < 0.5f) continue;                    // degenerate scale
        g.axis[i].dir = dir;

        if (mode == GizmoMode::Translate) {
            Vec2 p1;
            if (ctx) {
                // Linearized projection: project a SMALL step along the axis
                // and scale up (the projection derivative at the origin).
                // Projecting the full-length tip would re-apply the
                // perspective divide at the tip's own depth, so an axis
                // leaning toward the camera grows past its nominal length and
                // a rotating node makes the handles pulse. The derivative
                // keeps only the natural cos-theta foreshortening.
                const float EPS = 0.05f;
                Vec3 s2 = ctx->worldToScreen(origin + dir * (worldLen * EPS));
                if (s2.z < 0.0f || s2.z > 1.0f) continue;     // behind the camera
                p1 = so + (Vec2(s2.x, s2.y) - so) * (1.0f / EPS);
            } else {
                p1 = so + Vec2(dir.x, dir.y) * worldLen;
            }
            // View-aligned axes foreshorten; below this they are neither
            // readable nor meaningfully draggable.
            if ((p1 - so).length() < handlePx * 0.2f) continue;

            g.axis[i].valid = true;
            g.axis[i].p0 = so;
            g.axis[i].p1 = p1;
        } else {
            // Rotation ring around gimbal axis i. A circle is invariant under
            // the choice of in-plane basis, so any orthonormal pair works.
            Vec3 ref = (std::abs(dir.z) < 0.9f) ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
            Vec3 u = dir.cross(ref).normalized();
            Vec3 v = dir.cross(u);

            const int SEGMENTS = 48;
            auto& ring = g.axis[i].ring;
            ring.reserve(SEGMENTS);
            bool ok = true;
            for (int s = 0; s < SEGMENTS; ++s) {
                float a = TAU * (float)s / SEGMENTS;
                Vec3 p = origin + (u * cosf(a) + v * sinf(a)) * worldLen;
                if (ctx) {
                    Vec3 sc = ctx->worldToScreen(p);
                    if (sc.z < 0.0f || sc.z > 1.0f) { ok = false; break; }
                    ring.emplace_back(sc.x, sc.y);
                } else {
                    ring.emplace_back(p.x, p.y);
                }
            }
            if (!ok) { ring.clear(); continue; }
            g.axis[i].valid = true;
        }
    }

    g.valid = true;
    return g;
}

namespace {

float pointSegmentDist(const Vec2& m, const Vec2& a, const Vec2& b) {
    Vec2 ab = b - a;
    float len2 = ab.dot(ab);
    if (len2 < 1e-9f) return (m - a).length();
    float t = ab.dot(m - a) / len2;
    t = std::min(1.0f, std::max(0.0f, t));
    return (m - (a + ab * t)).length();
}

} // namespace

// Nearest handle within grab distance of the cursor (screen px), -1 if none.
int NodeInspector::pickAxis(const GizmoGeom& g, const Vec2& mouse) const {
    const float grabDist = std::max(6.0f, style_.gizmoThickness * 2.0f) * style_.gizmoScale;
    int best = -1;
    float bestD = grabDist;
    for (int i = 0; i < 3; ++i) {
        if (!g.axis[i].valid) continue;
        float d = pointSegmentDist(mouse, g.axis[i].p0, g.axis[i].p1);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// Nearest rotation ring within grab distance of the cursor, -1 if none.
int NodeInspector::pickRing(const GizmoGeom& g, const Vec2& mouse) const {
    const float grabDist = std::max(6.0f, style_.gizmoThickness * 2.0f) * style_.gizmoScale;
    int best = -1;
    float bestD = grabDist;
    for (int i = 0; i < 3; ++i) {
        const auto& ring = g.axis[i].ring;
        if (!g.axis[i].valid || ring.empty()) continue;
        for (size_t s = 0; s < ring.size(); ++s) {
            float d = pointSegmentDist(mouse, ring[s], ring[(s + 1) % ring.size()]);
            if (d < bestD) { bestD = d; best = (int)i; }
        }
    }
    return best;
}

// Axis-line parameter of the point on the drag axis closest to the cursor ray.
// Lines: axis P(s) = worldStart + s*d, ray Q(t) = R + t*e (d, e unit).
// Returns false near the axis's vanishing point, where the cursor ray runs
// ~parallel to the axis and the solve becomes a noise amplifier (sign flips,
// huge jumps) — callers hold the current position instead of chasing it.
bool NodeInspector::axisParamForMouse(const std::shared_ptr<const CameraContext>& ctx,
                                      const Vec2& mouse, float& outS) const {
    Ray r = ctx ? ctx->screenPointToRay(mouse.x, mouse.y)
                : Ray::fromScreenPoint2D(mouse.x, mouse.y);
    const Vec3& d = dragAxisDir_;
    const Vec3& e = r.direction;
    Vec3 w = dragWorldStart_ - r.origin;
    float b = d.dot(e);
    float denom = 1.0f - b * b;
    if (std::abs(denom) < 1e-3f) return false;
    outS = (b * w.dot(e) - w.dot(d)) / denom;
    return true;
}

// Origin->cursor direction inside the ring plane (plane through the node
// origin with normal dragAxisDir_), via cursor-ray/plane intersection.
// Returns false when the ray is ~parallel to the plane (ring edge-on).
bool NodeInspector::ringAngleForMouse(const std::shared_ptr<const CameraContext>& ctx,
                                      const Vec2& mouse, Vec3& outDirFromOrigin) const {
    Ray r = ctx ? ctx->screenPointToRay(mouse.x, mouse.y)
                : Ray::fromScreenPoint2D(mouse.x, mouse.y);
    const Vec3& n = dragAxisDir_;
    float denom = r.direction.dot(n);
    if (std::abs(denom) < 1e-4f) return false;
    float t = (dragWorldStart_ - r.origin).dot(n) / denom;
    Vec3 hit = r.at(t);
    Vec3 d = hit - dragWorldStart_;
    float len = d.length();
    if (len < 1e-6f) return false;
    outDirFromOrigin = d * (1.0f / len);
    return true;
}

bool NodeInspector::gizmoPressHandler(const MouseEventArgs& e) {
    if (!enabled_) return false;

    // Cmd/Ctrl+click toggles selection membership (multi-select). It is an
    // editor gesture: consumed whether or not it lands on a node, so the scene
    // never sees it. Clicks over an ImGui panel stay with the panel.
    if (e.super || e.ctrl) {
        if (tc::isOverlayHovered()) return false;
        Node* root = tc::getRootNode();
        if (!root) return false;
        auto res = root->findHitNodeFromScreen(e.pos.x, e.pos.y);
        if (res.hit()) toggleInSelection(res.node.get());
        return true;
    }

    if (!style_.showGizmo || !style_.gizmoDraggable) return false;
    Node* node = getSelectedNode();
    if (!node) return false;
    auto sel = getSelection();
    const bool multi = sel.size() > 1;

    // SHIFT at press selects the rotation rings (single selection only — a
    // multi gizmo always translates); the mode is then locked for the whole
    // gesture (releasing shift mid-drag keeps rotating).
    GizmoMode mode = (e.shift && !multi) ? GizmoMode::Rotate : GizmoMode::Translate;
    GizmoGeom g = multi ? computeGizmoGeomMulti(sel, mode) : computeGizmoGeom(node, mode);
    if (!g.valid) return false;

    Vec2 m(e.pos.x, e.pos.y);
    int axis = (mode == GizmoMode::Rotate) ? pickRing(g, m) : pickAxis(g, m);
    if (axis < 0) return false;

    dragAxis_ = axis;
    dragMode_ = mode;
    dragNode_ = node;
    dragWorldStart_ = g.origin;
    dragAxisDir_ = g.axis[axis].dir;
    if (mode == GizmoMode::Translate) {
        if (!axisParamForMouse(node->getCameraContext(), m, dragS0_)) {
            dragAxis_ = -1;          // axis at its vanishing point: refuse the grab
            dragNode_ = nullptr;
            return false;
        }
        // World position of every selected node at press — the drag applies
        // one shared world delta to each of them.
        dragStarts_.clear();
        for (Node* n : sel) dragStarts_.emplace_back(n->weak_from_this(), n->getGlobalPos());
    } else {
        if (!ringAngleForMouse(node->getCameraContext(), m, dragV0_)) {
            dragAxis_ = -1;          // ring edge-on: cannot rotate meaningfully
            dragNode_ = nullptr;
            return false;
        }
        dragVPrev_ = dragV0_;
        dragAngleAccum_ = 0.0f;
        dragEulerStart_ = node->getEuler();
    }
    return true;   // claim the gesture
}

void NodeInspector::gizmoDragHandler(MouseDragEventArgs& e) {
    if (dragAxis_ < 0) return;
    Node* node = getSelectedNode();
    if (!node || node != dragNode_) {            // selection died mid-drag: cancel
        dragAxis_ = -1;
        dragNode_ = nullptr;
        return;
    }

    Vec2 m(e.pos.x, e.pos.y);
    auto ctx = node->getCameraContext();
    if (dragMode_ == GizmoMode::Translate) {
        float s;
        if (axisParamForMouse(ctx, m, s)) {
            Vec3 delta = dragAxisDir_ * (s - dragS0_);
            Vec3 newOrigin = dragWorldStart_ + delta;
            // Refuse a step that would put the gizmo behind / on the camera —
            // beyond the axis's vanishing point the solve folds back, which
            // reads as "dragging away suddenly moves it closer".
            bool inFront = true;
            if (ctx) {
                Vec3 sc = ctx->worldToScreen(newOrigin);
                inFront = (sc.z >= 0.0f && sc.z <= 1.0f);
            }
            if (inFront) {
                // One shared world delta for every selected node (a node
                // destroyed mid-drag just drops out via its weak_ptr).
                for (auto& [w, start] : dragStarts_) {
                    if (auto sp = w.lock()) sp->setGlobalPos(start + delta);
                }
            }
        }
    } else {
        Vec3 v;
        if (ringAngleForMouse(ctx, m, v)) {
            // ACCUMULATE the per-event signed angle (each step is well under a
            // half turn) instead of measuring from the grab direction — a
            // from-start measurement wraps at +-180deg and snaps the object a
            // full turn backwards mid-drag.
            dragAngleAccum_ += atan2f(dragVPrev_.cross(v).dot(dragAxisDir_), dragVPrev_.dot(v));
            dragVPrev_ = v;
            // The ring is a GIMBAL ring: the accumulated angle goes straight
            // into that euler channel — the exact numeric path the inspector's
            // rotation field uses, so dragging the Y ring moves ONLY yaw, etc.
            Vec3 euler = dragEulerStart_;
            switch (dragAxis_) {
                case 0: euler.x += dragAngleAccum_; break;
                case 1: euler.y += dragAngleAccum_; break;
                case 2: euler.z += dragAngleAccum_; break;
            }
            node->setEuler(euler);
        }
    }
    e.consumed = true;
}

void NodeInspector::gizmoReleaseHandler(MouseEventArgs& e) {
    if (dragAxis_ < 0) return;
    dragAxis_ = -1;
    dragNode_ = nullptr;
    dragStarts_.clear();
    e.consumed = true;   // the gesture was ours, release included
}

void NodeInspector::enableGizmoInput() {
    if (gizmoInputEnabled_) return;
    gizmoInputEnabled_ = true;

    // Slightly ahead of BeforeApp: the gizmo draws in ImGui's FOREGROUND layer
    // (over the panels), so it also wins input over them — what you see on top
    // is what you grab.
    const int prio = tc::EventPriority::BeforeApp - 10;
    gizmoPress_ = tc::events().mousePressed.listen([this](MouseEventArgs& e) {
        if (gizmoPressHandler(e)) e.consumed = true;
    }, prio);
    gizmoDrag_ = tc::events().mouseDragged.listen([this](MouseDragEventArgs& e) {
        gizmoDragHandler(e);
    }, prio);
    gizmoRelease_ = tc::events().mouseReleased.listen([this](MouseEventArgs& e) {
        gizmoReleaseHandler(e);
    }, prio);
}

void NodeInspector::drawGizmo() {
    if (!enabled_ || !style_.showGizmo) return;
    enableGizmoInput();
    Node* node = getSelectedNode();
    if (!node) return;
    auto sel = getSelection();
    const bool multi = sel.size() > 1;

    // Mode: locked while dragging; otherwise SHIFT previews the rotation rings
    // (single selection only — a multi gizmo always translates).
    GizmoMode mode = (dragAxis_ >= 0) ? dragMode_
                   : ((ImGui::GetIO().KeyShift && !multi) ? GizmoMode::Rotate
                                                          : GizmoMode::Translate);

    GizmoGeom g = multi ? computeGizmoGeomMulti(sel, mode) : computeGizmoGeom(node, mode);
    if (!g.valid) return;

    ImVec2 mp = ImGui::GetIO().MousePos;
    Vec2 m(mp.x, mp.y);
    hoverAxis_ = (dragAxis_ >= 0) ? dragAxis_
               : (mode == GizmoMode::Rotate ? pickRing(g, m) : pickAxis(g, m));

    auto* dl = ImGui::GetForegroundDrawList();
    // Soft RGB, one per local axis (X/Y/Z); the hot handle goes warm yellow.
    static const Color axisColor[3] = {
        {0.89f, 0.36f, 0.33f, 1.0f},   // X
        {0.55f, 0.81f, 0.33f, 1.0f},   // Y
        {0.35f, 0.61f, 0.93f, 1.0f},   // Z
    };
    static const Color hotColor{1.0f, 0.84f, 0.30f, 1.0f};

    const float scale = style_.gizmoScale;
    const float thick = style_.gizmoThickness * scale;

    for (int i = 0; i < 3; ++i) {
        if (!g.axis[i].valid) continue;
        bool hot = (i == hoverAxis_);
        const Color& c = hot ? hotColor : axisColor[i];
        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, 1.0f));
        float w = hot ? thick + 1.0f : thick;

        if (mode == GizmoMode::Translate) {
            ImVec2 a(g.axis[i].p0.x, g.axis[i].p0.y);
            ImVec2 b(g.axis[i].p1.x, g.axis[i].p1.y);
            dl->AddLine(a, b, col, w);

            // Arrow head at the tip
            Vec2 dpx = g.axis[i].p1 - g.axis[i].p0;
            dpx = dpx * (1.0f / dpx.length());
            Vec2 n(-dpx.y, dpx.x);
            const float ah = 8.0f * scale, aw = 3.5f * scale;
            ImVec2 tip(b.x + dpx.x * ah, b.y + dpx.y * ah);
            ImVec2 b1(b.x + n.x * aw, b.y + n.y * aw);
            ImVec2 b2(b.x - n.x * aw, b.y - n.y * aw);
            dl->AddTriangleFilled(tip, b1, b2, col);
        } else {
            const auto& ring = g.axis[i].ring;
            static std::vector<ImVec2> pts;
            pts.clear();
            pts.reserve(ring.size());
            for (auto& p : ring) pts.emplace_back(p.x, p.y);
            dl->AddPolyline(pts.data(), (int)pts.size(), col, ImDrawFlags_Closed, w);
        }
    }

    // Origin pad
    const Color& acc = style_.accent;
    ImU32 accCol = ImGui::ColorConvertFloat4ToU32(ImVec4(acc.r, acc.g, acc.b, 1.0f));
    dl->AddCircleFilled(ImVec2(g.screenOrigin.x, g.screenOrigin.y), 3.5f * scale, accCol, 16);

    // Multi-selection: a small ring on each member (projected through its own
    // camera context), so you can see what the centroid gizmo will move.
    if (multi) {
        ImU32 markCol = ImGui::ColorConvertFloat4ToU32(
            ImVec4(hotColor.r, hotColor.g, hotColor.b, 1.0f));
        for (Node* n : sel) {
            Vec3 p = n->getGlobalPos();
            Vec2 sp;
            if (auto nctx = n->getCameraContext()) {
                Vec3 s = nctx->worldToScreen(p);
                if (s.z < 0.0f || s.z > 1.0f) continue;
                sp = Vec2(s.x, s.y);
            } else {
                sp = Vec2(p.x, p.y);
            }
            dl->AddCircle(ImVec2(sp.x, sp.y), 5.0f * scale, markCol, 12, 1.5f);
        }
    }
}

// =============================================================================
// Autonomous mode — the inspector draws itself every frame with no per-frame
// calls. Static API drives the singleton; the work lives in private helpers.
// =============================================================================

NodeInspector& NodeInspector::attach() {
    NodeInspector& s = instance();
    s.attachRoot_ = nullptr;
    s.doAttach();
    return s;
}

NodeInspector& NodeInspector::attach(Node& root) {
    NodeInspector& s = instance();
    s.attachRoot_ = &root;
    s.doAttach();
    return s;
}

NodeInspector& NodeInspector::attach(int toggleKey) {
    setToggleKey(toggleKey);
    return attach();
}

NodeInspector& NodeInspector::attach(Node& root, int toggleKey) {
    setToggleKey(toggleKey);
    return attach(root);
}

void NodeInspector::detach() {
    instance().doDetach();
}

NodeInspector& NodeInspector::setToggleKey(int key) {
    NodeInspector& s = instance();
    s.toggleKeys_ = {key};
    s.ensureToggleKeyListener();
    return s;
}

NodeInspector& NodeInspector::addToggleKey(int key) {
    NodeInspector& s = instance();
    s.toggleKeys_.push_back(key);
    s.ensureToggleKeyListener();
    return s;
}

NodeInspector& NodeInspector::clearToggleKeys() {
    instance().toggleKeys_.clear();         // listener stays; nothing matches
    return instance();
}

// --- private helpers ---------------------------------------------------------

void NodeInspector::doAttach() {
    imguiSetup();                           // idempotent — safe if already set up
    ensureExitGuard();

    // onRender fires after the scene has drawn (so camera contexts are current
    // for the gizmo) and before tcxImGui's render listener (priority 1000).
    // Building the frame at the default priority lands it in the same pass, on
    // top of the scene. Re-attaching just replaces the previous listener.
    autoDraw_ = events().onRender.listen([this] {
        Node* r = attachRoot_ ? attachRoot_ : getRootNode();
        if (!r) return;
        imguiBegin();
        draw(*r);
        imguiEnd();
    });
}

void NodeInspector::doDetach() {
    autoDraw_ = {};
}

// Installed once and kept for the app's lifetime; set/add/clear only edit
// toggleKeys_, so a cleared set simply never matches.
void NodeInspector::ensureToggleKeyListener() {
    if (toggleKeyListener_) return;
    ensureExitGuard();
    toggleKeyListener_ = events().keyPressed.listen([this](KeyEventArgs& e) {
        if (e.consumed) return;             // e.g. typing in the inspector's name field
        for (int k : toggleKeys_) {
            if (e.key == k) { toggle(); break; }
        }
    });
}

// The singleton outlives the framework, so its listeners would otherwise be
// torn down at static-destruction time — after events() may already be gone.
// Drop them at the exit event instead, while everything is still alive (the
// same pattern tcxImGui's ImGuiManager uses). Removing exitListener_ from
// inside its own callback is fine: the dispatch list is snapshotted.
void NodeInspector::ensureExitGuard() {
    if (exitListener_) return;
    exitListener_ = events().exit.listen([this] {
        autoDraw_ = {};
        toggleKeyListener_ = {};
        exitListener_ = {};
    });
}

} // namespace tcx::nodeinspector
