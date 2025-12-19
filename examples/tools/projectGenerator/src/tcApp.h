#pragma once

#include "tcBaseApp.h"
#include "tc/utils/tcThread.h"
#include <vector>
#include <string>
#include <atomic>

using namespace tc;
using namespace std;

// IDE 選択
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

    // マウスイベント（redraw 用）
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;

    // キーイベント（redraw 用）
    void keyPressed(int key) override;
    void keyReleased(int key) override;

private:
    // 設定
    string tcRoot;                      // TC_ROOT (tc_vX.Y.Z フォルダへのパス)
    string projectName = "myProject";   // プロジェクト名
    string projectDir;                  // 保存先
    vector<string> addons;              // 利用可能なアドオン
    vector<int> addonSelected;          // アドオン選択状態 (0/1)
    IdeType ideType = IdeType::VSCode;  // デフォルトは VSCode

    // UI 状態
    bool showSetupDialog = false;       // TC_ROOT 設定ダイアログ
    string statusMessage;               // ステータスメッセージ
    bool statusIsError = false;
    bool isImportedProject = false;     // インポートされたプロジェクトかどうか
    string importedProjectPath;         // インポートされたプロジェクトのパス

    // 生成スレッド
    atomic<bool> isGenerating{false};   // 生成中フラグ
    string generatingLog;               // 生成ログ
    mutex logMutex;                     // ログ用ミューテックス
    void startGenerate();               // 生成開始
    void startUpdate();                 // 更新開始
    void doGenerateProject();           // スレッドで実行される生成処理
    void doUpdateProject();             // スレッドで実行される更新処理

    // 設定ファイル
    string configPath;

    // ImGui 用バッファ
    char projectNameBuf[256] = "myProject";
    char projectDirBuf[512] = "";
    char tcRootBuf[512] = "";

    // ヘルパー関数
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
    void openInIde(const string& path);
    string getTemplatePath();
    void setStatus(const string& msg, bool isError = false);
    void resetToNewProject();
};
