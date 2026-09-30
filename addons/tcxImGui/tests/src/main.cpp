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
// =============================================================================

#include "imguiHarness.h"

using namespace std;
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

int main() {
    testMenus();
    testPanel();
    return harness::summary();
}
