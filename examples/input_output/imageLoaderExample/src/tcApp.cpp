#include "tcApp.h"
#include <iostream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

using namespace std;
namespace fs = std::filesystem;

// 実行ファイルのディレクトリを取得
fs::path getExecutableDir() {
#ifdef __APPLE__
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return fs::path(path).parent_path();
    }
#endif
    return fs::current_path();
}

// データパスを取得（macOS .app バンドル対応）
// TODO: tc::dataPath() としてフレームワークに追加
fs::path getDataPath(const fs::path& relativePath) {
    // 実行ファイルの場所から相対パスで探す
    fs::path exeDir = getExecutableDir();

    // 複数のパスを試す
    vector<fs::path> searchPaths = {
        exeDir / "../../../../bin/data",  // .app 内から: Contents/MacOS -> bin/data
        fs::path("../bin/data"),          // build/ から
        fs::path("bin/data"),             // プロジェクトルートから
        fs::path("data"),                 // カレントディレクトリ直下
    };

    for (const auto& base : searchPaths) {
        auto fullPath = base / relativePath;
        if (fs::exists(fullPath)) {
            return fullPath;
        }
    }

    // 見つからなければ最初のパスを返す（エラーになる）
    return searchPaths[0] / relativePath;
}

void tcApp::setup() {
    cout << "imageLoaderExample: Image Loading Demo" << endl;
    cout << "  - Loading multiple images" << endl;
    cout << "  - Drawing with different sizes" << endl;
    cout << "  - Reading pixel colors" << endl;

    // 画像を読み込む
    if (!bikers.load(getDataPath("images/bikers.jpg"))) {
        cout << "Failed to load bikers.jpg" << endl;
    }

    if (!gears.load(getDataPath("images/gears.gif"))) {
        cout << "Failed to load gears.gif" << endl;
    }

    if (!poster.load(getDataPath("images/poster.jpg"))) {
        cout << "Failed to load poster.jpg" << endl;
    }

    if (!transparency.load(getDataPath("images/transparency.png"))) {
        cout << "Failed to load transparency.png" << endl;
    }

    if (!icon.load(getDataPath("images/icon.png"))) {
        cout << "Failed to load icon.png" << endl;
    }
}

void tcApp::update() {
    // 何もしない
}

void tcApp::draw() {
    tc::clear(255);  // 白背景

    tc::setColor(255);  // 白（画像の色をそのまま表示）

    // bikers を左上に描画
    if (bikers.isAllocated()) {
        bikers.draw(0, 0);
    }

    // gears を右上に描画
    if (gears.isAllocated()) {
        gears.draw(600, 0);
    }

    // poster を右下に描画
    if (poster.isAllocated()) {
        poster.draw(600, 300, 200, 300);  // サイズ指定
    }

    // transparency を動かしながら描画
    if (transparency.isAllocated()) {
        float wave = sin(tc::getElapsedTime());
        transparency.draw(500 + wave * 50, 20);
    }

    // icon のピクセルを読んで円を描画
    if (icon.isAllocated()) {
        int w = icon.getWidth();
        int h = icon.getHeight();
        float diameter = 8;
        float offsetX = 20;
        float offsetY = 500;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                tc::Color c = icon.getColor(x, y);
                // 明るさに基づいてサイズを決定
                float brightness = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
                float size = (1.0f - brightness) * diameter / 2;

                tc::setColor(c);
                tc::drawCircle(offsetX + x * diameter, offsetY + y * diameter, size + 1);
            }
        }

        // 元のアイコンも表示
        tc::setColor(255);
        icon.draw(offsetX + w * diameter + 20, offsetY);
    }

    // 情報を表示（半透明背景付き）
    tc::drawBitmapStringHighlight("imageLoaderExample", 10, 20,
        tc::Color(0, 0, 0, 0.7f), tc::Color(1, 1, 1));
    tc::drawBitmapStringHighlight("Loaded images: bikers.jpg, gears.gif, poster.jpg, transparency.png, icon.png", 10, 40,
        tc::Color(0, 0, 0, 0.7f), tc::Color(1, 1, 1));
    tc::drawBitmapStringHighlight("Bottom: pixel colors from icon.png visualized as circles", 10, 60,
        tc::Color(0, 0, 0, 0.7f), tc::Color(1, 1, 1));
}
