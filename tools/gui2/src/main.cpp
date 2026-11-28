#include <TrussC.h>
#include <tcxImGui.h>
#include "ProjectHub.h"
#include "GeneratedFile.h"
#include "Strings.h"
#include "Card.h"
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
using gui2::theme;
using gui2::beginCard;
using gui2::endCard;
using gui2::sectionHeader;

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
bool button(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_Border, theme().buttonBorder);
    const bool clicked = ImGui::Button(text(id));
    ImGui::PopStyleColor();
    return clicked;
}
void label(const char* id) { ImGui::TextUnformatted(text(id)); }
bool primaryButton(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24, 12));
    const auto& t = theme();
    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accentHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, t.textOnAccent);
    ImGui::PushStyleColor(ImGuiCol_Border, t.accentActive);
    bool clicked = ImGui::Button(text(id));
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    return clicked;
}
bool dangerButton(const char* id) {
    const auto& t = theme();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24, 12));
    ImGui::PushStyleColor(ImGuiCol_Button, t.failureStrong);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.failure);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, gui2::darken(t.failureStrong, .8f));
    ImGui::PushStyleColor(ImGuiCol_Text, gui2::dangerText);
    const bool clicked = button(id);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
}
void captionText(const std::string& value) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme().captionScale);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    wrapped(value);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void caption(const char* id) { captionText(text(id)); }

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
    float consoleScrollY = 0, consoleScrollMaxY = 0;
    ImVec2 consoleSize;
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
        consoleScrollMaxY = 0;
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
        if (theme().headerScale == gui2::candy.headerScale) {
            const auto p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y+4), ImVec2(p.x+12, p.y+16), ImGui::GetColorU32(theme().accent), 4);
            ImGui::Dummy(ImVec2(12, 18));
            ImGui::SameLine();
        }
        ImGui::TextColored(theme().accent, "%s", text("app.title"));
        ImGui::SameLine();
        ImGui::TextColored(theme().textDim, "%s", text(home ? "nav.home" : monitor ? "nav.monitor" : "nav.hub"));
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
        beginCard("##project");
        caption("project.current");
        if (project.path.empty()) { label("project.none"); endCard(); return; }
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme().titleScale);
        wrapped(project.name);
        ImGui::PopFont();
        // Header has its own scrolling region; the copy action never scrolls out.
        const auto insetPadding = theme().framePadding;
        const int lines = wrapped(project.path, true, ImGui::GetContentRegionAvail().x - 2 * insetPadding.x - 2 * theme().frameBorder);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float lineSpacing = ImGui::GetStyle().ItemSpacing.y;
        const int visibleLines = (std::min)(lines, (std::max)(1, int((130 - 2 * insetPadding.y + lineSpacing) / lineHeight)));
        const float pathHeight = visibleLines * lineHeight - lineSpacing + 2 * insetPadding.y;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, insetPadding);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarPadding, 4.0f);
        ImGui::BeginChild("##project-path", ImVec2(0, pathHeight), ImGuiChildFlags_FrameStyle);
        const auto pathOrigin = ImGui::GetCursorScreenPos();
        const float textHeight = wrapped(project.path, true) * ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered()) pathSelected = false;
        if (ImGui::Selectable("##select-path", pathSelected, 0, ImVec2(0, textHeight))) {
            pathSelected = true;
            ImGui::SetClipboardText(project.path.c_str());
        }
        ImGui::SetCursorScreenPos(pathOrigin);
        wrapped(project.path);
        ImGui::EndChild();
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
            gui2::warningBadge(text("advanced.missing"));
        }
        endCard();
        if (!home && !monitor) caption("project.drop");
    }
    void homeView() {
        ImGui::Spacing();
        ImGui::BeginDisabled(busy());
        const auto dropBg = gui2::over(theme().accentSoft, theme().window);
        beginCard("##drop", ImGui::GetContentRegionAvail().x < 550 ? 200 : 140, 0, &dropBg, 0);
        const auto origin = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        auto* draw = ImGui::GetWindowDrawList();
        const ImU32 border = ImGui::GetColorU32(gui2::alpha(theme().accent, .6f));
        const float radius = theme().childRounding;
        for (float x = radius; x < size.x - radius; x += 12) {
            draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + (std::min)(x + 6, size.x - radius), origin.y), border);
            draw->AddLine(ImVec2(origin.x + x, origin.y + size.y - 1), ImVec2(origin.x + (std::min)(x + 6, size.x - radius), origin.y + size.y - 1), border);
        }
        for (float y = radius; y < size.y - radius; y += 12) {
            draw->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x, origin.y + (std::min)(y + 6, size.y - radius)), border);
            draw->AddLine(ImVec2(origin.x + size.x - 1, origin.y + y), ImVec2(origin.x + size.x - 1, origin.y + (std::min)(y + 6, size.y - radius)), border);
        }
        const ImVec2 centers[] = {{origin.x+radius, origin.y+radius}, {origin.x+size.x-radius-1, origin.y+radius},
            {origin.x+size.x-radius-1, origin.y+size.y-radius-1}, {origin.x+radius, origin.y+size.y-radius-1}};
        for (int i = 0; i < 4; ++i) {
            draw->PathArcToFast(centers[i], radius, 6+i*3, 9+i*3);
            draw->PathStroke(border, 1.0f);
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
        endCard();
        if (!projectHub::usableFramework(utf8ToPath(root))) {
            wrapped(text("home.root_missing"));
            if (button("home.choose_root")) pick(true);
        }
        ImGui::Spacing();
        beginCard("##recents");
        sectionHeader("home.recents");
        if (recents.empty()) wrapped(text("home.empty"));
        // select() updates recents, so render a stable copy of this list.
        auto rows = recents;
        for (const auto& path : rows) {
            ImGui::PushID(path.c_str());
            if (path != rows.front()) ImGui::Separator();
            const float openWidth = ImGui::CalcTextSize(text("home.recent_open")).x + ImGui::GetStyle().FramePadding.x * 2;
            const float openX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - openWidth;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(pathToUtf8(utf8ToPath(path).filename()).c_str());
            ImGui::SameLine(openX);
            if (button("home.recent_open")) select(utf8ToPath(path));
            ImGui::PushStyleColor(ImGuiCol_Text, theme().textDim);
            wrapped(path);
            ImGui::PopStyleColor();
            if (!fs::is_directory(utf8ToPath(path))) ImGui::TextColored(theme().failure, "%s", text("home.missing"));
            ImGui::Spacing();
            ImGui::PopID();
        }
        endCard();
        ImGui::EndDisabled();
    }
    void hubView() {
        beginCard("##actions");
        ImGui::BeginDisabled(busy());
        const float runY = ImGui::GetCursorPosY();
        if (primaryButton(ImGui::GetIO().KeyShift ? "hub.build" : "hub.run")) run(ImGui::GetIO().KeyShift);
        const float runHeight = ImGui::GetItemRectSize().y;
        const bool wide = ImGui::GetWindowWidth() >= 650;
        if (wide) ImGui::SameLine(0, 12);
        caption("hub.shift");
        if (wide) ImGui::SameLine(0, 12);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme().captionScale);
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
        endCard();
    }
    void stop() {
        if (cancel) {
            cancel->requestStop();
            status = text("monitor.stopping");
        }
    }
    void console() {
        beginCard("##console-card", ImGui::GetContentRegionAvail().y);
        sectionHeader("monitor.console");
        const bool resumed = ImGui::Checkbox(text("monitor.follow"), &followLatest) && followLatest;
        ImGui::SameLine();
        if (button("common.copy_details")) ImGui::SetClipboardText(logText().c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme().console);
        ImGui::PushStyleColor(ImGuiCol_Text, theme().consoleText);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, theme().cardBorder);
        ImGui::BeginChild("##console", ImVec2(0, 0), ImGuiChildFlags_FrameStyle);
        auto output = logText();
        if (output.empty()) label("common.no_operations"); else wrapped(output);
        const float scrollY = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        const ImVec2 size = ImGui::GetWindowSize();
        // Position changes cover wheel, scrollbar and keyboard navigation. New
        // output can increase the bottom without moving the current position.
        // A resize rewraps the text and clamps the position, which is not a user scroll.
        const bool resized = size.x != consoleSize.x || size.y != consoleSize.y
            || scrollMaxY < consoleScrollMaxY - 0.5f;
        if (!resumed && !resized && scrollY < consoleScrollY - 0.5f) followLatest = false;
        if (followLatest && (output.size() != consoleBytes || ImGui::GetScrollY() < ImGui::GetScrollMaxY()))
            ImGui::SetScrollHereY(1.0f);
        consoleBytes = output.size();
        consoleScrollY = scrollY;
        consoleScrollMaxY = scrollMaxY;
        consoleSize = size;
        ImGui::EndChild();
        ImGui::PopStyleColor(3);
        endCard(false);
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
            + ImGui::GetFrameHeightWithSpacing();
        const float summaryHeight = (std::max)(100.0f, ImGui::GetContentRegionAvail().y - consoleHeight);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(ImGui::GetContentRegionAvail().x, summaryHeight));
        beginCard("##operation-summary");
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme().titleScale);
        if (failed) ImGui::PushStyleColor(ImGuiCol_Text, theme().failure);
        wrapped(status);
        if (failed) ImGui::PopStyleColor();
        ImGui::PopFont();
        if (operation.valid() && cancel && current == projectOps::OperationPhase::Building && !cancel->stopRequested()) {
            if (progress.totalFiles > 0) {
                std::string counts = text("monitor.progress");
                counts.replace(counts.find("{compiledFiles}"), 15, std::to_string(progress.compiledFiles));
                counts.replace(counts.find("{totalFiles}"), 12, std::to_string(progress.totalFiles));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                ImGui::ProgressBar((std::min)(1.0f, float(progress.compiledFiles) / float(progress.totalFiles)), ImVec2(-1, 10), "");
                ImGui::PopStyleVar();
                captionText(counts);
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
                if (dangerButton("monitor.force")) cancel->forceStop();
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
            ImGui::PushStyleColor(ImGuiCol_ChildBg, theme().failureSoft);
            ImGui::PushStyleColor(ImGuiCol_Border, theme().failure);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme().frameRounding);
            ImGui::BeginChild("##failure", ImVec2(0, height), ImGuiChildFlags_Borders);
            const auto p = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x+2, p.y+2), ImVec2(p.x+6, p.y+height-2), ImGui::GetColorU32(theme().failure));
            wrapped(diagnostic);
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            recovery();
        }
        endCard();
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
        gui2::applyTheme(ImGui::GetStyle(), theme());
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
        const auto bg = theme().window;
        clear(bg.x, bg.y, bg.z, bg.w);
        if (!imguiBegin()) return;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("##hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        card();
        ImGui::BeginChild("##content", ImVec2(0, home || monitor ? 0 : -90));
        if (home) homeView();
        else if (monitor) monitorView();
        else hubView();
        if (!monitor && failed) recovery();
        if (!monitor && showDetails) console();
        ImGui::EndChild();
        if (!home && !monitor) {
            const auto stateBg = failed ? theme().failureSoft :
                (status == text("hub.built") || status == text("monitor.finished")) ? theme().successSoft : theme().card;
            beginCard("##status", 0, 0, &stateBg, -1, failed ? &theme().failure : nullptr);
            wrapped(status);
            if (button("common.details")) showDetails = !showDetails;
            ImGui::SameLine();
            if (button("common.copy_details")) ImGui::SetClipboardText(logText().c_str());
            endCard();
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
