#include <TrussC.h>
#include <tcxImGui.h>
#include "ProjectHub.h"
#include "GeneratedFile.h"
#include "Strings.h"
#include <algorithm>
#include <fstream>
#include <future>
#include <mutex>
#include <memory>
#include <cstdlib>
#include <utility>

using namespace tc;
using namespace tcx;
namespace fs = std::filesystem;
using gui2::text;

namespace {
// Prefer separators when wrapping paths; fall back to complete UTF-8
// characters so every character remains visible at narrow widths.
int wrapped(const std::string& value, bool measureOnly = false, float availableWidth = 0) {
    const float width = (std::max)(1.0f, availableWidth > 0 ? availableWidth : ImGui::GetContentRegionAvail().x);
    int lines = 0;
    const char* start = value.data();
    const char* end = start + value.size();
    while (start < end) {
        const char* cursor = start;
        const char* separator = nullptr;
        while (cursor < end && *cursor != '\n') {
            const char* next = cursor + 1;
            while (next < end && (static_cast<unsigned char>(*next) & 0xc0) == 0x80) ++next;
            if (cursor > start && ImGui::CalcTextSize(start, next).x > width) break;
            cursor = next;
            if (cursor[-1] == '/' || cursor[-1] == '\\') separator = cursor;
        }
        if (cursor < end && *cursor != '\n' && separator) cursor = separator;
        ++lines;
        if (!measureOnly) {
            ImGui::TextUnformatted(start, cursor);
            if (ImGui::IsItemClicked()) ImGui::SetClipboardText(value.c_str());
        }
        start = cursor < end && *cursor == '\n' ? cursor + 1 : cursor;
    }
    return lines;
}
bool button(const char* id) { return ImGui::Button(text(id)); }
void label(const char* id) { ImGui::TextUnformatted(text(id)); }

class HubApp : public App {
    projectHub::Snapshot project;
    Json preferences = Json::object();
    std::vector<std::string> recents;
    std::string root, details, status = text("hub.ready");
    bool home = true, mcp = true, showDetails = false, failed = false, dialogOpen = false;
    bool pathSelected = false;
    std::mutex logMutex;
    std::future<projectOps::Result> operation;
    std::unique_ptr<projectOps::CancelHandle> cancel;
    std::string createdPath;

    bool busy() const { return operation.valid() || dialogOpen; }
    void log(const std::string& message, projectOps::MessageKind kind = projectOps::MessageKind::Log) {
        logNotice("trusscli-gui2") << message;
        std::lock_guard<std::mutex> lock(logMutex);
        details += message;
        if (kind != projectOps::MessageKind::Output && kind != projectOps::MessageKind::ErrorOutput) details += "\n";
    }
    std::string logText() {
        std::lock_guard<std::mutex> lock(logMutex);
        return details;
    }
    void savePreferences() {
        try {
            preferences["recents"] = recents;
            preferences["framework"] = root;
            auto path = projectHub::preferencesPath();
            fs::create_directories(path.parent_path());
            writeGeneratedFile(path, preferences.dump(2));
        } catch (const std::exception& e) { log(e.what()); }
    }
    void error(const std::string& message) {
        log(message);
        failed = true;
        status = text("monitor.failed");
        showDetails = true;
    }
    void select(const fs::path& path) {
        if (busy()) { log(text("project.busy")); return; }
        auto selected = projectHub::inspect(path, root);
        if (!selected.error.empty()) { error(pathToUtf8(path) + "\n" + selected.error); return; }
        project = std::move(selected);
        pathSelected = false;
        for (const auto& message : project.diagnostics) log(message);
        if (!project.diagnostics.empty()) showDetails = true;
        mcp = true;
        if (preferences.contains("monitor") && preferences["monitor"].is_object()) {
            auto saved = preferences["monitor"].find(project.path);
            if (saved != preferences["monitor"].end() && saved->is_boolean()) mcp = saved->get<bool>();
        }
        recents.erase(std::remove(recents.begin(), recents.end(), project.path), recents.end());
        recents.insert(recents.begin(), project.path);
        home = false;
        failed = false;
        status = text("hub.ready");
        savePreferences();
    }
    void pick(bool framework) {
        dialogOpen = true;
        loadDialogAsync(text(framework ? "home.choose_root" : "home.open"), "", {}, true,
            [this, framework](const FileDialogResult& result) {
                dialogOpen = false;
                if (!result.success) return;
                if (framework) {
                    if (!projectHub::usableFramework(result.filePath)) { error(text("home.root_missing")); return; }
                    root = pathToUtf8(result.filePath);
                    savePreferences();
                } else select(result.filePath);
            });
    }
    void newProject() {
        try {
            if (!projectHub::usableFramework(utf8ToPath(root))) { error(text("home.root_missing")); return; }
            auto directory = projectHub::documentsDirectory() / "trussc-projects";
            const bool checkout = projectHub::defaultIsFramework(directory);
            if (checkout) log(text("home.framework_folder"));
            else fs::create_directories(directory);
            dialogOpen = true;
            saveDialogAsync(text("home.new"), checkout ? text("home.framework_folder") : "",
                directory, text("home.default_name"), [this](const FileDialogResult& result) {
                    dialogOpen = false;
                    if (!result.success) return;
                    createdPath = pathToUtf8(result.filePath);
                    ProjectSettings settings;
                    settings.tcRoot = root;
                    status = text("home.creating");
                    start([this, path = result.filePath, settings] {
                        return projectHub::create(path, settings, callback());
                    });
                });
        } catch (const std::exception& e) { error(e.what()); }
    }
    projectOps::LogCallback callback() {
        return [this](const std::string& message, projectOps::MessageKind kind) { log(message, kind); };
    }
    void start(std::function<projectOps::Result()> action) {
        failed = false;
        showDetails = true;
        operation = std::async(std::launch::async, [action] {
            try { return action(); }
            catch (const std::exception& e) {
                projectOps::Result result;
                result.failureReason = e.what();
                return result;
            }
        });
    }
    projectOps::UpdateInput updateInput() {
        projectOps::UpdateInput input;
        input.projectPath = project.path;
        input.executablePath = getExecutablePath();
        // Fallback is detection context, never an implicit use-local write.
        input.fetchVersion = false;
        return input;
    }
    void useFallback() {
        if (!projectHub::usableFramework(utf8ToPath(root))) return;
#ifdef _WIN32
        _putenv_s("TRUSSC_DIR", root.c_str());
#else
        setenv("TRUSSC_DIR", root.c_str(), 1);
#endif
    }
    void run(bool buildOnly) {
        useFallback();
        projectOps::RunInput input;
        input.build.update = updateInput();
        input.build.mcp = mcp;
        input.build.confirmFetch = [this](const std::vector<std::string>& missing) {
            for (const auto& spec : missing) log(spec);
            return false; // Source-acquisition approval UI comes in a later slice.
        };
        cancel = std::make_unique<projectOps::CancelHandle>();
        input.build.cancel = cancel.get();
        status = text("monitor.building");
        start([this, input, buildOnly] {
            auto result = buildOnly ? projectOps::build(input.build, callback()) :
                projectOps::run(input, callback(), cancel.get());
            return result;
        });
        buildOnlyResult = buildOnly;
    }
    bool buildOnlyResult = false;
    void card() {
        label("app.title");
        ImGui::SameLine();
        label(home ? "nav.home" : "nav.hub");
        if (!project.path.empty()) {
            ImGui::SameLine();
            ImGui::BeginDisabled(busy());
            if (button("project.change")) home = true;
            ImGui::EndDisabled();
        }
        if (busy()) label("project.busy");
        ImGui::Separator();
        label("project.current");
        if (project.path.empty()) { label("project.none"); return; }
        wrapped(project.name);
        if (button("project.copy_path")) ImGui::SetClipboardText(project.path.c_str());
        // Header has its own scrolling region; the copy action never scrolls out.
        const int lines = wrapped(project.path, true, ImGui::GetContentRegionAvail().x - 48);
        const float pathHeight = std::clamp(lines * ImGui::GetTextLineHeightWithSpacing() + 36, 48.0f, 130.0f);
        ImGui::BeginChild("##project-path", ImVec2(0, pathHeight), ImGuiChildFlags_Borders);
        const auto pathOrigin = ImGui::GetCursorScreenPos();
        const float textHeight = wrapped(project.path, true) * ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::Selectable("##select-path", pathSelected, 0, ImVec2(0, textHeight))) {
            pathSelected = true;
            ImGui::SetClipboardText(project.path.c_str());
        }
        ImGui::SetCursorScreenPos(pathOrigin);
        wrapped(project.path);
        ImGui::EndChild();
        std::string summary = text("project.summary");
        auto replace = [&](const std::string& key, const std::string& value) { summary.replace(summary.find(key), key.size(), value); };
        replace("{version}", !project.pin.empty() && project.localRoot.empty() ? project.pin : project.commit.empty() ? text("monitor.no_sample") : project.commit);
        replace("{source}", text(!project.localRoot.empty() ? "advanced.override" : !project.pin.empty() ? "source.pinned" : "advanced.fallback"));
        replace("{count}", std::to_string(project.addons.size()));
        wrapped(summary);
        if (!project.missing.empty()) {
            label("advanced.cache");
            ImGui::SameLine();
            label("advanced.missing");
        }
    }
    void homeView() {
        ImGui::Spacing();
        label("project.drop");
        ImGui::BeginDisabled(busy());
        if (button("home.open")) pick(false);
        ImGui::SameLine();
        if (button("home.new")) newProject();
        if (!projectHub::usableFramework(utf8ToPath(root))) {
            wrapped(text("home.root_missing"));
            if (button("home.choose_root")) pick(true);
        }
        ImGui::Spacing();
        ImGui::SeparatorText(text("home.recents"));
        if (recents.empty()) wrapped(text("home.empty"));
        // select() updates recents, so render a stable copy of this list.
        auto rows = recents;
        for (const auto& path : rows) {
            ImGui::PushID(path.c_str());
            if (ImGui::Selectable(pathToUtf8(utf8ToPath(path).filename()).c_str())) select(utf8ToPath(path));
            wrapped(path);
            if (!fs::is_directory(utf8ToPath(path))) label("home.missing");
            ImGui::Spacing();
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }
    void hubView() {
        label("project.drop");
        ImGui::BeginDisabled(busy());
        if (button(ImGui::GetIO().KeyShift ? "hub.build" : "hub.run")) run(ImGui::GetIO().KeyShift);
        ImGui::SameLine();
        label("hub.shift");
        ImGui::SameLine();
        if (ImGui::Checkbox(text("hub.mcp"), &mcp)) {
            preferences["monitor"][project.path] = mcp;
            savePreferences();
        }
        if (button("hub.edit")) {
            const auto& preset = project.presets;
            std::string problem;
            if (preset.hasIde && preset.ide != IdeType::CMakeOnly) problem = IdeHelper::openInIde(preset.ide, project.path);
            else {
                problem = projectHub::revealFolder(utf8ToPath(project.path) / "src");
                log(text("hub.edit_hint"));
            }
            if (!problem.empty()) error(problem);
        }
        ImGui::SameLine();
        if (button("hub.open_folder")) {
            auto problem = projectHub::revealFolder(utf8ToPath(project.path));
            if (!problem.empty()) error(problem);
        }
        ImGui::EndDisabled();
        wrapped(text("hub.app_hint"));
        if (operation.valid() && !cancel) label("update.no_cancel");
        if (operation.valid() && cancel) {
            if (button("monitor.stop")) { cancel->requestStop(); status = text("monitor.stopping"); }
            if (cancel->state() == projectOps::ProcessState::StillRunning) {
                label("monitor.force_prompt");
                if (button("monitor.force")) cancel->forceStop();
            }
        }
    }
public:
    void setup() override {
        imguiSetup();
        setFps(30);
        ImGui::GetIO().IniFilename = nullptr;
        auto font = projectHub::japaneseFont();
        if (!font.empty()) {
            if (auto loaded = ImGui::GetIO().Fonts->AddFontFromFileTTF(font.c_str(), 18.0f)) ImGui::GetIO().FontDefault = loaded;
        }
        auto& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(24, 18);
        style.ItemSpacing = ImVec2(12, 10);
        style.FramePadding = ImVec2(12, 7);
        style.FrameRounding = 4;
        try {
            std::ifstream file(projectHub::preferencesPath());
            if (file) {
                auto data = Json::parse(file);
                if (data.is_object()) preferences = std::move(data);
            }
            if (preferences.contains("recents") && preferences["recents"].is_array())
                for (const auto& item : preferences["recents"]) if (item.is_string()) recents.push_back(item.get<std::string>());
            root = preferences.value("framework", std::string{});
            if (!projectHub::usableFramework(utf8ToPath(root))) root = projectOps::detectTcRoot(getExecutablePath());
        } catch (const std::exception& e) { log(e.what()); }
        ProjectGenerator::runChildrenHidden = true;
    }
    void update() override {
        if (operation.valid() && operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = operation.get();
            if (result.stopped) status = text("monitor.stopped");
            else if (!result.success) error(result.failureReason);
            else status = text(buildOnlyResult ? "hub.built" : "monitor.finished");
            cancel.reset();
            if (result.success && createdPath.empty() && !project.path.empty()) {
                auto refreshed = projectHub::inspect(utf8ToPath(project.path), root);
                if (refreshed.error.empty()) project = std::move(refreshed);
                else log(refreshed.error);
            }
            if (!createdPath.empty()) {
                auto path = std::exchange(createdPath, {});
                if (result.success) select(utf8ToPath(path));
            }
            buildOnlyResult = false;
        }
    }
    void filesDropped(const std::vector<std::string>& paths) override {
        if (!paths.empty()) select(utf8ToPath(paths.front()));
    }
    void draw() override {
        clear(0.08f);
        if (!imguiBegin()) return;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("##hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        card();
        ImGui::Separator();
        ImGui::BeginChild("##content", ImVec2(0, -65));
        if (home) homeView(); else hubView();
        if (failed) {
            wrapped(text("common.recovery"));
            wrapped(text("advanced.update_hint"));
            ImGui::BeginDisabled(busy() || project.path.empty());
            if (button("common.update")) {
                useFallback();
                auto input = updateInput();
                input.fetchVersion = true; // Explicit Update may acquire the selected sources.
                status = text("update.status");
                start([this, input] { return projectOps::update(input, callback()); });
            }
            ImGui::EndDisabled();
        }
        if (showDetails) {
            ImGui::SeparatorText(text("monitor.console"));
            ImGui::BeginChild("##console", ImVec2(0, 0), ImGuiChildFlags_Borders);
            auto output = logText();
            if (output.empty()) label("common.no_operations"); else wrapped(output);
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::Separator();
        wrapped(status);
        if (button("common.details")) showDetails = !showDetails;
        ImGui::SameLine();
        if (button("common.copy_details")) ImGui::SetClipboardText(logText().c_str());
        ImGui::End();
        imguiEnd();
    }
    void cleanup() override {
        if (cancel) cancel->requestStop();
        if (operation.valid()) operation.wait();
        imguiShutdown();
    }
};
}
int main() {
    WindowSettings settings;
    settings.title = text("app.title");
    settings.width = 960;
    settings.height = 720;
    return runApp<HubApp>(settings);
}
