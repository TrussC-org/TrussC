#pragma once

// =============================================================================
// tcImGuiHooks.h - ImGui Test Engine Hook implementations + Widget Registry
//
// Provides the extern functions required by IMGUI_ENABLE_TEST_ENGINE, plus the
// [TrussC] value hook (see imgui/imconfig.h) that reports the value of each
// value widget. Separated from tcImGuiTools.h so that imgui_impl.mm/.cpp can
// include this without pulling in MCP / nlohmann dependencies.
//
// Two registries:
// - Per ImGui context, the widgets of the last completed frame (labels, rects,
//   status flags, values). Rebuilt every frame; what tcx_imgui_get_widgets
//   lists. One per context, so every window running imgui is listed.
// - "Touched": every value widget whose value was changed through the widget
//   (drag, typing, a click) since startup or the last resetTouched(). Kept
//   across frames with the last known value, so it survives the widget not
//   being drawn (collapsed tree, closed window). ImGuiItemStatusFlags_Edited is
//   only set by widget interaction, never by code assigning the variable.
//   Only the value hook creates entries, plus the combo / list box routing in
//   the ItemInfo hook: a pick inside a combo popup or a list box is an edit of
//   that widget. So an entry carries the value of a caller's variable, except
//   one routed to a custom BeginCombo / BeginListBox, which has no variable
//   (only its label, and for a combo the item shown). Items that change no
//   variable (menu headers, action menu items, plain Selectables, buttons)
//   are not recorded.
//
// Values queued by the MCP tools (tcx_imgui_input on a value widget) are
// written into the widget's variable by the value hook at the widget's entry,
// before the widget reads it; the widget returns true in that frame
// (IMGUI_TC_RETURN). The variable is read back at its return, and checked again
// at the widget's entry in the next frame. Such a write sets no Edited flag, so
// it is not recorded as touched.
// =============================================================================

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace tcx::imgui {

// ---------------------------------------------------------------------------
// Widget value reported by the [TrussC] value hook
// ---------------------------------------------------------------------------
struct WidgetValue {
    int kind = 0;                      // ImGuiTcValueKind_* (0 = no value)
    ImGuiDataType dataType = 0;        // scalar kinds: type of each component
    int components = 0;
    std::vector<unsigned char> bytes;  // components * sizeof(dataType), copied at report time
    bool hasText = false;
    std::string text;                  // Text: the string; Combo(Preview): the item shown
    bool hsv = false;                  // Color: the variable holds HSV (ImGuiColorEditFlags_InputHSV)
    bool password = false;             // Text: password field, the string is withheld
    bool truncated = false;            // Text: longer than kMaxTextBytes

    static constexpr size_t kMaxTextBytes = 64 * 1024;
};

// ---------------------------------------------------------------------------
// Widget info collected from Test Engine hooks
// ---------------------------------------------------------------------------
struct WidgetInfo {
    ImGuiID id = 0;
    std::string label;
    std::string windowName;            // the ImGui window (panel) it was submitted to
    ImRect rect;
    ImGuiItemStatusFlags statusFlags = 0;
    WidgetValue value;
};

// A widget changed by interaction since startup / the last resetTouched()
struct TouchedWidget {
    ImGuiContext* ctx = nullptr;       // null once that context is destroyed (window closed)
    ImGuiID id = 0;
    std::string label;
    std::string windowName;
    ImGuiItemStatusFlags statusFlags = 0;   // last seen
    WidgetValue value;                 // last known value
};

namespace detail {

// A value queued for one widget by the MCP tools (see queueValue()). The value
// hook writes it through the widget's Data pointer when the widget is next
// entered (the widget then returns true in that frame: IMGUI_TC_RETURN), reads
// the variable back when it returns, and checks at the widget's entry in the
// following frame that the variable still holds it.
struct PendingValue {
    enum class State {
        Queued,         // waiting for the widget to run
        Written,        // written at entry; the widget is running
        Verify,         // read back at return: waiting for the next frame's entry
        Applied,        // the variable still held the value at the next entry (or the widget
                        //   was not drawn again, and the read-back at return is all there is)
        Changed,        // read back at return: the widget changed it in the same frame
        Reverted,       // at the next entry the variable held another value again: the app
                        //   ignores the return value and copies its own value in every frame
        Disabled,       // inside BeginDisabled(): not written
        ReadOnly,       // a read-only widget: not written
        NotDrawn,       // the widget did not run in the frame after the call: not written
        ShapeChanged,   // the widget now reports another kind / type / component count
        Superseded,     // a later value was queued for the same widget before this one was written
    };
    ImGuiID id = 0;
    int kind = 0;                        // ImGuiTcValueKind_ the widget reported
    ImGuiDataType dataType = 0;
    int components = 0;
    std::vector<unsigned char> bytes;    // components * sizeof(dataType)
    std::chrono::steady_clock::time_point deadline;   // not written after this
    int queuedFrame = 0;                 // ctx->FrameCount when queued
    int writtenFrame = 0;                // ctx->FrameCount of the write
    State state = State::Queued;
    const ImGuiTcItemValue* writtenBy = nullptr;       // the hook scope that wrote it, until it returns
    WidgetValue readBack;                // the variable at the widget's return / next entry
    std::function<void()> onDone;        // called once, at the end of the frame the outcome is known
    bool done() const {
        return state != State::Queued && state != State::Written && state != State::Verify;
    }
};

// Per ImGui context (each window running imgui has its own)
struct ContextState {
    // Current frame (written during ImGui rendering)
    std::vector<WidgetInfo> currentFrame;
    std::unordered_map<ImGuiID, size_t> currentIdMap;

    // Last completed frame (read by MCP tools)
    std::vector<WidgetInfo> lastFrame;
    std::unordered_map<ImGuiID, size_t> lastIdMap;

    // Edits the hooks have seen so far (see ImGuiTcItemValue::EditCountAtEntry)
    unsigned int editCount = 0;

    // The combo whose popup is open at each BeginComboDepth (index depth - 1):
    // a pick in the popup (a Selectable) is an edit of that combo.
    struct OpenCombo { ImGuiID id = 0; std::string label, windowName; };
    std::vector<OpenCombo> openCombos;

    // The list boxes drawn this frame, by their ID, which is also the ChildId
    // of the child window BeginListBox opens: a pick inside that child window
    // (a Selectable) is an edit of the list box. Upstream has no depth counter
    // for list boxes, so the child window stands in for BeginComboDepth.
    struct OpenListBox { std::string label, windowName; };
    std::unordered_map<ImGuiID, OpenListBox> listBoxes;

    // Values queued by the MCP tools, waiting for their widget (or running in it)
    std::vector<std::shared_ptr<PendingValue>> pendingValues;

    // Opaque owner tag (tcxImGui: the tc::internal::WindowContext*), so tools
    // can say which OS window a widget is in
    const void* owner = nullptr;
};

inline std::unordered_map<ImGuiContext*, ContextState>& contexts() {
    static std::unordered_map<ImGuiContext*, ContextState> m;
    return m;
}

inline std::vector<TouchedWidget>& touched() {
    static std::vector<TouchedWidget> v;
    return v;
}

// Whether collection is active
inline bool collecting = false;

// > 0 while a TouchedExclusionScope is open: edits are not recorded as touched
inline int touchedExcludeDepth = 0;

inline TouchedWidget* findTouched(ImGuiContext* ctx, ImGuiID id) {
    for (auto& t : touched()) {
        if (t.ctx == ctx && t.id == id) return &t;
    }
    return nullptr;
}

inline TouchedWidget& markTouched(ImGuiContext* ctx, ImGuiID id) {
    touched().push_back(TouchedWidget{});
    TouchedWidget& t = touched().back();
    t.ctx = ctx;
    t.id = id;
    return t;
}

// Inside a ColorEdit/ColorPicker, its parts (##X, ##Text, ##picker, ...) run as
// widgets of their own on temporaries. The whole widget reports instead.
inline bool insideColorWidget(ImGuiContext* ctx) {
    return ctx->ColorEditCurrentID != 0;
}

inline void captureValue(WidgetValue& out, const ImGuiTcItemValue& item, ImGuiContext* ctx) {
    out = WidgetValue{};
    out.kind = item.Kind;
    switch (item.Kind) {
    case ImGuiTcValueKind_Text: {
        out.password = (item.Flags & ImGuiInputTextFlags_Password) != 0;
        const char* s = *static_cast<char* const*>(item.Data);
        if (!out.password && s) {
            size_t n = strnlen(s, WidgetValue::kMaxTextBytes + 1);
            out.truncated = n > WidgetValue::kMaxTextBytes;
            out.text.assign(s, out.truncated ? WidgetValue::kMaxTextBytes : n);
        }
        out.hasText = !out.password;
        break;
    }
    case ImGuiTcValueKind_ComboPreview: {
        const char* s = static_cast<const char*>(item.Data);
        out.hasText = s != nullptr;
        if (s) out.text = s;
        break;
    }
    case ImGuiTcValueKind_ListBoxBegin:
        break;   // no value: only which list box it is
    default: {
        out.dataType = item.DataType;
        out.components = item.Components;
        size_t size = ImGui::DataTypeGetInfo(item.DataType)->Size * (size_t)item.Components;
        out.bytes.resize(size);
        std::memcpy(out.bytes.data(), item.Data, size);
        if (item.Kind == ImGuiTcValueKind_Color) {
            // Like ColorEdit4 itself: the IO default applies when the widget
            // flags don't pick an input format.
            ImGuiColorEditFlags f = item.Flags;
            if (!(f & ImGuiColorEditFlags_InputMask_)) f |= ctx->IO.ConfigColorEditFlags & ImGuiColorEditFlags_InputMask_;
            out.hsv = (f & ImGuiColorEditFlags_InputHSV) != 0;
        }
        break;
    }
    }
}

// Combo: BeginCombo (inside Combo) already reported the item shown; keep it.
inline void mergeValue(WidgetValue& dst, WidgetValue&& src) {
    if (src.kind == ImGuiTcValueKind_Combo &&
        (dst.kind == ImGuiTcValueKind_ComboPreview || dst.kind == ImGuiTcValueKind_Combo)) {
        src.hasText = dst.hasText;
        src.text = std::move(dst.text);
    }
    dst = std::move(src);
}

// ---------------------------------------------------------------------------
// Values queued by the MCP tools
// ---------------------------------------------------------------------------

// Whether the value hook can write a value of this kind: the kinds whose Data
// is the caller's variable of a fixed size. Not Text (the buffer size is not
// known), nor the openers (no variable).
inline bool isWritableKind(int kind) {
    switch (kind) {
    case ImGuiTcValueKind_Drag:
    case ImGuiTcValueKind_Slider:
    case ImGuiTcValueKind_SliderAngle:
    case ImGuiTcValueKind_Input:
    case ImGuiTcValueKind_Color:
    case ImGuiTcValueKind_Combo:
    case ImGuiTcValueKind_Bool:
    case ImGuiTcValueKind_Radio:
    case ImGuiTcValueKind_ListBox:
        return true;
    default:
        return false;
    }
}

// Queue `bytes` for the widget `id` of `ctx`, which reported `kind`,
// `dataType` and `components`. The value hook writes it the next time the
// widget runs, within `lifetime`. A value still queued for the same widget is
// superseded. `onDone` runs once the outcome is known (see finishPendingValues()).
inline std::shared_ptr<PendingValue> queueValue(ImGuiContext* ctx, ImGuiID id, int kind,
                                                ImGuiDataType dataType, int components,
                                                std::vector<unsigned char> bytes,
                                                std::chrono::steady_clock::duration lifetime,
                                                std::function<void()> onDone) {
    auto& pending = contexts()[ctx].pendingValues;
    for (auto& q : pending) {
        if (q->id == id && q->state == PendingValue::State::Queued) {
            q->state = PendingValue::State::Superseded;   // answered at the end of the next frame
        }
    }
    auto p = std::make_shared<PendingValue>();
    p->id = id;
    p->kind = kind;
    p->dataType = dataType;
    p->components = components;
    p->bytes = std::move(bytes);
    p->deadline = std::chrono::steady_clock::now() + lifetime;
    p->queuedFrame = ctx->FrameCount;
    p->onDone = std::move(onDone);
    pending.push_back(p);
    return p;
}

// Whether a value widget refuses a written value: inside BeginDisabled(), or
// read-only (the ReadOnly item flag, ImGuiSliderFlags_ReadOnly,
// ImGuiInputTextFlags_ReadOnly on an InputScalar).
inline PendingValue::State refusalFor(const ImGuiTcItemValue& item) {
    ImGuiContext* g = item.Ctx;
    ImGuiItemFlags f = g->CurrentItemFlags;
    if (item.Id && g->LastItemData.ID == item.Id) f |= g->LastItemData.ItemFlags;   // after its ItemAdd()
    if (f & ImGuiItemFlags_Disabled) return PendingValue::State::Disabled;
    bool readOnly = (f & ImGuiItemFlags_ReadOnly) != 0;
    switch (item.Kind) {
    case ImGuiTcValueKind_Drag:
    case ImGuiTcValueKind_Slider:
    case ImGuiTcValueKind_SliderAngle:
        readOnly |= (item.Flags & ImGuiSliderFlags_ReadOnly) != 0;
        break;
    case ImGuiTcValueKind_Input:
        readOnly |= (item.Flags & ImGuiInputTextFlags_ReadOnly) != 0;
        break;
    default:
        break;
    }
    return readOnly ? PendingValue::State::ReadOnly : PendingValue::State::Queued;
}

// At a value widget's entry: first check a value written in an earlier frame
// (does the variable still hold it?), then write the value queued for it, if
// any, through Data, before the widget reads its variable.
inline void writePendingValue(ContextState& cs, ImGuiTcItemValue& item) {
    ImGuiWindow* window = static_cast<ImGuiWindow*>(item.Window);
    if (!window || window->SkipItems || !item.Data || !item.Label || !item.Label[0]) return;
    if (!isWritableKind(item.Kind) || insideColorWidget(item.Ctx)) return;
    const auto now = std::chrono::steady_clock::now();
    const int frame = item.Ctx->FrameCount;
    const ImGuiID id = item.Id ? item.Id : window->GetID(item.Label);
    // Same ID and kind: SliderAngle's inner SliderFloat and Combo's BeginCombo
    // share the ID but not the kind, and never see a value.
    auto matches = [&](const PendingValue& p) { return p.id == id && p.kind == item.Kind; };
    const size_t size = ImGui::DataTypeGetInfo(item.DataType)->Size * (size_t)item.Components;
    for (auto& pp : cs.pendingValues) {
        PendingValue& p = *pp;
        if (p.state != PendingValue::State::Verify || !matches(p) || frame <= p.writtenFrame) continue;
        captureValue(p.readBack, item, item.Ctx);
        p.state = (p.bytes.size() == size && std::memcmp(item.Data, p.bytes.data(), size) == 0)
                ? PendingValue::State::Applied : PendingValue::State::Reverted;
    }
    for (auto& pp : cs.pendingValues) {
        PendingValue& p = *pp;
        if (p.state != PendingValue::State::Queued || !matches(p)) continue;
        if (now >= p.deadline) {
            p.state = PendingValue::State::NotDrawn;   // the tool has answered (or given up) by now
            continue;
        }
        if (p.dataType != item.DataType || p.components != item.Components || p.bytes.size() != size) {
            p.state = PendingValue::State::ShapeChanged;
            return;
        }
        if (PendingValue::State refused = refusalFor(item); refused != PendingValue::State::Queued) {
            p.state = refused;
            return;
        }
        std::memcpy(const_cast<void*>(item.Data), p.bytes.data(), size);
        p.state = PendingValue::State::Written;
        p.writtenBy = &item;
        p.writtenFrame = frame;
        item.Injected = true;   // IMGUI_TC_RETURN: the widget returns true this frame
        return;
    }
}

// At a value widget's return: read back the variable a value was written into
// at its entry.
inline void readBackPendingValue(ContextState& cs, const ImGuiTcItemValue& item) {
    for (auto& pp : cs.pendingValues) {
        PendingValue& p = *pp;
        if (p.writtenBy != &item) continue;
        p.writtenBy = nullptr;
        captureValue(p.readBack, item, item.Ctx);
        p.state = p.readBack.bytes == p.bytes ? PendingValue::State::Verify
                                              : PendingValue::State::Changed;
        return;
    }
}

// At the end of a context's frame: settle what this frame decided and hand
// each finished value to its onDone. A value still queued after a whole frame
// was not drawn; a value still waiting for its check a frame after the write
// was not drawn again, and its read-back at return stands.
inline void finishPendingValues(ContextState& cs, int frame, bool contextGone = false) {
    std::vector<std::shared_ptr<PendingValue>> finished;
    for (auto it = cs.pendingValues.begin(); it != cs.pendingValues.end();) {
        PendingValue& p = **it;
        if (p.state == PendingValue::State::Queued && (contextGone || frame > p.queuedFrame)) {
            p.state = PendingValue::State::NotDrawn;
        } else if (p.state == PendingValue::State::Verify && (contextGone || frame > p.writtenFrame)) {
            p.state = PendingValue::State::Applied;   // not entered in the frame after the write
        }
        if (p.done()) {
            finished.push_back(*it);
            it = cs.pendingValues.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& p : finished) {
        if (p->onDone) p->onDone();
    }
}

} // namespace detail

// Begin frame: clear the current context's buffer. Also turns the hooks on for
// this context — every window's imgui runs its own context, and hook
// collection is a per-context switch.
inline void beginFrame() {
    if (!detail::collecting) return;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return;
    ctx->TestEngineHookItems = true;
    auto& cs = detail::contexts()[ctx];
    cs.currentFrame.clear();
    cs.currentIdMap.clear();
    cs.listBoxes.clear();
}

// Swap frames: move current to last
inline void swapFrames() {
    if (!detail::collecting) return;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return;
    auto& cs = detail::contexts()[ctx];
    cs.lastFrame.swap(cs.currentFrame);
    cs.lastIdMap.swap(cs.currentIdMap);
    if (!cs.pendingValues.empty()) detail::finishPendingValues(cs, ctx->FrameCount);
}

// Enable/disable collection
inline void enableCollection() {
    if (auto* ctx = ImGui::GetCurrentContext()) ctx->TestEngineHookItems = true;
    detail::collecting = true;
}

inline void disableCollection() {
    for (auto& [ctx, cs] : detail::contexts()) ctx->TestEngineHookItems = false;
    if (auto* ctx = ImGui::GetCurrentContext()) ctx->TestEngineHookItems = false;
    detail::collecting = false;
}

// Tag a context with its owner (tcxImGui: the WindowContext it renders into).
inline void setContextOwner(ImGuiContext* ctx, const void* owner) {
    if (ctx) detail::contexts()[ctx].owner = owner;
}

// Call before destroying an ImGui context: drops its frame registry. Its
// touched widgets stay listed (last known value) but no longer update.
inline void forgetContext(ImGuiContext* ctx) {
    if (!ctx) return;
    auto it = detail::contexts().find(ctx);
    if (it != detail::contexts().end()) {
        // Values queued for its widgets are answered now (not drawn / as read back)
        detail::ContextState gone;
        gone.pendingValues = std::move(it->second.pendingValues);
        detail::contexts().erase(it);
        detail::finishPendingValues(gone, 0, true);
    }
    for (auto& t : detail::touched()) {
        if (t.ctx == ctx) t.ctx = nullptr;
    }
}

// Widgets changed by interaction since startup / the last resetTouched()
inline const std::vector<TouchedWidget>& getTouched() { return detail::touched(); }
inline void resetTouched() { detail::touched().clear(); }

// While one is alive, edits made through imgui widgets are not recorded as
// touched. For tools that keep their own record of what the user changed
// (tcxNodeInspector: its widgets are reused for whichever node is selected, so
// an ImGuiID can't say which node's value it was).
struct TouchedExclusionScope {
    TouchedExclusionScope()  { ++detail::touchedExcludeDepth; }
    ~TouchedExclusionScope() { --detail::touchedExcludeDepth; }
    TouchedExclusionScope(const TouchedExclusionScope&) = delete;
    TouchedExclusionScope& operator=(const TouchedExclusionScope&) = delete;
};

} // namespace tcx::imgui

// =============================================================================
// Test Engine Hook implementations
// These extern functions are called by ImGui internally when
// IMGUI_ENABLE_TEST_ENGINE is defined.
// =============================================================================

inline void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data) {
    if (!tcx::imgui::detail::collecting) return;

    auto& cs = tcx::imgui::detail::contexts()[ctx];

    tcx::imgui::WidgetInfo info;
    info.id = id;
    info.rect = bb;
    if (ctx->CurrentWindow) {
        info.windowName = ctx->CurrentWindow->Name;
    }
    if (item_data) {
        info.statusFlags = item_data->StatusFlags;
    }
    // EndListBox adds the list box's child window as an item under the list
    // box ID, with no ItemInfo: name it after the list box.
    if (auto lb = cs.listBoxes.find(id); lb != cs.listBoxes.end()) {
        info.label = lb->second.label;
        info.value.kind = ImGuiTcValueKind_ListBoxBegin;
    }

    size_t idx = cs.currentFrame.size();
    cs.currentFrame.push_back(std::move(info));
    cs.currentIdMap[id] = idx;
}

inline void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    namespace d = tcx::imgui::detail;
    if (!d::collecting) return;

    auto& cs = d::contexts()[ctx];
    if (flags & ImGuiItemStatusFlags_Edited) cs.editCount++;

    auto it = cs.currentIdMap.find(id);
    if (it == cs.currentIdMap.end()) return;

    auto& widget = cs.currentFrame[it->second];
    if (label) widget.label = label;
    widget.statusFlags = flags;

    // A pick inside a list box (its child window) is an edit of the list box,
    // not of the item picked (also covers custom BeginListBox/Selectable
    // lists). Checked before the combo popup: a list box inside a combo popup
    // is the closer owner of its items.
    if (ImGuiWindow* cw = ctx->CurrentWindow; cw && (cw->Flags & ImGuiWindowFlags_ChildWindow)) {
        auto lb = cs.listBoxes.find(cw->ChildId);
        if (lb != cs.listBoxes.end()) {
            if ((flags & ImGuiItemStatusFlags_Edited) && d::touchedExcludeDepth == 0 &&
                !d::findTouched(ctx, lb->first)) {
                auto& t = d::markTouched(ctx, lb->first);
                t.label = lb->second.label;
                t.windowName = lb->second.windowName;
            }
            return;
        }
    }

    // Likewise, a pick inside a combo popup is an edit of the combo (also
    // covers custom BeginCombo/Selectable combos).
    if (ctx->BeginComboDepth > 0) {
        if ((flags & ImGuiItemStatusFlags_Edited) && d::touchedExcludeDepth == 0 &&
            (size_t)ctx->BeginComboDepth <= cs.openCombos.size()) {
            const auto& combo = cs.openCombos[ctx->BeginComboDepth - 1];
            if (combo.id && !d::findTouched(ctx, combo.id)) {
                auto& t = d::markTouched(ctx, combo.id);
                t.label = combo.label;
                t.windowName = combo.windowName;
            }
        }
        return;
    }

    // Touched entries are created by the value hook only: an edit that no
    // value hook reports (a menu header, an action menu item, a plain
    // Selectable) changes no variable of the caller. Here the entries that
    // exist are only refreshed.
    if (widget.label.empty() || d::insideColorWidget(ctx)) return;
    tcx::imgui::TouchedWidget* t = d::findTouched(ctx, id);
    if (t) {
        t->label = widget.label;
        t->windowName = widget.windowName;
        t->statusFlags = flags;
    }
}

inline void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...) {
    (void)ctx;
    (void)fmt;
}

inline const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id) {
    auto& contexts = tcx::imgui::detail::contexts();
    auto cit = contexts.find(ctx);
    if (cit == contexts.end()) return nullptr;
    auto& cs = cit->second;
    auto it = cs.currentIdMap.find(id);
    if (it != cs.currentIdMap.end()) {
        auto& w = cs.currentFrame[it->second];
        if (!w.label.empty()) return w.label.c_str();
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// [TrussC] value hook (declared in imgui/imconfig.h)
// -----------------------------------------------------------------------------

// Runs when a value widget is entered, before it reads its variable. Writes a
// value the MCP tools queued for it; returns the edit count so far (see
// ImGuiTcItemValue::EditCountAtEntry).
inline unsigned int ImGuiTcHook_ItemEntry(ImGuiTcItemValue* item) {
    namespace d = tcx::imgui::detail;
    auto& cs = d::contexts()[item->Ctx];
    if (d::collecting && !cs.pendingValues.empty()) d::writePendingValue(cs, *item);
    return cs.editCount;
}

// Runs when a value widget returns. The widget's own item (or, for a
// composite widget, its group) is g.LastItemData at this point.
inline void ImGuiTcHook_ItemValue(const ImGuiTcItemValue* item) {
    namespace d = tcx::imgui::detail;
    if (!d::collecting || !item || !item->Ctx) return;
    ImGuiContext* ctx = item->Ctx;
    ImGuiWindow* window = static_cast<ImGuiWindow*>(item->Window);
    auto& cs = d::contexts()[ctx];

    // A value the tools queued was written at this widget's entry: read it back.
    if (!cs.pendingValues.empty()) d::readBackPendingValue(cs, *item);

    // Edited by this call: its own item, or any part of it (a component of
    // DragFloat3, ##X inside ColorEdit). Counting it also lets an enclosing
    // widget see this edit.
    // BeginCombo and BeginListBox return with their popup / child window
    // current, before anything in it was picked: they only say which widget.
    const bool opener = item->Kind == ImGuiTcValueKind_ComboPreview ||
                        item->Kind == ImGuiTcValueKind_ListBoxBegin;
    bool edited = cs.editCount != item->EditCountAtEntry;
    if (!opener && (ctx->LastItemData.StatusFlags & ImGuiItemStatusFlags_Edited)) {
        edited = true;
    }
    if (edited) cs.editCount++;

    // Nothing was submitted (collapsed / hidden window).
    if (!window || window->SkipItems) return;
    // A part of a ColorEdit/ColorPicker: the whole reports.
    if (d::insideColorWidget(ctx)) return;
    // The text field of a Ctrl+Click'ed Drag/Slider: that widget reports.
    if (item->Kind == ImGuiTcValueKind_Text && (item->Flags & ImGuiInputTextFlags_TempInput)) return;
    // A component of a multi-component widget (label ""): the group reports.
    if (!item->Label || !item->Label[0]) return;
    // MenuItem(label, shortcut, bool* p_selected) with p_selected == NULL: no variable.
    if (item->Kind == ImGuiTcValueKind_Bool && !item->Data) return;

    const ImGuiID id = item->Id ? item->Id : window->GetID(item->Label);

    // BeginCombo that opened its popup returns with the popup current.
    if (item->Kind == ImGuiTcValueKind_ComboPreview && ctx->BeginComboDepth > 0 &&
        ctx->CurrentWindow != window) {
        cs.openCombos.resize((size_t)ctx->BeginComboDepth);
        cs.openCombos.back() = {id, item->Label, window->Name};
    }
    // BeginListBox returns with its child window current; that window's
    // ChildId is the list box ID.
    if (item->Kind == ImGuiTcValueKind_ListBoxBegin && ctx->CurrentWindow != window &&
        ctx->CurrentWindow->ChildId == id) {
        cs.listBoxes[id] = {item->Label, window->Name};
    }

    tcx::imgui::WidgetValue value;
    d::captureValue(value, *item, ctx);

    // Frame registry. A single widget already has an entry (ItemAdd); a group
    // (DragFloat3, ColorEdit4, ...) never went through ItemAdd under its own
    // ID, so it gets one here when its group was visible.
    auto it = cs.currentIdMap.find(id);
    if (it != cs.currentIdMap.end()) {
        auto& entry = cs.currentFrame[it->second];
        // BeginCombo registers no label (no ItemInfo upstream); its hook runs
        // only once ItemAdd let the combo through, so name it here.
        if (item->Kind == ImGuiTcValueKind_ComboPreview && entry.label.empty()) entry.label = item->Label;
        d::mergeValue(entry.value, tcx::imgui::WidgetValue(value));
    } else if (!item->Id && (ctx->LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible)) {
        tcx::imgui::WidgetInfo info;
        info.id = id;
        info.label = item->Label;
        info.windowName = window->Name;
        info.rect = ctx->LastItemData.Rect;
        info.statusFlags = ctx->LastItemData.StatusFlags | ImGuiItemStatusFlags_Inputable;
        info.value = value;
        cs.currentIdMap[id] = cs.currentFrame.size();
        cs.currentFrame.push_back(std::move(info));
    }

    // Touched registry
    tcx::imgui::TouchedWidget* t = d::findTouched(ctx, id);
    if (!t && edited && !opener && d::touchedExcludeDepth == 0) {
        t = &d::markTouched(ctx, id);
    }
    if (t) {
        t->label = item->Label;
        t->windowName = window->Name;
        d::mergeValue(t->value, std::move(value));
    }
}
