// =============================================================================
// tcApp.cpp - ファイルダイアログサンプル
// =============================================================================

#include "tcApp.h"
#include <iostream>

using namespace std;

void tcApp::setup() {
    cout << "=== File Dialog Example ===" << endl;
    cout << "O: Open file dialog" << endl;
    cout << "F: Open folder dialog" << endl;
    cout << "S: Save dialog" << endl;
    cout << "A: Alert dialog" << endl;
    cout << "===========================" << endl;
}

void tcApp::update() {
}

void tcApp::draw() {
    tc::clear(40);

    float y = 40;

    // タイトル
    tc::setColor(255);
    tc::drawBitmapString("File Dialog Example", 40, y);
    y += 30;

    // 操作説明
    tc::setColor(180);
    tc::drawBitmapString("O: Open file   F: Open folder   S: Save   A: Alert", 40, y);
    y += 40;

    // ステータス
    tc::setColor(100, 200, 255);
    tc::drawBitmapString("Status: " + statusMessage, 40, y);
    y += 40;

    // 結果表示
    if (lastResult.success) {
        tc::setColor(100, 255, 100);
        tc::drawBitmapString("Success!", 40, y);
        y += 25;

        tc::setColor(220);
        tc::drawBitmapString("File: " + lastResult.fileName, 40, y);
        y += 20;
        tc::drawBitmapString("Path: " + lastResult.filePath, 40, y);
        y += 40;

        // 画像が読み込まれていれば表示
        if (hasImage && loadedImage.isAllocated()) {
            tc::setColor(255);
            tc::drawBitmapString("Loaded Image:", 40, y);
            y += 25;

            // 画像を適切なサイズで表示
            float maxW = tc::getWindowWidth() - 80;
            float maxH = tc::getWindowHeight() - y - 40;
            float imgW = loadedImage.getWidth();
            float imgH = loadedImage.getHeight();
            float scale = min(maxW / imgW, maxH / imgH);
            if (scale > 1.0f) scale = 1.0f;

            loadedImage.draw(40, y, imgW * scale, imgH * scale);
        }
    } else if (!lastResult.filePath.empty()) {
        tc::setColor(255, 100, 100);
        tc::drawBitmapString("Cancelled", 40, y);
    }
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'O':
        case 'o': {
            // ファイル選択ダイアログ
            statusMessage = "Opening file dialog...";
            lastResult = tc::loadDialog("Select a file");
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "File selected";
                cout << "Selected: " << lastResult.filePath << endl;

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
                        cout << "Image loaded: " << loadedImage.getWidth() << "x" << loadedImage.getHeight() << endl;
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
            lastResult = tc::loadDialog("Select a folder", true);
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "Folder selected";
                cout << "Selected folder: " << lastResult.filePath << endl;
            } else {
                statusMessage = "Folder dialog cancelled";
            }
            break;
        }

        case 'S':
        case 's': {
            // 保存ダイアログ
            statusMessage = "Opening save dialog...";
            lastResult = tc::saveDialog("untitled.txt", "Save your file");
            hasImage = false;

            if (lastResult.success) {
                statusMessage = "Save location selected";
                cout << "Save to: " << lastResult.filePath << endl;
            } else {
                statusMessage = "Save dialog cancelled";
            }
            break;
        }

        case 'A':
        case 'a': {
            // アラートダイアログ
            statusMessage = "Showing alert...";
            tc::alertDialog("This is a test alert from TrussC!");
            statusMessage = "Alert closed";
            break;
        }
    }
}
