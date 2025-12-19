// =============================================================================
// tcApp.cpp - ファイルダイアログサンプル
// =============================================================================

#include "tcApp.h"

using namespace std;

void tcApp::setup() {
    tcLogNotice() << "=== File Dialog Example ===";
    tcLogNotice() << "O: Open file dialog";
    tcLogNotice() << "F: Open folder dialog";
    tcLogNotice() << "S: Save dialog";
    tcLogNotice() << "A: Alert dialog";
    tcLogNotice() << "===========================";
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(40);

    float y = 40;

    // タイトル
    setColor(255);
    drawBitmapString("File Dialog Example", 40, y);
    y += 30;

    // 操作説明
    setColor(180);
    drawBitmapString("O: Open file   F: Open folder   S: Save   A: Alert", 40, y);
    y += 40;

    // ステータス
    setColor(100, 200, 255);
    drawBitmapString("Status: " + statusMessage, 40, y);
    y += 40;

    // 結果表示
    if (lastResult.success) {
        setColor(100, 255, 100);
        drawBitmapString("Success!", 40, y);
        y += 25;

        setColor(220);
        drawBitmapString("File: " + lastResult.fileName, 40, y);
        y += 20;
        drawBitmapString("Path: " + lastResult.filePath, 40, y);
        y += 40;

        // 画像が読み込まれていれば表示
        if (hasImage && loadedImage.isAllocated()) {
            setColor(255);
            drawBitmapString("Loaded Image:", 40, y);
            y += 25;

            // 画像を適切なサイズで表示
            float maxW = getWindowWidth() - 80;
            float maxH = getWindowHeight() - y - 40;
            float imgW = loadedImage.getWidth();
            float imgH = loadedImage.getHeight();
            float scale = std::min<float>(maxW / imgW, maxH / imgH);
            if (scale > 1.0f) scale = 1.0f;

            loadedImage.draw(40, y, imgW * scale, imgH * scale);
        }
    } else if (!lastResult.filePath.empty()) {
        setColor(255, 100, 100);
        drawBitmapString("Cancelled", 40, y);
    }
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'O':
        case 'o': {
            // ファイル選択ダイアログ
            statusMessage = "Opening file dialog...";
            lastResult = loadDialog("Select a file");
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "File selected";
                tcLogNotice() << "Selected: " << lastResult.filePath;

                // 画像ファイルなら読み込みを試行
                string path = lastResult.filePath;
                if (path.find(".png") != string::npos ||
                    path.find(".jpg") != string::npos ||
                    path.find(".jpeg") != string::npos ||
                    path.find(".PNG") != string::npos ||
                    path.find(".JPG") != string::npos ||
                    path.find(".JPEG") != string::npos) {
                    if (loadedImage.load(path)) {
                        hasImage = true;
                        tcLogNotice() << "Image loaded: " << loadedImage.getWidth() << "x" << loadedImage.getHeight();
                    }
                }
            } else {
                statusMessage = "File dialog cancelled";
            }
            break;
        }

        case 'F':
        case 'f': {
            // フォルダ選択ダイアログ
            statusMessage = "Opening folder dialog...";
            lastResult = loadDialog("Select a folder", true);
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "Folder selected";
                tcLogNotice() << "Selected folder: " << lastResult.filePath;
            } else {
                statusMessage = "Folder dialog cancelled";
            }
            break;
        }

        case 'S':
        case 's': {
            // 保存ダイアログ
            statusMessage = "Opening save dialog...";
            lastResult = saveDialog("untitled.txt", "Save your file");
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "Save location selected";
                tcLogNotice() << "Save to: " << lastResult.filePath;
            } else {
                statusMessage = "Save dialog cancelled";
            }
            break;
        }

        case 'A':
        case 'a': {
            // アラートダイアログ
            statusMessage = "Showing alert...";
            alertDialog("This is a test alert from TrussC!");
            statusMessage = "Alert closed";
            break;
        }
    }
}
