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
//     the verify error; disabled and read-only widgets are refused
// =============================================================================

#include "imguiHarness.h"

#include <cmath>
#include <cstring>

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

    bool pushDown = false, far = false, near = false;
    h.setUi([&] {
        ImGui::SetNextWindowPos(ImVec2(10, 30));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        ImGui::Begin("Scroll");
        ImGui::Checkbox("near", &near);
        if (pushDown) ImGui::Dummy(ImVec2(10, 1000));
        ImGui::Checkbox("far", &far);
        ImGui::End();
    });
    h.frames(3);
    h.click("far");
    check("clipped checkbox: toggled on by hand", far && isValue(touchedJson("far"), "checkbox", true));
    h.click("near");   // move the nav focus off "far": ImGui never clips the focused item

    pushDown = true;   // now below the window's bottom edge: clipped
    h.frames(2);
    far = false;       // changed from code while clipped
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

    r = input(h, "Mode A", "1");
    check("input: RadioButton sets the group's variable", isOk(r) && mode == 1);

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
    bool editedSeen = false, far = false;
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
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(620, 10));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        ImGui::Begin("Far");
        ImGui::Dummy(ImVec2(10, 1000));
        ImGui::Checkbox("far", &far);   // below the window's bottom edge: clipped
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
    check("clipped checkbox: written", isOk(r) && far);

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
    check("no variable: an action MenuItem is an error, not typed into", isError(r));

    h.frame();
    check("returns true: injected values not recorded as touched", tcx::imgui::getTouched().empty());
}

int main() {
    testMenus();
    testPanel();
    testClippedCheckbox();
    testListBoxInCombo();
    testValueInput();
    testValueInputReturnsTrue();
    return harness::summary();
}
