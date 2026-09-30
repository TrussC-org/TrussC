// =============================================================================
// tcxImGui tests - headless behavioral test (no window, no GPU).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
// Drives a real Dear ImGui context through imguiHarness.h: clicks go in as
// ImGuiIO events, and the checks read what the MCP tools read (the frame
// registry and the touched record filled by the hooks in tcImGuiHooks.h).
//
// Touched record (#322): only what a value hook reports is recorded.
//   - a toggle MenuItem(bool*) in a dropdown is recorded with its new value,
//     although the menu closes on the click
//   - Selectable(bool*) and Checkbox carry their new value
//   - RadioButton(int*) reports the variable under the pressed button's label
//   - a ListBox pick is recorded under the list box label with the index, not
//     under the item picked; a custom BeginListBox list under its label
//   - menu headers, action menu items, MenuItem(bool selected) used as a
//     toggle, plain Selectables and RadioButton(bool) are not recorded
//   - a Checkbox scrolled out of view still reports its variable
//   - a list box inside a combo popup owns the picks in it
//
// Setting values (#321): tcx_imgui_input on a value widget writes the value
// through the value hook, called as the MCP server calls it (the reply waits
// for the next frame).
//   - DragFloat2 (its centre is the gap between the fields), DragFloat3,
//     ColorEdit4 (0-1 floats), SliderAngle (radians), Checkbox, Combo, InputInt,
//     RadioButton, ListBox, MenuItem(bool*), Selectable(bool*): the variable
//     holds the value, and the reply carries the value read back
//   - a wrong shape or type, a widget not drawn in the next frame, and a hand
//     edit that changes the value in the same frame are errors
//   - injected values are not recorded as touched
//   - tcx_imgui_click on a composite widget is an error; text fields still type
//   - the widget returns true in the write frame, so getter/setter copies,
//     CheckboxFlags and ColorPicker3 take the value (once, without Edited); a
//     clipped Checkbox is written; a copy that ignores the return value gets
//     the verify error; disabled and read-only widgets are refused; items with
//     no variable that take no text (an action MenuItem, RadioButton(label,
//     bool), a button) are errors and are not pressed
//   - every settable widget used as a copy applied only on true takes the
//     value, whichever return it leaves by (clipped, popup open, text mode)
//   - a disabled MenuItem / Selectable and the ReadOnly flags are refused; a
//     clamp or conversion after the return is ok with the value held
//   - a frame without imgui settles the reply at once, and the value it
//     dropped is not written later; a widget that runs past the value's
//     lifetime is not written; a window that renders no frame after the write
//     gets its reply at the check deadline, ok on the read-back
//   - RadioButton(int*): each button lists its own value (buttonValue); only
//     the button whose value it is takes a value, its handler runs once;
//     another button's value is refused by the tool, and by the hook when the
//     button's value changed since it was listed
//   - the value the variable already holds changes nothing (no handler runs,
//     ok); a mixed-state CheckboxFlags still takes false and true
//   - tcxNodeInspector does not record an injected value as a hand edit
// =============================================================================

#include "imguiHarness.h"
#include <tcxNodeInspector.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

using namespace std;
using harness::callTool;
using harness::check;
using harness::ImGuiHarness;
using harness::touched;
using harness::touchedJson;

static bool isValue(const nlohmann::json& e, const char* widget, const nlohmann::json& value) {
    return e.is_object() && e.value("widget", "") == widget && e.contains("value") && e["value"] == value;
}

// ---------------------------------------------------------------------------
// Menus: a toggle item reports its new state; headers and action items are
// not recorded.
// ---------------------------------------------------------------------------
static void testMenus() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    bool showGrid = false;
    bool autosave = false;   // MenuItem(label, shortcut, bool selected) used as a toggle
    h.setUi([&] {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                ImGui::MenuItem("Save");
                if (ImGui::MenuItem("Autosave", nullptr, autosave)) autosave = !autosave;
                ImGui::MenuItem("No variable", nullptr, (bool*)nullptr);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Grid", nullptr, &showGrid);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    });
    h.frames(3);

    h.click("View");
    h.frames(2);
    check("menu: View opens", h.find("Show Grid") != nullptr);
    h.click("Show Grid");
    check("menu: Show Grid toggled on, menu closed", showGrid && h.find("Show Grid") == nullptr);
    nlohmann::json grid = touchedJson("Show Grid");
    check("menu: toggle item recorded with its new value (true)", isValue(grid, "checkbox", true));
    check("menu: toggle item reported as a bool", grid.is_object() && grid.value("valueType", "") == "bool");
    check("menu: toggle item not drawn right now (visible=false)", grid.is_object() && grid.value("visible", true) == false);

    h.click("File");
    h.frames(2);
    h.click("Save");
    h.click("File");
    h.frames(2);
    h.click("Autosave");
    check("menu: bool-selected toggle flipped by the app", autosave);
    h.click("File");
    h.frames(2);
    h.click("No variable");

    check("menu: header File not recorded", touched("File") == nullptr);
    check("menu: header View not recorded", touched("View") == nullptr);
    check("menu: action item Save not recorded", touched("Save") == nullptr);
    check("menu: MenuItem(bool selected) toggle not recorded", touched("Autosave") == nullptr);
    check("menu: MenuItem(bool* = NULL) not recorded", touched("No variable") == nullptr);
    check("menu: exactly one entry", tcx::imgui::getTouched().size() == 1);

    // Reopened, the item is drawn again and keeps its value
    h.click("View");
    h.frames(2);
    grid = touchedJson("Show Grid");
    check("menu: reopened, still true and visible",
          isValue(grid, "checkbox", true) && grid.value("visible", false) == true);
    const tcx::imgui::WidgetInfo* w = h.find("Show Grid");
    check("menu: get_widgets lists the toggle item's value",
          w && w->value.kind == ImGuiTcValueKind_Bool && w->value.bytes.size() == 1 && w->value.bytes[0] == 1);
}

// ---------------------------------------------------------------------------
// A panel of pick widgets
// ---------------------------------------------------------------------------
static void testPanel() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    bool enabled = false, pinned = false, flagRadio = false;
    int mode = 0, fruit = 1, color = 0;
    static const char* fruits[] = {"Apple", "Banana", "Cherry"};
    static const char* colors[] = {"Red", "Green", "Blue"};
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 30));
        ImGui::SetNextWindowSize(ImVec2(500, 700));
        ImGui::Begin("Params");
        ImGui::Checkbox("enabled", &enabled);
        ImGui::Selectable("pinned", &pinned);
        ImGui::Selectable("plain");
        ImGui::RadioButton("Mode A", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Mode B", &mode, 1);
        if (ImGui::RadioButton("flag radio", flagRadio)) flagRadio = !flagRadio;
        ImGui::ListBox("fruit", &fruit, fruits, 3);
        if (ImGui::BeginListBox("custom list", ImVec2(0, 80))) {
            for (int i = 0; i < 3; i++) {
                if (ImGui::Selectable(colors[i], color == i)) color = i;
            }
            ImGui::EndListBox();
        }
        ImGui::End();
    });
    h.frames(3);

    h.click("enabled");
    check("checkbox: toggled", enabled);
    check("checkbox: recorded with its value (true)", isValue(touchedJson("enabled"), "checkbox", true));

    h.click("pinned");
    check("selectable(bool*): toggled", pinned);
    check("selectable(bool*): recorded with its new value (true)", isValue(touchedJson("pinned"), "checkbox", true));

    h.click("plain");
    check("selectable: plain Selectable not recorded", touched("plain") == nullptr);

    h.click("Mode B");
    check("radio: mode set to 1", mode == 1);
    nlohmann::json modeB = touchedJson("Mode B");
    check("radio: pressed button recorded with the variable (1)", isValue(modeB, "radio", 1));
    check("radio: reported as an int", modeB.is_object() && modeB.value("valueType", "") == "int");
    check("radio: button not pressed not recorded", touched("Mode A") == nullptr);

    h.click("flag radio");
    check("radio: RadioButton(bool) toggled by the app", flagRadio);
    check("radio: RadioButton(bool) not recorded", touched("flag radio") == nullptr);

    h.click("Apple");
    check("listbox: fruit set to 0", fruit == 0);
    nlohmann::json lb = touchedJson("fruit");
    check("listbox: recorded under its label with the index (0)", isValue(lb, "listbox", 0));
    check("listbox: in the panel's window",
          lb.is_object() && lb.value("window", "") == "Params" && lb.value("visible", false) == true);
    check("listbox: item picked not recorded", touched("Apple") == nullptr);
    const tcx::imgui::WidgetInfo* w = h.find("fruit");
    check("listbox: get_widgets lists it under its label with the index",
          w && w->windowName == "Params" && w->value.kind == ImGuiTcValueKind_ListBox &&
          w->value.bytes.size() == 4 && w->value.bytes[0] == 0);

    h.click("Green");
    check("custom listbox: color set to 1", color == 1);
    nlohmann::json custom = touchedJson("custom list");
    check("custom listbox: recorded under its label, without a value",
          custom.is_object() && custom.value("widget", "") == "listbox" && !custom.contains("value") &&
          custom.value("window", "") == "Params");
    check("custom listbox: item picked not recorded", touched("Green") == nullptr);
    check("custom listbox: get_widgets lists it under its label", h.find("custom list") != nullptr);

    check("panel: exactly the value widgets recorded", tcx::imgui::getTouched().size() == 5);
}

// ---------------------------------------------------------------------------
// A Checkbox scrolled out of view still reports the variable
// ---------------------------------------------------------------------------
static void testClippedCheckbox() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    bool pushDown = false, farBox = false, nearBox = false;
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 30));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        ImGui::Begin("Scroll");
        ImGui::Checkbox("near", &nearBox);
        if (pushDown) ImGui::Dummy(ImVec2(10, 1000));
        ImGui::Checkbox("far", &farBox);
        ImGui::End();
    });
    h.frames(3);
    h.click("far");
    check("clipped checkbox: toggled on by hand", farBox && isValue(touchedJson("far"), "checkbox", true));
    h.click("near");   // move the nav focus off "far": ImGui never clips the focused item

    pushDown = true;   // now below the window's bottom edge: clipped
    h.frames(2);
    farBox = false;       // changed from code while clipped
    h.frames(2);
    const tcx::imgui::WidgetInfo* w = h.find("far");
    check("clipped checkbox: listed with its value",
          w && w->value.kind == ImGuiTcValueKind_Bool && w->value.bytes.size() == 1 && w->value.bytes[0] == 0);
    check("clipped checkbox: touched entry follows the variable", isValue(touchedJson("far"), "checkbox", false));
}

// ---------------------------------------------------------------------------
// A custom list box inside a custom combo's popup owns its picks
// ---------------------------------------------------------------------------
static void testListBoxInCombo() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    int shape = 0;
    static const char* shapes[] = {"Square", "Circle", "Star"};
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 30));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("Shapes");
        if (ImGui::BeginCombo("shape", shapes[shape])) {
            if (ImGui::BeginListBox("shape list", ImVec2(0, 80))) {
                for (int i = 0; i < 3; i++) {
                    if (ImGui::Selectable(shapes[i], shape == i)) shape = i;
                }
                ImGui::EndListBox();
            }
            ImGui::EndCombo();
        }
        ImGui::End();
    });
    h.frames(3);
    h.click("shape");
    h.frames(2);
    check("listbox in combo: popup open", h.find("Circle") != nullptr);
    h.click("Circle");
    check("listbox in combo: picked", shape == 1);
    check("listbox in combo: recorded under the list box", touched("shape list") != nullptr);
    check("listbox in combo: not recorded under the combo", touched("shape") == nullptr);
}

// ---------------------------------------------------------------------------
// tcx_imgui_input writes values through the value hook (#321)
// ---------------------------------------------------------------------------
static nlohmann::json input(ImGuiHarness& h, const string& label, const string& text, bool* deferred = nullptr) {
    return callTool(h, "tcx_imgui_input", {{"label", label}, {"text", text}}, deferred);
}

static bool isOk(const nlohmann::json& r) { return r.is_object() && r.value("status", "") == "ok"; }
static bool isError(const nlohmann::json& r) { return r.is_object() && r.value("status", "") == "error"; }

static void testValueInput() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    float v2[2] = {1, 2};
    float v3[3] = {1, 2, 3};
    float tint[4] = {1, 1, 1, 1};
    float angle = 0;
    bool flag = false, showGrid = false, pinned = false;
    int quality = 0, mode = 0, fruit = 1, count = 5;
    unsigned char level = 3;
    float extra = 0;
    bool drawExtra = true;
    char name[64] = "old";
    static const char* qualities[] = {"Low", "Medium", "High"};
    static const char* fruits[] = {"Apple", "Banana", "Cherry"};
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(600, 740));
        ImGui::Begin("Values");
        ImGui::SetNextItemWidth(200);
        ImGui::DragFloat2("##v2", v2);
        ImGui::DragFloat3("position", v3);
        ImGui::ColorEdit4("tint", tint);
        ImGui::SliderAngle("angle", &angle);
        ImGui::Checkbox("flag", &flag);
        ImGui::Combo("quality", &quality, qualities, 3);
        ImGui::InputInt("count", &count);
        ImGui::DragScalar("level", ImGuiDataType_U8, &level);
        ImGui::RadioButton("Mode A", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Mode B", &mode, 1);
        ImGui::ListBox("fruit", &fruit, fruits, 3);
        ImGui::MenuItem("Show Grid", nullptr, &showGrid);
        ImGui::Selectable("pinned", &pinned);
        ImGui::InputText("name", name, sizeof(name));
        if (drawExtra) ImGui::SliderFloat("extra", &extra, 0, 1);
        ImGui::End();
    });
    h.frames(3);

    // The DragFloat2 of the issue: its centre is the gap between the fields.
    const tcx::imgui::WidgetInfo* w = h.find("##v2");
    if (w) {
        const float mid = w->rect.GetCenter().x;
        const float fieldW = (200 - ImGui::GetStyle().ItemInnerSpacing.x) / 2;
        check("setup: DragFloat2 centre is in the gap between its fields",
              mid > w->rect.Min.x + fieldW && mid < w->rect.Min.x + fieldW + ImGui::GetStyle().ItemInnerSpacing.x);
    } else {
        check("setup: DragFloat2 listed", false);
    }

    bool deferred = false;
    int frames = 0;
    nlohmann::json r = callTool(h, "tcx_imgui_input", {{"label", "##v2"}, {"text", "[5, 6.5]"}}, &deferred, &frames);
    check("input: DragFloat2 set, reply after the write frame and the check frame",
          isOk(r) && deferred && frames == 2 && v2[0] == 5 && v2[1] == 6.5f);
    check("input: reply carries the value read back", r.is_object() && r.contains("value") && r["value"] == nlohmann::json({5, 6.5}));

    r = input(h, "position", "[0.5, -1, 2.25]");
    check("input: DragFloat3 set", isOk(r) && v3[0] == 0.5f && v3[1] == -1 && v3[2] == 2.25f);

    r = input(h, "tint", "[0.25, 0.5, 0.75, 0.125]");
    check("input: ColorEdit4 set as 0-1 floats",
          isOk(r) && tint[0] == 0.25f && tint[1] == 0.5f && tint[2] == 0.75f && tint[3] == 0.125f);

    r = input(h, "angle", "1.5");
    check("input: SliderAngle set in radians", isOk(r) && angle == 1.5f);

    r = input(h, "flag", "true");
    check("input: Checkbox set", isOk(r) && flag);

    r = input(h, "quality", "2");
    check("input: Combo set by index", isOk(r) && quality == 2);
    h.frame();
    w = h.find("quality");
    check("input: Combo shows the new item", w && w->value.hasText && w->value.text == "High");

    r = input(h, "count", "42");
    check("input: InputInt set", isOk(r) && count == 42);

    r = input(h, "Mode B", "1");
    check("input: RadioButton sets the group's variable to its own value",
          isOk(r) && mode == 1 && r.value("buttonValue", -1) == 1);

    r = input(h, "fruit", "2");
    check("input: ListBox set by index", isOk(r) && fruit == 2);

    r = input(h, "Show Grid", "true");
    check("input: MenuItem(bool*) set", isOk(r) && showGrid);

    r = input(h, "pinned", "true");
    check("input: Selectable(bool*) set", isOk(r) && pinned);

    // Injected values are not edits through the widget
    h.frame();
    check("input: injected values not recorded as touched", tcx::imgui::getTouched().empty());

    // Shape and type errors: nothing is written, and the reply says so
    r = input(h, "position", "[1, 2]");
    check("error: DragFloat3 given 2 values", isError(r) && v3[0] == 0.5f);
    r = input(h, "position", "7");
    check("error: DragFloat3 given a single number", isError(r) && v3[0] == 0.5f);
    r = input(h, "tint", "[1, \"x\", 0, 1]");
    check("error: ColorEdit4 given a string element", isError(r) && tint[0] == 0.25f);
    r = input(h, "flag", "0");
    check("error: Checkbox given a number", isError(r) && flag);
    r = input(h, "quality", "1.5");
    check("error: Combo given a fraction", isError(r) && quality == 2);
    r = input(h, "level", "300");
    check("error: uint8 drag given 300 (out of the type's range)", isError(r) && level == 3);
    r = input(h, "angle", "1e39");
    check("error: float given a number past FLT_MAX", isError(r) && angle == 1.5f);
    r = input(h, "angle", "fast");
    check("error: text that is not JSON", isError(r) && angle == 1.5f);
    r = input(h, "level", "200");
    check("input: uint8 drag set (no clamp to the widget's range)", isOk(r) && level == 200);

    // Not drawn in the frame after the call: error, and nothing is written later
    drawExtra = false;
    r = input(h, "extra", "0.75");
    check("error: widget not drawn in the next frame", isError(r) && extra == 0);
    drawExtra = true;
    h.frames(2);
    check("error: the dropped value is not written when the widget is drawn again", extra == 0);

    // Read back: a hand click on the checkbox in the same frame flips the
    // value just written, and the tool reports it instead of ok.
    flag = false;
    h.frames(2);
    w = h.find("flag");
    if (w) {
        h.mouseMove(w->rect.GetCenter());
        h.mouseButton(true);
        ImGui::SetCurrentContext(h.context());
        ImGui::GetIO().AddMouseButtonEvent(0, false);   // lands in the frame of the call
        r = input(h, "flag", "true");
        check("read-back: changed by a click in the same frame is an error", isError(r) && !flag);
        check("read-back: error carries the value the variable holds",
              r.is_object() && r.contains("value") && r["value"] == false);
    } else {
        check("read-back: checkbox listed", false);
    }

    // tcx_imgui_click on a composite widget
    r = callTool(h, "tcx_imgui_click", {{"label", "position"}});
    check("click: composite DragFloat3 is an error", isError(r));
    r = callTool(h, "tcx_imgui_click", {{"label", "##v2"}});
    check("click: composite DragFloat2 is an error", isError(r));
    r = callTool(h, "tcx_imgui_click", {{"label", "tint"}});
    check("click: composite ColorEdit4 is an error", isError(r));
    h.frames(2);
    check("click: nothing changed", v3[0] == 0.5f && v2[0] == 5 && tint[0] == 0.25f);
    r = callTool(h, "tcx_imgui_click", {{"label", "pinned"}});
    h.frames(4);
    check("click: a single widget is still clicked", isOk(r) && !pinned);

    // Text fields are still typed into
    r = input(h, "name", "hello", &deferred);
    h.frames(12);
    check("text: typed, answered at once", isOk(r) && !deferred && std::strcmp(name, "hello") == 0);
}

// ---------------------------------------------------------------------------
// The widget returns true in the frame a value is written (#321 decision),
// the next frame checks the variable kept it, and disabled / read-only widgets
// are refused.
// ---------------------------------------------------------------------------
struct FakeNode {
    float x = 0;
    float getX() const { return x; }
    void setX(float v) { x = v; }
};

static void testValueInputReturnsTrue() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    FakeNode node;
    unsigned int bits = 0;
    float pick[3] = {1, 1, 1};
    float counted = 0;
    int count = 0;
    bool editedSeen = false, farBox = false, boolRadio = false;
    int presses = 0;
    float model = 2;              // copied into `ignored` every frame, the return value ignored
    float disabledV = 1, readOnlyV = 1;
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(600, 740));
        ImGui::Begin("Copies");
        float x = node.getX();
        if (ImGui::DragFloat("node x", &x)) node.setX(x);
        ImGui::CheckboxFlags("bit 2", &bits, 4u);
        ImGui::ColorPicker3("picker", pick);
        if (ImGui::DragFloat("counted", &counted)) {
            ++count;
            editedSeen |= ImGui::IsItemEdited();
        }
        float ignored = model;
        ImGui::DragFloat("ignored", &ignored);
        ImGui::BeginDisabled();
        ImGui::DragFloat("disabled", &disabledV);
        ImGui::EndDisabled();
        ImGui::InputFloat("read only", &readOnlyV, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
        ImGui::MenuItem("action");
        if (ImGui::RadioButton("bool radio", boolRadio)) boolRadio = !boolRadio;
        if (ImGui::Button("press")) ++presses;
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(620, 10));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        ImGui::Begin("Far");
        ImGui::Dummy(ImVec2(10, 1000));
        ImGui::Checkbox("far", &farBox);   // below the window's bottom edge: clipped
        ImGui::End();
    });
    h.frames(3);

    nlohmann::json r = input(h, "node x", "3.5");
    check("returns true: getter/setter copy takes the value", isOk(r) && node.x == 3.5f);

    r = input(h, "bit 2", "true");
    check("returns true: CheckboxFlags sets the bit", isOk(r) && bits == 4u);

    r = input(h, "picker", "[0.25, 0.5, 0.75]");
    check("returns true: ColorPicker3 set", isOk(r) && pick[0] == 0.25f && pick[1] == 0.5f && pick[2] == 0.75f);

    r = input(h, "counted", "7");
    h.frames(3);
    check("returns true: exactly once per injection", isOk(r) && counted == 7 && count == 1);
    check("returns true: IsItemEdited() not set", !editedSeen);

    const tcx::imgui::WidgetInfo* w = h.find("far");
    check("clipped checkbox: listed with a bool value", w && w->value.kind == ImGuiTcValueKind_Bool);
    r = input(h, "far", "true");
    check("clipped checkbox: written", isOk(r) && farBox);

    r = input(h, "ignored", "5");
    check("verify: a copy that ignores the return value is an error",
          isError(r) && r.value("message", "").find("cannot be set from MCP") != std::string::npos);
    check("verify: the error carries the value the variable went back to",
          r.is_object() && r.contains("value") && r["value"] == 2);

    r = input(h, "disabled", "5");
    check("refused: widget inside BeginDisabled()",
          isError(r) && disabledV == 1 && r.value("message", "").find("disabled") != std::string::npos);
    r = input(h, "read only", "5");
    check("refused: read-only InputFloat",
          isError(r) && readOnlyV == 1 && r.value("message", "").find("read-only") != std::string::npos);

    r = input(h, "action", "true");
    check("no variable: an action MenuItem is an error, not typed into",
          isError(r) && r.value("message", "").find("use tcx_imgui_click") != std::string::npos);
    r = input(h, "bool radio", "true");
    h.frames(3);
    check("no variable: RadioButton(label, bool) is an error, not pressed",
          isError(r) && !boolRadio && r.value("message", "").find("no variable") != std::string::npos);
    r = input(h, "press", "1");
    h.frames(3);
    check("no variable: a button is an error pointing at tcx_imgui_click, not pressed",
          isError(r) && presses == 0 &&
          r.value("message", "").find("has no value to set; to press it, use tcx_imgui_click") != std::string::npos);

    h.frame();
    check("returns true: injected values not recorded as touched", tcx::imgui::getTouched().empty());
}

// ---------------------------------------------------------------------------
// Every settable widget used as a copy the app applies only when the widget
// returns true (`T c = model; if (Widget(&c)) model = c;`): the model takes
// the injected value, whichever return the widget leaves by.
// ---------------------------------------------------------------------------
static void testCopyOnReturn() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    float drag3M[3] = {0, 0, 0}, slider2M[2] = {0, 0}, colorM[4] = {1, 1, 1, 1}, picker4M[4] = {1, 1, 1, 1};
    float sliderM = 0, angleM = 0, vsliderM = 0, inputFloatM = 0, tempDragM = 0, tempSliderM = 0;
    int inputStepM = 0, inputEnterM = 0, input2M[2] = {0, 0}, comboM = 0, comboOpenM = 0, listM = 0,
        listClippedM = 0, radioM = 0;
    bool selM = false, menuM = false, farM = false, nearM = false;
    bool clipList = false;
    static const char* items[] = {"Zero", "One", "Two"};
    // Copy `model` into a local, run the widget on it, copy it back on true
    #define COPY_ON_TRUE(model, call) \
        do { auto c = model; if (call) model = c; } while (0)
    #define COPY_ARRAY_ON_TRUE(model, call) \
        do { decltype(model) c; std::memcpy(c, model, sizeof(c)); if (call) std::memcpy(model, c, sizeof(c)); } while (0)
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(600, 740));
        ImGui::Begin("Copies");
        COPY_ARRAY_ON_TRUE(drag3M, ImGui::DragFloat3("c drag3", c));
        COPY_ON_TRUE(sliderM, ImGui::SliderFloat("c slider", &c, 0, 10));
        COPY_ARRAY_ON_TRUE(slider2M, ImGui::SliderFloat2("c slider2", c, 0, 10));
        COPY_ON_TRUE(angleM, ImGui::SliderAngle("c angle", &c));
        COPY_ON_TRUE(vsliderM, ImGui::VSliderFloat("c vslider", ImVec2(20, 60), &c, 0, 10));
        COPY_ON_TRUE(inputStepM, ImGui::InputInt("c input step", &c));
        COPY_ON_TRUE(inputFloatM, ImGui::InputFloat("c input float", &c));
        COPY_ON_TRUE(inputEnterM, ImGui::InputInt("c input enter", &c, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue));
        COPY_ARRAY_ON_TRUE(input2M, ImGui::InputInt2("c input2", c));
        COPY_ARRAY_ON_TRUE(colorM, ImGui::ColorEdit4("c color", c));
        COPY_ON_TRUE(comboM, ImGui::Combo("c combo", &c, items, 3));
        COPY_ON_TRUE(comboOpenM, ImGui::Combo("c combo open", &c, items, 3));
        COPY_ON_TRUE(listM, ImGui::ListBox("c list", &c, items, 3));
        COPY_ON_TRUE(radioM, ImGui::RadioButton("c radio", &c, 1));
        COPY_ON_TRUE(selM, ImGui::Selectable("c selectable", &c));
        COPY_ON_TRUE(menuM, ImGui::MenuItem("c menu item", nullptr, &c));
        COPY_ON_TRUE(tempDragM, ImGui::DragFloat("c temp drag", &c));
        COPY_ON_TRUE(tempSliderM, ImGui::SliderFloat("c temp slider", &c, 0, 10));
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(620, 10));
        ImGui::SetNextWindowSize(ImVec2(300, 400));
        ImGui::Begin("Big");
        COPY_ARRAY_ON_TRUE(picker4M, ImGui::ColorPicker4("c picker4", c));
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(620, 420));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        ImGui::Begin("Far");
        ImGui::Checkbox("c near", &nearM);
        // Scrolled out of view once clipList is set (listed while visible)
        ImGui::Dummy(ImVec2(10, clipList ? 1000.0f : 1.0f));
        COPY_ON_TRUE(listClippedM, ImGui::ListBox("c list clipped", &c, items, 3));
        ImGui::Dummy(ImVec2(10, 1000));
        COPY_ON_TRUE(farM, ImGui::Checkbox("c far", &c));   // below the window's bottom edge: clipped
        ImGui::End();
    });
    #undef COPY_ON_TRUE
    #undef COPY_ARRAY_ON_TRUE
    h.frames(3);

    nlohmann::json r = input(h, "c drag3", "[1, 2, 3]");
    check("copy on true: DragFloat3", isOk(r) && drag3M[0] == 1 && drag3M[1] == 2 && drag3M[2] == 3);
    r = input(h, "c slider", "2.5");
    check("copy on true: SliderFloat", isOk(r) && sliderM == 2.5f);
    r = input(h, "c slider2", "[3, 4]");
    check("copy on true: SliderFloat2", isOk(r) && slider2M[0] == 3 && slider2M[1] == 4);
    r = input(h, "c angle", "0.5");
    check("copy on true: SliderAngle", isOk(r) && angleM == 0.5f);
    r = input(h, "c vslider", "6");
    check("copy on true: VSliderFloat", isOk(r) && vsliderM == 6);
    r = input(h, "c input step", "7");
    check("copy on true: InputInt with step buttons", isOk(r) && inputStepM == 7);
    r = input(h, "c input float", "1.25");
    check("copy on true: InputFloat without step", isOk(r) && inputFloatM == 1.25f);
    r = input(h, "c input enter", "9");
    check("copy on true: InputInt with EnterReturnsTrue", isOk(r) && inputEnterM == 9);
    r = input(h, "c input2", "[5, 6]");
    check("copy on true: InputInt2", isOk(r) && input2M[0] == 5 && input2M[1] == 6);
    r = input(h, "c color", "[0.25, 0.5, 0.75, 1]");
    check("copy on true: ColorEdit4", isOk(r) && colorM[0] == 0.25f && colorM[2] == 0.75f);
    r = input(h, "c picker4", "[0.5, 0.25, 0.125, 0.5]");
    check("copy on true: ColorPicker4", isOk(r) && picker4M[0] == 0.5f && picker4M[3] == 0.5f);
    r = input(h, "c combo", "2");
    check("copy on true: Combo, popup closed", isOk(r) && comboM == 2);
    r = input(h, "c list", "1");
    check("copy on true: ListBox", isOk(r) && listM == 1);
    r = input(h, "c radio", "1");
    check("copy on true: RadioButton(int*)", isOk(r) && radioM == 1);
    r = input(h, "c selectable", "true");
    check("copy on true: Selectable(bool*)", isOk(r) && selM);
    r = input(h, "c menu item", "true");
    check("copy on true: MenuItem(bool*)", isOk(r) && menuM);
    // ImGui never clips the nav-focused item: move the focus off "c far" first
    h.click("c near");
    const tcx::imgui::WidgetInfo* farW = h.find("c far");
    check("setup: \"c far\" is clipped and not focused",
          farW && farW->rect.Min.y > 768 && h.context()->NavId != farW->id);
    r = input(h, "c far", "true");
    check("copy on true: clipped Checkbox", isOk(r) && farM);

    clipList = true;   // listed in the last frame, clipped from the next one on
    r = input(h, "c list clipped", "2");
    check("copy on true: clipped ListBox", isOk(r) && listClippedM == 2);

    // Combo with its popup open
    if (h.click("c combo open")) {
        h.frames(2);
        check("setup: combo popup open", h.context()->OpenPopupStack.Size == 1);
        r = input(h, "c combo open", "1");
        check("copy on true: Combo, popup open", isOk(r) && comboOpenM == 1);
    } else {
        check("setup: combo listed", false);
    }
    h.clickAt(ImVec2(5, 5));   // close the popup
    h.frames(2);

    // Drag / Slider in Ctrl+Click text input mode
    auto ctrlClick = [&](const char* label) {
        const tcx::imgui::WidgetInfo* w = h.find(label);
        if (!w) return false;
        // As tcx_imgui_input's typing does: with ConfigMacOSXBehaviors (macOS)
        // imgui swaps Cmd and Ctrl, and Ctrl+click becomes a right click
        ImGui::SetCurrentContext(h.context());
        const ImGuiKey mod = ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
        ImGui::GetIO().AddKeyEvent(mod, true);
        h.clickAt(w->rect.GetCenter());
        ImGui::SetCurrentContext(h.context());
        ImGui::GetIO().AddKeyEvent(mod, false);
        h.frame();
        return ImGui::GetCurrentContext()->TempInputId != 0;
    };
    check("setup: DragFloat in text input mode", ctrlClick("c temp drag"));
    r = input(h, "c temp drag", "4");
    check("copy on true: DragFloat in text input mode", isOk(r) && tempDragM == 4);
    h.clickAt(ImVec2(5, 5));
    h.frames(2);
    check("setup: SliderFloat in text input mode", ctrlClick("c temp slider"));
    r = input(h, "c temp slider", "3");
    check("copy on true: SliderFloat in text input mode", isOk(r) && tempSliderM == 3);
    h.clickAt(ImVec2(5, 5));
    h.frames(2);
}

// ---------------------------------------------------------------------------
// Refusals the widget decides for itself, and what the app does with the
// value after the widget returned (#321 audit).
// ---------------------------------------------------------------------------
static void testRefusalsAndAdjust() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    bool autosave = false, selDisabled = false;
    int saved = 0, selRuns = 0;
    float roSlider = 1, roItem = 1, roPicker[4] = {1, 1, 1, 1};
    int clamped = 5;
    float quantized = 0;
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(600, 740));
        ImGui::Begin("Refusals");
        {
            bool c = autosave;
            if (ImGui::MenuItem("autosave", nullptr, &c, false)) { autosave = c; ++saved; }
        }
        {
            bool c = selDisabled;
            if (ImGui::Selectable("selectable disabled", &c, ImGuiSelectableFlags_Disabled)) { selDisabled = c; ++selRuns; }
        }
        ImGui::DragFloat("slider flag read only", &roSlider, 1, 0, 0, "%.3f", ImGuiSliderFlags_ReadOnly);
        ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, true);
        ImGui::SliderFloat("item flag read only", &roItem, 0, 10);
        ImGui::PopItemFlag();
        ImGui::GetCurrentContext()->NextItemData.ItemFlagsSet |= ImGuiItemFlags_ReadOnly;
        ImGui::ColorPicker4("picker read only", roPicker);
        if (ImGui::DragInt("clamped", &clamped)) clamped = std::max(clamped, 1);
        if (ImGui::DragFloat("quantized", &quantized)) quantized = std::round(quantized * 4) / 4;
        ImGui::End();
    });
    h.frames(3);

    nlohmann::json r = input(h, "autosave", "true");
    check("refused: MenuItem(bool*) with enabled = false",
          isError(r) && !autosave && saved == 0 && r.value("message", "").find("disabled") != std::string::npos);
    r = input(h, "selectable disabled", "true");
    check("refused: Selectable(bool*) with ImGuiSelectableFlags_Disabled",
          isError(r) && !selDisabled && selRuns == 0 && r.value("message", "").find("disabled") != std::string::npos);
    r = input(h, "slider flag read only", "5");
    check("refused: ImGuiSliderFlags_ReadOnly",
          isError(r) && roSlider == 1 && r.value("message", "").find("read-only") != std::string::npos);
    r = input(h, "item flag read only", "5");
    check("refused: PushItemFlag(ImGuiItemFlags_ReadOnly)",
          isError(r) && roItem == 1 && r.value("message", "").find("read-only") != std::string::npos);
    r = input(h, "picker read only", "[0, 0, 0, 1]");
    check("refused: ColorPicker4 with the ReadOnly flag set for the next item",
          isError(r) && roPicker[0] == 1 && r.value("message", "").find("read-only") != std::string::npos);

    // The app clamps the value it takes: ok, with what the variable holds
    r = input(h, "clamped", "0");
    check("adjusted: a clamp after the return is ok, not the revert error",
          isOk(r) && clamped == 1 && r.contains("message") && r.value("value", -1) == 1);
    r = input(h, "quantized", "0.3");
    check("adjusted: a conversion after the return is ok with the value held",
          isOk(r) && quantized == 0.25f && r.contains("value") && r["value"] == 0.25);
    // ... but the old value back is still the revert error
    r = input(h, "clamped", "-3");
    check("reverted: a clamp back to the old value is the revert error", isError(r) && clamped == 1);
}

// ---------------------------------------------------------------------------
// The reply when the app draws no imgui in the frame after the write or the
// call (a value that hides the GUI): answered in that frame, not by the core
// timeout. And a widget that runs too late for the reply is not written.
// ---------------------------------------------------------------------------
static void testFramesWithoutImGui() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    bool showGui = true, rendering = true;
    float late = 0, slow = 0;
    int slowRuns = 0;
    h.setImGuiWhen([&] { return showGui; });
    h.setRenderWhen([&] { return rendering; });
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("Panel");
        ImGui::Checkbox("Show GUI", &showGui);
        ImGui::DragFloat("late", &late);
        if (ImGui::DragFloat("slow", &slow)) ++slowRuns;
        ImGui::End();
    });
    h.frames(3);

    int frames = 0;
    nlohmann::json r = callTool(h, "tcx_imgui_input", {{"label", "Show GUI"}, {"text", "false"}}, nullptr, &frames);
    check("no imgui next frame: the write that hid the GUI is ok after two frames",
          isOk(r) && !showGui && frames == 2);

    r = callTool(h, "tcx_imgui_input", {{"label", "late"}, {"text", "7"}}, nullptr, &frames);
    check("no imgui next frame: a call while the GUI is hidden is answered at once as not drawn",
          isError(r) && late == 0 && frames == 1 && r.value("message", "").find("not drawn") != std::string::npos);
    // Shown again (by the app, not by the dropped value): "late" runs again
    // and keeps its own value
    showGui = true;
    h.frames(3);
    check("no imgui next frame: the dropped value is not written when the widget runs again",
          late == 0 && tcx::imgui::detail::contexts()[h.context()].pendingValues.empty());

    // Past the value's lifetime before the widget runs: not written
    r = callTool(h, "tcx_imgui_input", {{"label", "late"}, {"text", "3"}}, nullptr, &frames, 10, [](int i) {
        if (i == 0) std::this_thread::sleep_for(tcx::imgui::detail::kPendingValueLifetime + std::chrono::milliseconds(100));
    });
    check("expired: a widget that runs after the lifetime is not written",
          isError(r) && late == 0 && r.value("message", "").find("too late") != std::string::npos);

    // The window renders no frame after the write frame (very slow, or it
    // stopped): the value is settled on its read-back at return from the main
    // window's afterFrame, 4 s after the call (the clock is advanced before
    // frame 3), not left to the core's 5 s timeout and its generic error.
    namespace d = tcx::imgui::detail;
    r = callTool(h, "tcx_imgui_input", {{"label", "slow"}, {"text", "4"}}, nullptr, &frames, 10, [&](int i) {
        if (i == 1) rendering = false;
        if (i == 3) d::clockSkew += d::kValueCheckDeadline;
    });
    check("slow window: written value settled ok at the check deadline, not by the core timeout",
          isOk(r) && slow == 4 && slowRuns == 1 && frames == 4 && r.value("value", 0.0) == 4);
    d::clockSkew = {};
    rendering = true;
    h.frames(2);
}

// ---------------------------------------------------------------------------
// RadioButton(int*): true from a button means the variable holds that
// button's value, so a value is set through the button whose value it is.
// ---------------------------------------------------------------------------
static void testRadioButtons() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    int mode = 0, runsA = 0, runsB = 0, runsC = 0, valueC = 2;
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(600, 300));
        ImGui::Begin("Modes");
        if (ImGui::RadioButton("Mode A", &mode, 0)) ++runsA;
        ImGui::SameLine();
        if (ImGui::RadioButton("Mode B", &mode, 1)) ++runsB;
        ImGui::SameLine();
        if (ImGui::RadioButton("Mode C", &mode, valueC)) ++runsC;
        ImGui::End();
    });
    h.frames(3);

    nlohmann::json widgets = callTool(h, "tcx_imgui_get_widgets", nlohmann::json::object());
    nlohmann::json listedC;
    for (auto& e : widgets.value("widgets", nlohmann::json::array())) {
        if (e.value("label", "") == "Mode C") listedC = e;
    }
    check("radio: get_widgets lists each button's own value",
          isValue(listedC, "radio", 0) && listedC.value("buttonValue", -1) == 2);

    nlohmann::json r = input(h, "Mode C", "2");
    h.frames(2);
    check("radio: the matching button's handler runs once",
          isOk(r) && mode == 2 && runsC == 1 && runsA == 0 && runsB == 0);

    bool deferred = true;
    r = input(h, "Mode A", "1", &deferred);
    h.frames(2);
    check("radio: another button's value is refused at once, nothing written",
          isError(r) && !deferred && mode == 2 && runsA == 0 && runsB == 0 && runsC == 1);
    check("radio: the refusal names this button's value and the button to target",
          r.value("message", "").find("this button's value is 0") != std::string::npos &&
          r.value("message", "").find("target the button whose value is 1") != std::string::npos);

    // The button's value changed after it was listed: the value hook refuses it
    mode = 1;   // set by the app
    h.frames(2);
    r = callTool(h, "tcx_imgui_input", {{"label", "Mode C"}, {"text", "2"}}, nullptr, nullptr, 10, [&](int i) {
        if (i == 0) valueC = 5;   // listed with 2, runs as the button for 5
    });
    check("radio: a button whose value changed since it was listed is refused by the hook",
          isError(r) && mode == 1 && runsC == 1 &&
          r.value("message", "").find("this button's value is 5") != std::string::npos);
    valueC = 2;
    h.frames(2);
}

// ---------------------------------------------------------------------------
// A value the variable already holds changes nothing: the widget does not
// return true, so toggle handlers do not run. A mixed-state CheckboxFlags
// holds neither value and is still set.
// ---------------------------------------------------------------------------
static void testCurrentValue() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    bool fullscreen = false;
    int toggles = 0, drags = 0, radios = 0, mode = 1;
    float speed = 3;
    unsigned int bits = 1;   // flags_value 3: only one of its bits set (mixed)
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(600, 300));
        ImGui::Begin("Current");
        {
            bool c = fullscreen;
            if (ImGui::MenuItem("Fullscreen", nullptr, &c)) { fullscreen = c; ++toggles; }
        }
        if (ImGui::DragFloat("speed", &speed)) ++drags;
        if (ImGui::RadioButton("mode one", &mode, 1)) ++radios;
        ImGui::CheckboxFlags("mixed", &bits, 3u);
        ImGui::End();
    });
    h.frames(3);

    nlohmann::json r = input(h, "Fullscreen", "false");
    h.frames(2);
    check("current value: a toggle MenuItem set to its value runs no handler",
          isOk(r) && !fullscreen && toggles == 0 && r["value"] == false);
    r = input(h, "speed", "3");
    h.frames(2);
    check("current value: a drag set to its value does not return true", isOk(r) && speed == 3 && drags == 0);
    r = input(h, "mode one", "1");
    h.frames(2);
    check("current value: the radio button already selected does not return true",
          isOk(r) && mode == 1 && radios == 0);

    r = input(h, "mixed", "false");
    check("mixed CheckboxFlags: false clears its bits", isOk(r) && bits == 0);
    bits = 1;
    h.frames(2);
    r = input(h, "mixed", "true");
    check("mixed CheckboxFlags: true sets its bits", isOk(r) && bits == 3);
    r = input(h, "mixed", "true");
    check("mixed CheckboxFlags: set again, no change", isOk(r) && bits == 3);
}

// ---------------------------------------------------------------------------
// tcxNodeInspector records its own touched list from the widget's return
// value: an injected value is applied but not recorded as a hand edit.
// ---------------------------------------------------------------------------
static void testInspectorRecord() {
    ImGuiHarness h;
    tcx::imgui::resetTouched();

    float radius = 1;
    bool visible = false;
    std::vector<std::string> recorded;
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("Inspector");
        tcx::nodeinspector::ImGuiReflector r;
        r.visit("radius", radius);
        r.visit("visible", visible);
        for (auto& e : r.edited) recorded.push_back(e);
        ImGui::End();
    });
    h.frames(3);

    nlohmann::json r = input(h, "radius", "10");
    check("inspector: injected value applied", isOk(r) && radius == 10);
    check("inspector: injected value not recorded as an edit", recorded.empty());
    h.click("visible");
    check("inspector: a hand click is still recorded",
          visible && std::find(recorded.begin(), recorded.end(), "visible") != recorded.end());
}

int main() {
    testMenus();
    testPanel();
    testClippedCheckbox();
    testListBoxInCombo();
    testValueInput();
    testValueInputReturnsTrue();
    testCopyOnReturn();
    testRefusalsAndAdjust();
    testFramesWithoutImGui();
    testRadioButtons();
    testCurrentValue();
    testInspectorRecord();
    return harness::summary();
}
