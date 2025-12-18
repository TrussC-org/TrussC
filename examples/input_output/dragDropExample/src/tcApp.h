#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <vector>
#include <string>

using namespace trussc;
using namespace std;

// dragDropExample - ドラッグ&ドロップのデモ
// ファイルをウィンドウにドロップすると、ファイル情報を表示
// 画像ファイルの場合はプレビューも表示

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

    void filesDropped(const vector<string>& files) override;

private:
    // ドロップされたファイル情報
    struct DroppedFile {
        string path;
        string name;
        string extension;
        bool isImage;
    };
    vector<DroppedFile> droppedFiles;

    // プレビュー用画像（最新1枚のみ）
    Image previewImage;
    bool hasPreview = false;

    // メッセージ表示用
    string statusMessage = "Drop files here!";
};
