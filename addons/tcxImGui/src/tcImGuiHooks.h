#pragma once

// =============================================================================
// tcImGuiHooks.h - ImGui hook declarations + Widget Registry
//
// Declares the functions required by IMGUI_ENABLE_TEST_ENGINE, plus the
// [TrussC] value hook (see imgui/imconfig.h) that reports the value of each
// value widget. Definitions live in tcImGuiHooksImpl.h, included only by the
// platform TU that compiles ImGui. Apps and tools share these declarations
// without emitting calls to ImGui internals in a hot-reload host.
//
// Two registries:
// - Per ImGui context, the widgets of the last completed frame (labels, rects,
//   status flags, values). Rebuilt every frame; what tcx_imgui_get_widgets
//   lists. One per context, so every window running imgui is listed.
// - "Touched": every value widget whose value was changed through the widget
//   (drag, typing, a click) since startup or the last resetTouched(). Kept
//   across frames with the last known value, so it survives the widget not
//   being drawn (collapsed tree, closed window). Fixed-size values are compared
//   at entry and exit; text uses ImGui's Edited flag / edit counter.
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
// (IMGUI_TC_RETURN) unless the variable already held the value. The variable is
// read back at its return, and checked again at the widget's entry in the next
// frame (or settled on the read-back at return, when that frame does not come
// in time: settleOverdueValues()). Snapshots are taken after that write, so
// the injection is not recorded as touched.
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
    int buttonValue = 0;               // Radio: this button's value (v_button), what pressing it sets

    // Bound (security): OS text edits can produce large widget values; a snapshot cap keeps retained text memory finite. Overflow truncates the snapshot at a UTF-8 boundary and sets truncated.
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
        Adjusted,       // at the next entry the variable held a third value: the app took the
                        //   value and changed it (a clamp, a conversion in its setter)
        Changed,        // read back at return: the widget changed it in the same frame
        Reverted,       // at the next entry the variable held its old value again: the app
                        //   ignores the return value and copies its own value in every frame
                        //   (or it turned the value back to the old one)
        Disabled,       // inside BeginDisabled() or disabled by the widget itself: not written
        ReadOnly,       // a read-only widget: not written
        NotDrawn,       // the widget did not run in the frame after the call: not written
        Expired,        // the widget ran only after the value's lifetime: not written
        ShapeChanged,   // the widget now reports another kind / type / component count
        Superseded,     // a later value was queued for the same widget before this one was written
        OtherButton,    // a RadioButton(int*) whose own value (v_button) is not the value: not written
    };
    ImGuiID id = 0;
    int kind = 0;                        // ImGuiTcValueKind_ the widget reported
    ImGuiDataType dataType = 0;
    int components = 0;
    std::vector<unsigned char> bytes;    // components * sizeof(dataType)
    std::vector<unsigned char> before;   // the variable just before the write
    std::chrono::steady_clock::time_point deadline;        // not written after this
    std::chrono::steady_clock::time_point checkDeadline;   // Verify: settled on the read-back at return
                                                           //   after this (settleOverdueValues())
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

// Input injected by a tool, identified separately from native input. Keep its
// reservation until all its events are consumed, or its write deadline expires.
struct PendingText {
    ImU32 firstEvent = 0, eventCount = 0;
    size_t reservedEvents = 0;
    std::chrono::steady_clock::time_point deadline;
    // Only pairs actually inserted by Add*Event (which deduplicates against
    // native state). Text injection presses Ctrl, mouse, A, Delete and Enter.
    struct Release { ImU32 press = 0, release = 0; };
    Release releases[5];
    int releaseCount = 0;
    bool contains(ImU32 id) const { return ImU32(id - firstEvent) < eventCount; }
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

    // Active fixed-size value widgets. Keep the slots and their byte buffers
    // when popping so both allocations are reused across widgets and frames.
    struct ValueSnapshot {
        const ImGuiTcItemValue* item = nullptr;
        ImGuiID id = 0;
        int comboDepth = 0;
        std::vector<unsigned char> bytes;
    };
    std::vector<ValueSnapshot> valueSnapshots;
    size_t valueSnapshotDepth = 0;

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
    std::vector<PendingText> pendingText;
    uint64_t rejectedPendingValues = 0;

    // Opaque owner tag (tcxImGui: the tc::internal::WindowContext*), so tools
    // can say which OS window a widget is in
    const void* owner = nullptr;
};

// Trivial process-lifetime flag: either registry's destructor closes runtime
// teardown before its storage disappears. Global owned managers can outlive both.
inline bool hookRegistriesAlive = true;
template<class T> struct HookRegistry {
    T data;
    ~HookRegistry() { hookRegistriesAlive = false; }
};
std::unordered_map<ImGuiContext*, ContextState>& contexts();

std::vector<TouchedWidget>& touched();

// Whether collection is active
inline bool collecting = false;

// The clock the queued values' deadlines follow: steady_clock, plus an offset
// the tests advance to emulate a slow window without sleeping.
inline std::chrono::steady_clock::duration clockSkew{};
std::chrono::steady_clock::time_point now();

// Values the value hook has written so far that made their widget return
// true, in every context. See injectedValueCount().
inline unsigned int injectedCount = 0;

// > 0 while a TouchedExclusionScope is open: edits are not recorded as touched
inline int touchedExcludeDepth = 0;

TouchedWidget* findTouched(ImGuiContext* ctx, ImGuiID id);

TouchedWidget& markTouched(ImGuiContext* ctx, ImGuiID id);

// Inside a ColorEdit/ColorPicker, its parts (##X, ##Text, ##picker, ...) run as
// widgets of their own on temporaries. The whole widget reports instead.
bool insideColorWidget(ImGuiContext* ctx);

void captureValue(WidgetValue& out, const ImGuiTcItemValue& item, ImGuiContext* ctx);

// Combo's preview was read before the pick: keep it only if the index stayed.
void mergeValue(WidgetValue& dst, WidgetValue&& src, bool edited);

// ---------------------------------------------------------------------------
// Values queued by the MCP tools
// ---------------------------------------------------------------------------

// Whether the value hook can write a value of this kind: the kinds whose Data
// is the caller's variable of a fixed size. Not Text (the buffer size is not
// known), nor the openers (no variable).
bool isWritableKind(int kind);

// Search all active scopes: a component or another value widget may be on
// top while ItemInfo runs inside a plain Combo / ListBox.
bool hasValueOwner(const ContextState& cs, int kind, ImGuiID id, int comboDepth = 0);

// Bound (security): pending tool edits, including text input, per UI context
// are bounded to 256 (1024 total). Injected input also reserves at most 8192
// events and 128 KiB per context (32768 events / 512 KiB total), BEFORE insertion.
// These budgets permit interactive bursts without retaining unlimited idle work.
// Bound (reply latency): unconsumed text uses the same 2 s write deadline as values.
inline constexpr std::chrono::seconds kPendingValueLifetime{2};
inline constexpr size_t kMaxPendingValuesPerContext = 256;
inline constexpr size_t kMaxPendingValuesTotal = 1024;
inline constexpr size_t kMaxPendingEventsPerContext = 8192;
inline constexpr size_t kMaxPendingEventsTotal = 32768;
inline constexpr size_t kMaxPendingBytesPerContext = 128 * 1024;
inline constexpr size_t kMaxPendingBytesTotal = 512 * 1024;
inline uint64_t rejectedPendingValues = 0;

// Reclaim only tool events, preserving native events and other requests. Keep
// releases whose tool presses have already been consumed: trickling can split
// a request across frames. Retaining these in place preserves native ordering
// and needs neither a focus-loss event (which clears native state) nor a current
// context switch. Rebuild to release allocation even when this UI stays cached.
void settlePendingText(ImGuiContext* ctx, ContextState& cs, bool settleAll = false);

size_t pendingValueCount();
size_t pendingEventCount(const ContextState& cs);
size_t pendingByteCount(const ContextState& cs);
bool admitPendingEdit(ImGuiContext* ctx, size_t events, size_t bytes);

// Queue `bytes` for the widget `id` of `ctx`, which reported `kind`,
// `dataType` and `components`. The value hook writes it the next time the
// widget runs, within `lifetime`; its check in the widget's next frame is
// waited for until `checkWithin` after now (see settleOverdueValues()). A value
// still queued for the same widget is superseded. `onDone` runs once the
// outcome is known (see finishPendingValues()). Returns null on overflow;
// rejection is counted and leaves existing edits (including supersession) untouched.
std::shared_ptr<PendingValue> queueValue(ImGuiContext* ctx, ImGuiID id, int kind,
                                                ImGuiDataType dataType, int components,
                                                std::vector<unsigned char> bytes,
                                                std::chrono::steady_clock::duration lifetime,
                                                std::chrono::steady_clock::duration checkWithin,
                                                std::function<void()> onDone);

// Whether a value widget refuses a written value: inside BeginDisabled() or
// disabled by the widget itself (MenuItem enabled = false,
// ImGuiSelectableFlags_Disabled: a Bool hook's Flags), or read-only (the
// ReadOnly item flag, ImGuiSliderFlags_ReadOnly, ImGuiInputTextFlags_ReadOnly
// on an InputScalar).
// The item flags in force for a value widget at its entry: those in force now,
// plus those set for the next item, which its ItemAdd() will apply
// (ColorPicker4 reads its ReadOnly from there too), plus its own once its
// ItemAdd() ran.
ImGuiItemFlags itemFlagsAtEntry(const ImGuiTcItemValue& item);

PendingValue::State refusalFor(const ImGuiTcItemValue& item);

// At a value widget's entry: first check a value written in an earlier frame
// (does the variable still hold it?), then write the value queued for it, if
// any, through Data, before the widget reads its variable.
void writePendingValue(ContextState& cs, ImGuiTcItemValue& item);

// At a value widget's return: read back the variable a value was written into
// at its entry.
void readBackPendingValue(ContextState& cs, const ImGuiTcItemValue& item);

// At the end of a context's frame: settle what this frame decided and hand
// each finished value to its onDone. A value still queued after a whole frame
// was not drawn; a value still waiting for its check a frame after the write
// was not drawn again, and its read-back at return stands. `settleAll`: no
// imgui frame will decide them (the context is going, or its window rendered
// a frame without imgui), so every value is settled now.
void finishPendingValues(ContextState& cs, int frame, bool settleAll = false);

// Call once per frame of the window a context renders into, in a frame where
// that context ran no imgui frame (the app skipped imguiBegin() / imguiEnd()):
// the values queued for its widgets are settled as the end of an imgui frame
// would settle them. A value still queued was not drawn in the frame after
// the call; a value written in an earlier frame keeps its read-back at return.
// (tcxImGui: ImGuiManager's render listener.)
void settleWithoutImGuiFrame(ImGuiContext* ctx);

// Call regularly for window and offscreen contexts (tcxImGui: the main
// window's afterFrame, before tc::mcp's timeout drain): a value written into
// one of ctx's widgets whose check in the widget's next frame has not come by
// its checkDeadline (the window renders no frame: very slow, or it stopped
// rendering) is settled as applied on its read-back at return, as for a widget
// not drawn again, and handed to its onDone.
void settleOverdueValues(ImGuiContext* ctx);

// Values written by the value hook so far that made their widget return true
// (every context). Code that acts on a widget's return value can compare it
// before and after the widget call: a change means the widget returned true
// for a value the MCP tools wrote (IMGUI_TC_RETURN), not for an edit by hand.
// (tcxNodeInspector: its own touched record.)
unsigned int injectedValueCount();

} // namespace detail

// Begin frame: clear the current context's buffer. Also turns the hooks on for
// this context — every window's imgui runs its own context, and hook
// collection is a per-context switch.
void beginFrame();

// Swap frames: move current to last
void swapFrames();

// Enable/disable collection
void enableCollection();

void disableCollection();

// Tag a context with its owner (tcxImGui: the WindowContext it renders into).
void setContextOwner(ImGuiContext* ctx, const void* owner);

// Call before destroying an ImGui context: drops its frame registry. Its
// touched widgets stay listed (last known value) but no longer update.
void forgetContext(ImGuiContext* ctx);

// Widgets changed by interaction since startup / the last resetTouched()
const std::vector<TouchedWidget>& getTouched();
void resetTouched();

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
// Test Engine Hook declarations
// These extern functions are called by ImGui internally when
// IMGUI_ENABLE_TEST_ENGINE is defined.
// =============================================================================

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data);

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags);

void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...);

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id);

// -----------------------------------------------------------------------------
// [TrussC] value hook (declared in imgui/imconfig.h)
// -----------------------------------------------------------------------------

// Runs when a value widget is entered, before it reads its variable. Writes a
// value the MCP tools queued for it; returns the edit count so far (see
// ImGuiTcItemValue::EditCountAtEntry).
unsigned int ImGuiTcHook_ItemEntry(ImGuiTcItemValue* item);

// Runs when a value widget returns. The widget's own item (or, for a
// composite widget, its group) is g.LastItemData at this point.
void ImGuiTcHook_ItemValue(const ImGuiTcItemValue* item);
