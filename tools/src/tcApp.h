#pragma once

#include <TrussC.h>
#include <tcxImGui.h>
#include "ProjectGenerator.h"
#include <atomic>
using namespace std;
using namespace tc;
using namespace tcx;

// Project mode
enum class ProjectMode {
    Import,  // Import existing project
    New      // Create new project
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

    // Mouse events (for redraw)
    void mousePressed(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;
    void mouseMoved(Vec2 pos) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseScrolled(Vec2 delta) override;

    // Key events (for redraw)
    void keyPressed(int key) override;
    void keyReleased(int key) override;

    // Drag & drop
    void filesDropped(const vector<string>& files) override;

private:
    // Settings
    string tcRoot;                      // TC_ROOT (path to tc_vX.Y.Z folder)
    string projectName = "myProject";   // Project name
    string projectDir;                  // Save location (parent directory for New mode)
    string importedProjectPath;         // Full path for Import mode
    vector<string> addons;              // Available addons
    vector<int> addonSelected;          // Addon selection state (0/1)
    IdeType ideType = IdeType::VSCode;  // Default is VSCode
    bool generateWebBuild = false;      // Generate Web (Emscripten) build
    bool generateAndroidBuild = false;  // Generate Android build
    bool generateIosBuild = false;      // Generate iOS build
    int webBackend = 0;                 // 0: WebGPU, 1: WebGL

    // Android env check (cached at startup)
    bool androidEnvOk = false;
    string androidEnvTip;

    // Visual Studio versions (Windows only)
    vector<VsVersionInfo> installedVsVersions;
    int selectedVsIndex = 0;

    // UI state
    bool showSetupDialog = false;       // TC_ROOT setup dialog
    string statusMessage;               // Status message
    bool statusIsError = false;
    ProjectMode mode = ProjectMode::New; // Current mode (Import or New)
    bool hasImportedProject = false;    // Whether a project is loaded in Import mode
    string pendingImportPath;           // Deferred import (to avoid crash during InputText edit)

    // Copied popup
    bool showCopiedPopup = false;

    // Generation thread
    atomic<bool> isGenerating{false};   // Generation in progress flag
    string generatingLog;               // Generation log
    mutex logMutex;                     // Log mutex
    void startGenerate();               // Start generation
    void startUpdate();                 // Start update
    void doGenerateProject();           // Generation process run in thread
    void doUpdateProject();             // Update process run in thread

    // Config file
    string configPath;

    // ImGui buffers
    char projectNameBuf[256] = "myProject";
    char projectDirBuf[512] = "";
    char tcRootBuf[512] = "";

    // Helper functions
    void loadConfig();
    void saveConfig();
    void scanAddons();
    void importProject(const string& path);
    string getTemplatePath();
    void setStatus(const string& msg, bool isError = false);
    void resetToNewProject();
    ProjectSettings buildProjectSettings(); // 現在のUI状態からProjectSettingsを構築
};
