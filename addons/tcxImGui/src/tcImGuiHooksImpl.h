// ImGui hooks and registry implementation. Include only in the platform TU
// that compiles Dear ImGui, so hooks and their helpers are defined once in
// the addon archive and never emitted by an app/host that includes the API.
#include "tcImGuiHooks.h"

namespace tcx::imgui::detail {

std::unordered_map<ImGuiContext*, ContextState>& contexts() {
    static HookRegistry<std::unordered_map<ImGuiContext*, ContextState>> m;
    return m.data;
}

std::vector<TouchedWidget>& touched() {
    static HookRegistry<std::vector<TouchedWidget>> v;
    return v.data;
}

std::chrono::steady_clock::time_point now() { return std::chrono::steady_clock::now() + clockSkew; }

TouchedWidget* findTouched(ImGuiContext* ctx, ImGuiID id) {
    for (auto& t : touched()) {
        if (t.ctx == ctx && t.id == id) return &t;
    }
    return nullptr;
}

TouchedWidget& markTouched(ImGuiContext* ctx, ImGuiID id) {
    touched().push_back(TouchedWidget{});
    TouchedWidget& t = touched().back();
    t.ctx = ctx;
    t.id = id;
    return t;
}

// Inside a ColorEdit/ColorPicker, its parts (##X, ##Text, ##picker, ...) run as
// widgets of their own on temporaries. The whole widget reports instead.
bool insideColorWidget(ImGuiContext* ctx) {
    return ctx->ColorEditCurrentID != 0;
}

void captureValue(WidgetValue& out, const ImGuiTcItemValue& item, ImGuiContext* ctx) {
    out = WidgetValue{};
    out.kind = item.Kind;
    switch (item.Kind) {
    case ImGuiTcValueKind_Text: {
        out.password = (item.Flags & ImGuiInputTextFlags_Password) != 0;
        const char* s = *static_cast<char* const*>(item.Data);
        if (!out.password && s) {
            size_t n = strnlen(s, WidgetValue::kMaxTextBytes + 1);
            out.truncated = n > WidgetValue::kMaxTextBytes;
            size_t len = out.truncated
                ? static_cast<size_t>(ImTextFindValidUtf8CodepointEnd(
                      s, s + n, s + WidgetValue::kMaxTextBytes) - s)
                : n;
            out.text.assign(s, len);
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
        if (item.Kind == ImGuiTcValueKind_Radio) out.buttonValue = item.Flags;   // v_button
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

// Combo's preview was read before the pick: keep it only if the index stayed.
void mergeValue(WidgetValue& dst, WidgetValue&& src, bool edited) {
    if (src.kind == ImGuiTcValueKind_Combo && !edited &&
        (dst.kind == ImGuiTcValueKind_ComboPreview || dst.kind == ImGuiTcValueKind_Combo)) {
        src.hasText = dst.hasText;
        src.text = std::move(dst.text);
    }
    dst = std::move(src);
}

// Whether the value hook can write a value of this kind: the kinds whose Data
// is the caller's variable of a fixed size. Not Text (the buffer size is not
// known), nor the openers (no variable).
bool isWritableKind(int kind) {
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

// Search all active scopes: a component or another value widget may be on
// top while ItemInfo runs inside a plain Combo / ListBox.
bool hasValueOwner(const ContextState& cs, int kind, ImGuiID id, int comboDepth) {
    for (size_t i = cs.valueSnapshotDepth; i > 0; --i) {
        const auto& snapshot = cs.valueSnapshots[i - 1];
        if (snapshot.item->Kind == kind && snapshot.id == id &&
            (kind != ImGuiTcValueKind_Combo || snapshot.comboDepth == comboDepth)) return true;
    }
    return false;
}

// Reclaim only tool events, preserving native events and other requests. Keep
// releases whose tool presses have already been consumed: trickling can split
// a request across frames. Retaining these in place preserves native ordering
// and needs neither a focus-loss event (which clears native state) nor a current
// context switch. Rebuild to release allocation even when this UI stays cached.
void settlePendingText(ImGuiContext* ctx, ContextState& cs, bool settleAll) {
    const auto t = now();
    bool expired = false;
    for (const auto& p : cs.pendingText) expired |= settleAll || t >= p.deadline;
    if (expired) {
        ImVector<ImGuiInputEvent> retained;
        for (const auto& event : ctx->InputEventsQueue) {
            bool drop = false;
            for (const auto& p : cs.pendingText) {
                if (!(settleAll || t >= p.deadline) || !p.contains(event.EventId)) continue;
                drop = true;
                for (int i = 0; i < p.releaseCount; ++i) {
                    // NewFrame consumes a prefix of the ordered event queue,
                    // including across EventId wrap. A press older than its
                    // first remaining event has already reached ImGui's IO.
                    const auto& pair = p.releases[i];
                    if (pair.release == event.EventId &&
                        ImU32(pair.press - ctx->InputEventsQueue[0].EventId) > UINT32_MAX / 2)
                        drop = false;
                }
                break;
            }
            if (!drop) retained.push_back(event);
        }
        ctx->InputEventsQueue.swap(retained);
    }
    // Requests and events are in insertion order. Walk the queue once, including
    // across EventId wrap, rather than scanning it again for every request.
    int eventIndex = 0;
    for (auto it = cs.pendingText.begin(); it != cs.pendingText.end();) {
        while (eventIndex < ctx->InputEventsQueue.Size &&
               ImU32(ctx->InputEventsQueue[eventIndex].EventId - it->firstEvent) > UINT32_MAX / 2)
            ++eventIndex;
        const bool queued = !settleAll && t < it->deadline &&
            eventIndex < ctx->InputEventsQueue.Size && it->contains(ctx->InputEventsQueue[eventIndex].EventId);
        if (!queued) it = cs.pendingText.erase(it);
        else ++it;
    }
}

size_t pendingValueCount() {
    size_t count = 0;
    for (const auto& entry : contexts())
        count += entry.second.pendingValues.size() + entry.second.pendingText.size();
    return count;
}

size_t pendingEventCount(const ContextState& cs) {
    size_t count = 0;
    for (const auto& p : cs.pendingText) count += p.reservedEvents;
    return count;
}

size_t pendingByteCount(const ContextState& cs) {
    size_t count = pendingEventCount(cs) * sizeof(ImGuiInputEvent);
    // Reserve the write, before and read-back buffers for fixed-size edits too.
    for (const auto& p : cs.pendingValues) count += 3 * p->bytes.size();
    return count;
}

bool admitPendingEdit(ImGuiContext* ctx, size_t events, size_t bytes) {
    size_t totalCount = 0, totalEvents = 0, totalBytes = 0;
    for (auto& [context, state] : contexts()) {
        settlePendingText(context, state);
        totalCount += state.pendingValues.size() + state.pendingText.size();
        totalEvents += pendingEventCount(state);
        totalBytes += pendingByteCount(state);
    }
    auto& cs = contexts()[ctx];
    if (cs.pendingValues.size() + cs.pendingText.size() >= kMaxPendingValuesPerContext ||
        totalCount >= kMaxPendingValuesTotal ||
        events > kMaxPendingEventsPerContext - pendingEventCount(cs) ||
        events > kMaxPendingEventsTotal - totalEvents ||
        bytes > kMaxPendingBytesPerContext - pendingByteCount(cs) ||
        bytes > kMaxPendingBytesTotal - totalBytes) {
        ++cs.rejectedPendingValues;
        ++rejectedPendingValues;
        return false;
    }
    return true;
}

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
                                                std::function<void()> onDone) {
    if (!admitPendingEdit(ctx, 0, 3 * bytes.size())) return nullptr;
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
    const auto t = now();
    p->deadline = t + lifetime;
    p->checkDeadline = t + checkWithin;
    p->queuedFrame = ctx->FrameCount;
    p->onDone = std::move(onDone);
    pending.push_back(p);
    return p;
}

// Whether a value widget refuses a written value: inside BeginDisabled() or
// disabled by the widget itself (MenuItem enabled = false,
// ImGuiSelectableFlags_Disabled: a Bool hook's Flags), or read-only (the
// ReadOnly item flag, ImGuiSliderFlags_ReadOnly, ImGuiInputTextFlags_ReadOnly
// on an InputScalar).
// The item flags in force for a value widget at its entry: those in force now,
// plus those set for the next item, which its ItemAdd() will apply
// (ColorPicker4 reads its ReadOnly from there too), plus its own once its
// ItemAdd() ran.
ImGuiItemFlags itemFlagsAtEntry(const ImGuiTcItemValue& item) {
    ImGuiContext* g = item.Ctx;
    ImGuiItemFlags f = g->CurrentItemFlags | g->NextItemData.ItemFlagsSet;
    if (item.Id && g->LastItemData.ID == item.Id) f |= g->LastItemData.ItemFlags;   // after its ItemAdd()
    return f;
}

PendingValue::State refusalFor(const ImGuiTcItemValue& item) {
    ImGuiItemFlags f = itemFlagsAtEntry(item);
    if (item.Kind == ImGuiTcValueKind_Bool) f |= item.Flags;   // the flags the widget adds for itself
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
void writePendingValue(ContextState& cs, ImGuiTcItemValue& item) {
    ImGuiWindow* window = static_cast<ImGuiWindow*>(item.Window);
    if (!window || window->SkipItems || !item.Data || !item.Label || !item.Label[0]) return;
    if (!isWritableKind(item.Kind) || insideColorWidget(item.Ctx)) return;
    const auto t = now();
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
        auto holds = [&](const std::vector<unsigned char>& v) {
            return v.size() == size && std::memcmp(item.Data, v.data(), size) == 0;
        };
        // The value itself; else the old value again (the app copied its own
        // value back, or turned the value back); else a third value (the app
        // took the value and changed it, e.g. a setter that converts it)
        p.state = holds(p.bytes)  ? PendingValue::State::Applied
                : holds(p.before) ? PendingValue::State::Reverted
                                  : PendingValue::State::Adjusted;
    }
    for (auto& pp : cs.pendingValues) {
        PendingValue& p = *pp;
        if (p.state != PendingValue::State::Queued || !matches(p)) continue;
        if (t >= p.deadline) {
            p.state = PendingValue::State::Expired;   // too late to be checked within the reply's time
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
        // A RadioButton(int*) sets its variable to its own value only (it
        // returns true when that is the value): another value belongs to the
        // button of the group whose value it is.
        if (item.Kind == ImGuiTcValueKind_Radio) {
            int wanted = 0;
            std::memcpy(&wanted, p.bytes.data(), sizeof(wanted));
            if (wanted != item.Flags) {
                captureValue(p.readBack, item, item.Ctx);   // for the reply: this button's value
                p.state = PendingValue::State::OtherButton;
                return;
            }
        }
        p.before.assign(static_cast<const unsigned char*>(item.Data),
                        static_cast<const unsigned char*>(item.Data) + size);
        std::memcpy(const_cast<void*>(item.Data), p.bytes.data(), size);
        p.state = PendingValue::State::Written;
        p.writtenBy = &item;
        p.writtenFrame = frame;
        // IMGUI_TC_RETURN: the widget returns true this frame, as for a change
        // by hand; not when the variable already held the value (nothing
        // changed: a toggle handler must not run). A mixed-state check box
        // (CheckboxFlags with only some of its bits set: ImGuiItemFlags_MixedValue,
        // its variable false) holds neither value, so false still returns true
        // and CheckboxFlags clears the bits.
        const bool mixed = item.Kind == ImGuiTcValueKind_Bool &&
                           (itemFlagsAtEntry(item) & ImGuiItemFlags_MixedValue) != 0;
        if (mixed || p.before != p.bytes) {
            ++injectedCount;
            item.Injected = true;
        }
        return;
    }
}

// At a value widget's return: read back the variable a value was written into
// at its entry.
void readBackPendingValue(ContextState& cs, const ImGuiTcItemValue& item) {
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
// was not drawn again, and its read-back at return stands. `settleAll`: no
// imgui frame will decide them (the context is going, or its window rendered
// a frame without imgui), so every value is settled now.
void finishPendingValues(ContextState& cs, int frame, bool settleAll) {
    std::vector<std::shared_ptr<PendingValue>> finished;
    for (auto it = cs.pendingValues.begin(); it != cs.pendingValues.end();) {
        PendingValue& p = **it;
        if (p.state == PendingValue::State::Queued && (settleAll || frame > p.queuedFrame)) {
            p.state = PendingValue::State::NotDrawn;
        } else if (p.state == PendingValue::State::Verify && (settleAll || frame > p.writtenFrame)) {
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

// Call once per frame of the window a context renders into, in a frame where
// that context ran no imgui frame (the app skipped imguiBegin() / imguiEnd()):
// the values queued for its widgets are settled as the end of an imgui frame
// would settle them. A value still queued was not drawn in the frame after
// the call; a value written in an earlier frame keeps its read-back at return.
// (tcxImGui: ImGuiManager's render listener.)
void settleWithoutImGuiFrame(ImGuiContext* ctx) {
    if (!ctx) return;
    auto it = contexts().find(ctx);
    if (it == contexts().end()) return;
    settlePendingText(ctx, it->second, true);
    finishPendingValues(it->second, ctx->FrameCount, true);
}

// Call regularly for window and offscreen contexts (tcxImGui: the main
// window's afterFrame, before tc::mcp's timeout drain): a value written into
// one of ctx's widgets whose check in the widget's next frame has not come by
// its checkDeadline (the window renders no frame: very slow, or it stopped
// rendering) is settled as applied on its read-back at return, as for a widget
// not drawn again, and handed to its onDone.
void settleOverdueValues(ImGuiContext* ctx) {
    if (!ctx) return;
    auto it = contexts().find(ctx);
    if (it == contexts().end()) return;
    settlePendingText(ctx, it->second);
    const auto t = now();
    bool any = false;
    for (auto& p : it->second.pendingValues) {
        if (p->state == PendingValue::State::Verify && t >= p->checkDeadline) {
            p->state = PendingValue::State::Applied;
            any = true;
        } else if (p->state == PendingValue::State::Queued && t >= p->deadline) {
            p->state = PendingValue::State::Expired;
            any = true;
        }
        // Superseded and other finished requests must also be reclaimed when
        // this context never draws another UI frame.
        any |= p->done();
    }
    // Outside any imgui frame of ctx: FrameCount is its last frame, which
    // leaves unexpired queued values and verification before its deadline alone
    if (any) finishPendingValues(it->second, ctx->FrameCount);
}

// Values written by the value hook so far that made their widget return true
// (every context). Code that acts on a widget's return value can compare it
// before and after the widget call: a change means the widget returned true
// for a value the MCP tools wrote (IMGUI_TC_RETURN), not for an edit by hand.
// (tcxNodeInspector: its own touched record.)
unsigned int injectedValueCount() { return injectedCount; }

} // namespace tcx::imgui::detail

namespace tcx::imgui {

// Begin frame: clear the current context's buffer. Also turns the hooks on for
// this context — every window's imgui runs its own context, and hook
// collection is a per-context switch.
void beginFrame() {
    if (!detail::collecting) return;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return;
    ctx->TestEngineHookItems = true;
    auto& cs = detail::contexts()[ctx];
    detail::settlePendingText(ctx, cs);
    cs.currentFrame.clear();
    cs.currentIdMap.clear();
    cs.listBoxes.clear();
}

// Swap frames: move current to last
void swapFrames() {
    if (!detail::collecting) return;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return;
    auto& cs = detail::contexts()[ctx];
    cs.lastFrame.swap(cs.currentFrame);
    cs.lastIdMap.swap(cs.currentIdMap);
    if (!cs.pendingValues.empty()) detail::finishPendingValues(cs, ctx->FrameCount);
}

// Enable/disable collection
void enableCollection() {
    if (auto* ctx = ImGui::GetCurrentContext()) ctx->TestEngineHookItems = true;
    detail::collecting = true;
}

void disableCollection() {
    for (auto& [ctx, cs] : detail::contexts()) ctx->TestEngineHookItems = false;
    if (auto* ctx = ImGui::GetCurrentContext()) ctx->TestEngineHookItems = false;
    detail::collecting = false;
}

// Tag a context with its owner (tcxImGui: the WindowContext it renders into).
void setContextOwner(ImGuiContext* ctx, const void* owner) {
    if (ctx) detail::contexts()[ctx].owner = owner;
}

// Call before destroying an ImGui context: drops its frame registry. Its
// touched widgets stay listed (last known value) but no longer update.
void forgetContext(ImGuiContext* ctx) {
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
const std::vector<TouchedWidget>& getTouched() { return detail::touched(); }

void resetTouched() { detail::touched().clear(); }

} // namespace tcx::imgui

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data) {
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

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
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
            if (d::hasValueOwner(cs, ImGuiTcValueKind_ListBox, lb->first)) return;
            if ((flags & ImGuiItemStatusFlags_Edited) && d::touchedExcludeDepth == 0 &&
                !(flags & ImGuiItemStatusFlags_Inputable) &&
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
            !(flags & ImGuiItemStatusFlags_Inputable) &&
            (size_t)ctx->BeginComboDepth <= cs.openCombos.size()) {
            const auto& combo = cs.openCombos[ctx->BeginComboDepth - 1];
            if (d::hasValueOwner(cs, ImGuiTcValueKind_Combo, combo.id, ctx->BeginComboDepth)) return;
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

void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...) {
    (void)ctx;
    (void)fmt;
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id) {
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

// Runs when a value widget is entered, before it reads its variable. Writes a
// value the MCP tools queued for it; returns the edit count so far (see
// ImGuiTcItemValue::EditCountAtEntry).
unsigned int ImGuiTcHook_ItemEntry(ImGuiTcItemValue* item) {
    namespace d = tcx::imgui::detail;
    auto& cs = d::contexts()[item->Ctx];
    if (d::collecting && !cs.pendingValues.empty()) d::writePendingValue(cs, *item);
    // Deliberately independent of collecting: Ctx pairs entry and exit even
    // if collection is disabled while the widget is running.
    if (item->Data && d::isWritableKind(item->Kind)) {
        if (cs.valueSnapshotDepth == cs.valueSnapshots.size()) cs.valueSnapshots.emplace_back();
        auto& snapshot = cs.valueSnapshots[cs.valueSnapshotDepth++];
        snapshot.item = item;
        auto* window = static_cast<ImGuiWindow*>(item->Window);
        snapshot.id = item->Id ? item->Id : (window && item->Label ? window->GetID(item->Label) : 0);
        snapshot.comboDepth = item->Ctx->BeginComboDepth + 1;
        const size_t size = ImGui::DataTypeGetInfo(item->DataType)->Size * (size_t)item->Components;
        const auto* bytes = static_cast<const unsigned char*>(item->Data);
        snapshot.bytes.assign(bytes, bytes + size);
    }
    return cs.editCount;
}

// Runs when a value widget returns. The widget's own item (or, for a
// composite widget, its group) is g.LastItemData at this point.
void ImGuiTcHook_ItemValue(const ImGuiTcItemValue* item) {
    namespace d = tcx::imgui::detail;
    if (!item || !item->Ctx) return;
    ImGuiContext* ctx = item->Ctx;
    ImGuiWindow* window = static_cast<ImGuiWindow*>(item->Window);
    auto& cs = d::contexts()[ctx];
    const bool hasSnapshot = item->Data && d::isWritableKind(item->Kind);
    bool edited = false;
    if (hasSnapshot) {
        IM_ASSERT(cs.valueSnapshotDepth > 0);
        const auto& snapshot = cs.valueSnapshots[--cs.valueSnapshotDepth];
        IM_ASSERT(snapshot.item == item);
        edited = std::memcmp(item->Data, snapshot.bytes.data(), snapshot.bytes.size()) != 0;
    }
    if (!d::collecting) return;

    // A value the tools queued was written at this widget's entry: read it back.
    if (!cs.pendingValues.empty()) d::readBackPendingValue(cs, *item);

    // Fixed-size values use only the comparison above. Text keeps the
    // Edited flag / edit counter; its buffer can be large and may be resized.
    // BeginCombo and BeginListBox return with their popup / child window
    // current, before anything in it was picked: they only say which widget.
    const bool opener = item->Kind == ImGuiTcValueKind_ComboPreview ||
                        item->Kind == ImGuiTcValueKind_ListBoxBegin;
    if (!hasSnapshot) {
        edited = cs.editCount != item->EditCountAtEntry;
        if (!opener && (ctx->LastItemData.StatusFlags & ImGuiItemStatusFlags_Edited)) edited = true;
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
        d::mergeValue(entry.value, tcx::imgui::WidgetValue(value), edited);
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
        d::mergeValue(t->value, std::move(value), edited);
    }
}
