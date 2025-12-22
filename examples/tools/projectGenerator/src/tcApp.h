#pragma once

#include "tcBaseApp.h"
#include "tc/utils/tcThread.h"
#include <vector>
#include <string>
#include <atomic>

using namespace tc;
using namespace std;

// IDE selection
enum class IdeType {
    CMakeOnly,
    VSCode,
    Cursor,
    Xcode,
    VisualStudio
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

    // Mouse events (for redraw)
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float deltaX, float deltaY) override;

    // Key events (for redraw)
    void keyPressed(int key) override;
    void keyReleased(int key) override;

    // ドラッグ&ドロップ
    void filesDropped(const vector<string>& files) override;

private:
    // Settings
    string tcRoot;                      // TC_ROOT (path to tc_vX.Y.Z folder)
    string projectName = "myProject";   // Project name
    string projectDir;                  // Save location
    vector<string> addons;              // Available addons
    vector<int> addonSelected;          // Addon selection state (0/1)
    IdeType ideType = IdeType::VSCode;  // Default is VSCode
    bool generateWebBuild = false;      // Generate Web (Emscripten) build

    // UI state
    bool showSetupDialog = false;       // TC_ROOT setup dialog
    string statusMessage;               // Status message
    bool statusIsError = false;
    bool isImportedProject = false;     // Whether this is an imported project
    string importedProjectPath;         // Path of imported project
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
    bool generateProject();
    bool updateProject();
    void importProject(const string& path);
    void generateVSCodeFiles(const string& path);
    void generateXcodeProject(const string& path);
    void generateXcodeSchemes(const string& path);
    void generateVisualStudioProject(const string& path);
    void generateWebBuildFiles(const string& path);
    void openInIde(const string& path);
    string getTemplatePath();
    string getTrusscDirValue(const string& projectPath);
    void setStatus(const string& msg, bool isError = false);
    void resetToNewProject();
};
