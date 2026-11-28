#include <TrussC.h>
#include <tcxImGui.h>
#include "ProjectHub.h"
#include "GeneratedFile.h"
#include "Strings.h"
#include <algorithm>
#include <fstream>
#include <future>
#include <chrono>
#include <optional>
#include <mutex>
#include <memory>
#include <cstdlib>
#include <utility>

using namespace tc;
using namespace tcx;
namespace fs = std::filesystem;
using gui2::text;

namespace {
// Wrap at words; split a UTF-8 token only when it exceeds the width.
int wrapped(const std::string& value, bool measureOnly = false, float availableWidth = 0) {
    const float width = (std::max)(1.0f, availableWidth > 0 ? availableWidth : ImGui::GetContentRegionAvail().x);
    int lines = 0;
    const char* start = value.data();
    const char* end = start + value.size();
    while (start < end) {
        const char* lineEnd = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), start, end, width);
        if (lineEnd == start && *start != '\n') {
            ++lineEnd;
            while (lineEnd < end && (static_cast<unsigned char>(*lineEnd) & 0xc0) == 0x80) ++lineEnd;
        }
        ++lines;
        if (!measureOnly) ImGui::TextUnformatted(start, lineEnd);
        start = lineEnd;
        while (start < end && (*start == ' ' || *start == '\t')) ++start;
        if (start < end && *start == '\n') ++start;
    }
    return lines;
}
bool button(const char* id) { return ImGui::Button(text(id)); }
void label(const char* id) { ImGui::TextUnformatted(text(id)); }
bool primaryButton(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24, 12));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.40f, 0.60f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.49f, 0.71f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.32f, 0.49f, 1));
    bool clicked = button(id);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return clicked;
}
void caption(const char* id) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    wrapped(text(id));
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

class HubApp : public App {
    projectHub::Snapshot project;
    Json preferences = Json::object();
    std::vector<std::string> recents;
    std::string root, details, status = text("hub.ready");
    bool home = true, mcp = true, showDetails = false, failed = false, dialogOpen = false;
    bool pathSelected = false, monitor = false, followLatest = true, closing = false;
    EventListener exitListener;
    BuildOutputFilter::Progress buildProgress;
    projectOps::OperationPhase phase = projectOps::OperationPhase::Building;
    std::optional<std::chrono::steady_clock::time_point> runStarted;
    double runSeconds = 0;
    bool ranApp = false, forceDeclined = false;
    size_t consoleBytes = 0;
    float consoleScrollY = 0;
    std::mutex logMutex;
    std::unique_ptr<projectOps::CancelHandle> cancel;
    std::future<projectOps::Result> operation; // Join before releasing its handle/log state.
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
        monitor = false;
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
        forceDeclined = false;
        showDetails = true;
        monitor = true;
        home = false;
        followLatest = true;
        consoleBytes = 0;
        consoleScrollY = 0;
        {
            std::lock_guard<std::mutex> lock(logMutex);
            details.clear();
            buildProgress = {};
            phase = projectOps::OperationPhase::Building;
            runStarted.reset();
            runSeconds = 0;
            ranApp = false;
        }
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
        input.build.progress = [this](const BuildOutputFilter::Progress& progress) {
            std::lock_guard<std::mutex> lock(logMutex);
            buildProgress = progress;
        };
        input.build.phase = [this](projectOps::OperationPhase next) {
            std::lock_guard<std::mutex> lock(logMutex);
            phase = next;
            if (next == projectOps::OperationPhase::Running) {
                runStarted = std::chrono::steady_clock::now();
                ranApp = true;
            }
        };
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
        label(home ? "nav.home" : monitor ? "nav.monitor" : "nav.hub");
        if (!project.path.empty()) {
            const float changeWidth = ImGui::CalcTextSize(text("project.change")).x + ImGui::GetStyle().FramePadding.x * 2;
            if (ImGui::GetWindowWidth() >= 600)
                ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - changeWidth));
            ImGui::BeginDisabled(busy());
            if (button("project.change")) home = true;
            ImGui::EndDisabled();
            if (busy() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", text("project.busy"));
        }
        if (!home && !monitor) caption("project.drop");
        ImGui::Separator();
        caption("project.current");
        if (project.path.empty()) { label("project.none"); return; }
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
        wrapped(project.name);
        ImGui::PopFont();
        // Header has its own scrolling region; the copy action never scrolls out.
        const int lines = wrapped(project.path, true, ImGui::GetContentRegionAvail().x - 24);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float lineSpacing = ImGui::GetStyle().ItemSpacing.y;
        const int visibleLines = (std::min)(lines, (std::max)(1, int((130 - 14 + lineSpacing) / lineHeight)));
        const float pathHeight = visibleLines * lineHeight - lineSpacing + 14;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 7));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarPadding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##project-path", ImVec2(0, pathHeight), ImGuiChildFlags_Borders);
        const auto pathOrigin = ImGui::GetCursorScreenPos();
        const float textHeight = wrapped(project.path, true) * ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered()) pathSelected = false;
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.6f, 0.8f, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.6f, 0.8f, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.6f, 0.8f, 0.20f));
        if (ImGui::Selectable("##select-path", pathSelected, 0, ImVec2(0, textHeight))) {
            pathSelected = true;
            ImGui::SetClipboardText(project.path.c_str());
        }
        ImGui::PopStyleColor(3);
        ImGui::SetCursorScreenPos(pathOrigin);
        wrapped(project.path);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (button("project.copy_path")) ImGui::SetClipboardText(project.path.c_str());
        std::string summary = text("project.summary");
        auto replace = [&](const std::string& key, const std::string& value) { summary.replace(summary.find(key), key.size(), value); };
        replace("{version}", project.commit.empty() ? text("project.local") : std::string(text("app.title")) + " " + project.commit.substr(0, 7));
        replace("{source}", text(!project.localRoot.empty() ? "advanced.override" : !project.pin.empty() ? "source.pinned" : "advanced.fallback"));
        replace("{count}", std::to_string(project.addons.size()));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        wrapped(summary);
        ImGui::PopStyleColor();
        if (!project.missing.empty()) {
            label("advanced.cache");
            ImGui::SameLine();
            label("advanced.missing");
        }
    }
    void homeView() {
        ImGui::Spacing();
        ImGui::BeginDisabled(busy());
        ImGui::BeginChild("##drop", ImVec2(0, ImGui::GetContentRegionAvail().x < 550 ? 200 : 140));
        const auto origin = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        auto* draw = ImGui::GetWindowDrawList();
        const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
        for (float x = 0; x < size.x; x += 12) {
            draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + (std::min)(x + 6, size.x), origin.y), border);
            draw->AddLine(ImVec2(origin.x + x, origin.y + size.y - 1), ImVec2(origin.x + (std::min)(x + 6, size.x), origin.y + size.y - 1), border);
        }
        for (float y = 0; y < size.y; y += 12) {
            draw->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x, origin.y + (std::min)(y + 6, size.y)), border);
            draw->AddLine(ImVec2(origin.x + size.x - 1, origin.y + y), ImVec2(origin.x + size.x - 1, origin.y + (std::min)(y + 6, size.y)), border);
        }
        ImGui::SetCursorPosY(25);
        ImGui::SetCursorPosX((std::max)(0.0f, (size.x - ImGui::CalcTextSize(text("project.drop")).x) / 2));
        label("project.drop");
        const float actionsWidth = ImGui::CalcTextSize(text("home.open")).x + ImGui::CalcTextSize(text("home.new")).x + 96 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((std::max)(0.0f, (size.x - (size.x >= 550 ? actionsWidth : ImGui::CalcTextSize(text("home.open")).x + 48)) / 2));
        if (primaryButton("home.open")) pick(false);
        if (size.x >= 550) ImGui::SameLine();
        else ImGui::SetCursorPosX((std::max)(0.0f, (size.x - ImGui::CalcTextSize(text("home.new")).x - 48) / 2));
        if (primaryButton("home.new")) newProject();
        ImGui::EndChild();
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
            ImGui::BeginChild("##recent", ImVec2(0, 105), ImGuiChildFlags_Borders);
            const float openWidth = ImGui::CalcTextSize(text("home.recent_open")).x + ImGui::GetStyle().FramePadding.x * 2;
            const float openX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - openWidth;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(pathToUtf8(utf8ToPath(path).filename()).c_str());
            ImGui::SameLine(openX);
            if (button("home.recent_open")) select(utf8ToPath(path));
            wrapped(path);
            if (!fs::is_directory(utf8ToPath(path))) label("home.missing");
            ImGui::EndChild();
            ImGui::Spacing();
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }
    void hubView() {
        ImGui::BeginDisabled(busy());
        const float runY = ImGui::GetCursorPosY();
        if (primaryButton(ImGui::GetIO().KeyShift ? "hub.build" : "hub.run")) run(ImGui::GetIO().KeyShift);
        const float runHeight = ImGui::GetItemRectSize().y;
        const bool wide = ImGui::GetWindowWidth() >= 650;
        if (wide) ImGui::SameLine(0, 12);
        caption("hub.shift");
        if (wide) ImGui::SameLine(0, 12);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
        if (wide) ImGui::SetCursorPosY(runY + (runHeight - ImGui::GetFrameHeight()) * 0.5f);
        if (ImGui::Checkbox(text("hub.mcp"), &mcp)) {
            preferences["monitor"][project.path] = mcp;
            savePreferences();
        }
        ImGui::PopFont();
        ImGui::PopStyleVar();
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
        caption("hub.app_hint");
    }
    void stop() {
        if (cancel) {
            cancel->requestStop();
            status = text("monitor.stopping");
        }
    }
    void console() {
        ImGui::SeparatorText(text("monitor.console"));
        const bool resumed = ImGui::Checkbox(text("monitor.follow"), &followLatest) && followLatest;
        ImGui::SameLine();
        if (button("common.copy_details")) ImGui::SetClipboardText(logText().c_str());
        ImGui::BeginChild("##console", ImVec2(0, (std::max)(160.0f, ImGui::GetContentRegionAvail().y)), ImGuiChildFlags_Borders);
        auto output = logText();
        if (output.empty()) label("common.no_operations"); else wrapped(output);
        const float scrollY = ImGui::GetScrollY();
        // Position changes cover wheel, scrollbar and keyboard navigation. New
        // output can increase the bottom without moving the current position.
        if (!resumed && scrollY < consoleScrollY - 0.5f) followLatest = false;
        if (followLatest && (output.size() != consoleBytes || ImGui::GetScrollY() < ImGui::GetScrollMaxY()))
            ImGui::SetScrollHereY(1.0f);
        consoleBytes = output.size();
        consoleScrollY = scrollY;
        ImGui::EndChild();
    }
    void recovery() {
        wrapped(text("common.recovery"));
        ImGui::BeginDisabled(busy() || project.path.empty());
        if (button("common.update")) {
            useFallback();
            auto input = updateInput();
            input.fetchVersion = true;
            status = text("update.status");
            start([this, input] { return projectOps::update(input, callback()); });
        }
        ImGui::EndDisabled();
        caption("advanced.update_hint");
    }
    void monitorView() {
        BuildOutputFilter::Progress progress;
        projectOps::OperationPhase current;
        std::optional<std::chrono::steady_clock::time_point> started;
        {
            std::lock_guard<std::mutex> lock(logMutex);
            progress = buildProgress;
            current = phase;
            started = runStarted;
        }
        // Keep the console visible while long diagnostics/recovery controls scroll.
        const float consoleHeight = 160 + ImGui::GetTextLineHeightWithSpacing()
            + ImGui::GetFrameHeightWithSpacing() + 2 * ImGui::GetStyle().ItemSpacing.y;
        const float summaryHeight = (std::max)(100.0f, ImGui::GetContentRegionAvail().y - consoleHeight);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(ImGui::GetContentRegionAvail().x, summaryHeight));
        ImGui::BeginChild("##operation-summary", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        const ImVec4 failureRed(0.78f, 0.22f, 0.22f, 1);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
        if (failed) ImGui::PushStyleColor(ImGuiCol_Text, failureRed);
        wrapped(status);
        if (failed) ImGui::PopStyleColor();
        ImGui::PopFont();
        if (operation.valid() && cancel && current == projectOps::OperationPhase::Building && !cancel->stopRequested()) {
            if (progress.totalFiles > 0) {
                std::string counts = text("monitor.progress");
                counts.replace(counts.find("{compiledFiles}"), 15, std::to_string(progress.compiledFiles));
                counts.replace(counts.find("{totalFiles}"), 12, std::to_string(progress.totalFiles));
                ImGui::ProgressBar((std::min)(1.0f, float(progress.compiledFiles) / float(progress.totalFiles)), ImVec2(-1, 0), counts.c_str());
            } else {
                // Unknown totals stay indeterminate; never infer counts from screen dots.
                const char* frames[] = {"|", "/", "-", "\\"};
                ImGui::TextUnformatted(frames[int(ImGui::GetTime() * 5) % 4]);
            }
        }
        if (operation.valid()) {
            if (current == projectOps::OperationPhase::Running && started && cancel && !cancel->stopRequested()) {
                std::string elapsed = text("monitor.running_time");
                elapsed.replace(elapsed.find("{elapsed}"), 9,
                    formatElapsed(std::chrono::duration<double>(std::chrono::steady_clock::now() - *started).count()));
                wrapped(elapsed);
                wrapped(text("monitor.app"));
            }
            if (!forceDeclined) {
                ImGui::BeginDisabled(!cancel || cancel->stopRequested());
                if (button("monitor.stop")) stop();
                ImGui::EndDisabled();
            }
            if (!cancel) wrapped(text("update.no_cancel"));
            if (cancel && cancel->state() == projectOps::ProcessState::StillRunning) {
                if (!forceDeclined) {
                    wrapped(text("monitor.force_prompt"));
                    if (button("monitor.wait")) {
                        forceDeclined = true;
                        status = text("monitor.waiting");
                    }
                    ImGui::SameLine();
                }
                ImGui::PushStyleColor(ImGuiCol_Button, failureRed);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.28f, 0.28f, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.63f, 0.15f, 0.15f, 1));
                if (button("monitor.force")) cancel->forceStop();
                ImGui::PopStyleColor(3);
            }
        } else {
            if (ranApp) {
                std::string summary = text("monitor.run_time");
                summary += formatElapsed(runSeconds);
                wrapped(summary);
            }
            if (button("monitor.back")) {
                monitor = false;
                home = project.path.empty();
            }
        }
        if (failed) {
            const auto& diagnostic = progress.firstError.empty() ? failureMessage : progress.firstError;
            const int lines = wrapped(diagnostic, true, ImGui::GetContentRegionAvail().x - 24);
            const float height = (std::min)(5, (std::max)(1, lines)) * ImGui::GetTextLineHeightWithSpacing()
                - ImGui::GetStyle().ItemSpacing.y + 14;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 7));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, failureRed);
            ImGui::BeginChild("##failure", ImVec2(0, height), ImGuiChildFlags_Borders);
            wrapped(diagnostic);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            recovery();
        }
        ImGui::EndChild();
        console();
    }
    std::string failureMessage;

public:
    void setup() override {
        imguiSetup();
        exitListener = events().exitRequested.listen([this](ExitRequestEventArgs& args) {
            if (!busy()) return;
            args.cancel = true;
            closing = true;
            monitor = true;
            home = false;
            stop();
            if (!cancel) log(text("update.no_cancel"));
        });
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
        style.FrameBorderSize = 1;
        style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.21f, 0.23f, 1);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.29f, 0.31f, 1);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.17f, 0.19f, 1);
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.16f, 0.40f, 0.60f, 1);
        style.Colors[ImGuiCol_Border] = ImVec4(0.50f, 0.52f, 0.55f, 0.35f);
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
        if (auto* window = getWindow(); window && window->getWidth() < 360)
            window->setSize(360, window->getHeight());
        if (operation.valid() && cancel && !cancel->stopRequested()) {
            std::lock_guard<std::mutex> lock(logMutex);
            switch (phase) {
                case projectOps::OperationPhase::Configuring: status = text("monitor.configuring"); break;
                case projectOps::OperationPhase::Building: status = text("monitor.building"); break;
                case projectOps::OperationPhase::Starting: status = text("monitor.starting"); break;
                case projectOps::OperationPhase::Running: status = text("monitor.running"); break;
            }
        }
        if (operation.valid() && operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = operation.get();
            {
                std::lock_guard<std::mutex> lock(logMutex);
                if (runStarted) runSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - *runStarted).count();
            }
            log(std::string(text("monitor.exit_code")) + std::to_string(result.exitCode));
            failureMessage = result.failureReason;
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
        if (closing && !busy()) exitApp();
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
        ImGui::BeginChild("##content", ImVec2(0, home || monitor ? 0 : -90));
        if (home) homeView();
        else if (monitor) monitorView();
        else hubView();
        if (!monitor && failed) recovery();
        if (!monitor && showDetails) console();
        ImGui::EndChild();
        if (!home && !monitor) {
            ImGui::Separator();
            wrapped(status);
            if (button("common.details")) showDetails = !showDetails;
            ImGui::SameLine();
            if (button("common.copy_details")) ImGui::SetClipboardText(logText().c_str());
        }
        ImGui::End();
        imguiEnd();
    }
    void cleanup() override {
        // Normal close is vetoed until update() consumes the completed future.
        exitListener.disconnect();
        imguiShutdown();
    }
};
}
int main() {
    WindowSettings settings;
    settings.title = text("app.title");
    settings.width = 400;
    settings.height = 720;
    return runApp<HubApp>(settings);
}
