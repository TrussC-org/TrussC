#include "TrussC.h"
#include "tcApp.h"
#include <sstream>
#include <algorithm>

// ファイル名を取得（パスの最後の部分）
static string getFileName(const string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos) return path;
    return path.substr(pos + 1);
}

// 拡張子を取得
static string getExtension(const string& path) {
    size_t pos = path.find_last_of('.');
    if (pos == string::npos) return "";
    string ext = path.substr(pos + 1);
    // 小文字に変換
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// 画像拡張子かどうか
static bool isImageExtension(const string& ext) {
    return ext == "png" || ext == "jpg" || ext == "jpeg" ||
           ext == "gif" || ext == "bmp" || ext == "tga";
}

void tcApp::setup() {
    setWindowTitle("dragDropExample");
}

void tcApp::draw() {
    clear(40);

    int w = getWindowWidth();
    int h = getWindowHeight();

    // タイトル
    setColor(1.0f);
    drawBitmapString("=== Drag & Drop Demo ===", 20, 20);

    // ステータスメッセージ
    setColor(0.78f, 0.78f, 0.4f);
    drawBitmapString(statusMessage, 20, 50);

    // ドロップ領域の枠（点線風）
    setColor(0.4f);
    noFill();
    drawRect(10, 70, w - 20, h - 80);
    fill();

    // ドロップされたファイルのリスト表示
    float y = 100;
    int maxDisplay = 10;  // 最大表示数
    int count = 0;

    for (const auto& file : droppedFiles) {
        if (count >= maxDisplay) {
            setColor(0.6f);
            drawBitmapString("... and more", 30, y);
            break;
        }

        // アイコン
        if (file.isImage) {
            setColor(0.4f, 0.78f, 0.4f);  // 画像は緑
            drawBitmapString("[IMG]", 30, y);
        } else {
            setColor(0.4f, 0.6f, 0.78f);  // その他は青
            drawBitmapString("[FILE]", 30, y);
        }

        // ファイル名
        setColor(1.0f);
        drawBitmapString(file.name, 90, y);

        y += 20;
        count++;
    }

    // 画像プレビュー
    if (hasPreview && previewImage.isAllocated()) {
        float previewX = w - 250;
        float previewY = 100;
        float maxSize = 200;

        float imgW = previewImage.getWidth();
        float imgH = previewImage.getHeight();
        float scale = std::min(maxSize / imgW, maxSize / imgH);

        float drawW = imgW * scale;
        float drawH = imgH * scale;

        // 枠
        setColor(0.3f);
        drawRect(previewX - 5, previewY - 5, drawW + 10, drawH + 10);

        // 画像
        setColor(1.0f);
        previewImage.draw(previewX, previewY, drawW, drawH);

        // サイズ情報
        setColor(0.6f);
        stringstream ss;
        ss << (int)imgW << " x " << (int)imgH;
        drawBitmapString(ss.str(), previewX, previewY + drawH + 15);
    }

    // 操作説明
    setColor(0.47f);
    drawBitmapString("Drag and drop files onto this window", 20, h - 25);
}

void tcApp::filesDropped(const vector<string>& files) {
    // リストをクリア
    droppedFiles.clear();
    hasPreview = false;

    // ファイル情報を収集
    for (const auto& path : files) {
        DroppedFile df;
        df.path = path;
        df.name = getFileName(path);
        df.extension = getExtension(path);
        df.isImage = isImageExtension(df.extension);

        droppedFiles.push_back(df);

        // 画像ならプレビュー用に読み込み（最後の画像）
        if (df.isImage) {
            if (previewImage.load(path)) {
                hasPreview = true;
            }
        }
    }

    // ステータス更新
    stringstream ss;
    ss << files.size() << " file(s) dropped";
    statusMessage = ss.str();
}
