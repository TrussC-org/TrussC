# レシピ

ここで紹介するのは基本ではなく、TrussC の応用的な使い方です（スマートフォンの操作パネル、CLI ツール、常駐アプリ）。基本は `examples/` のサンプルと [GET_STARTED_jp.md › サンプルを実行する](GET_STARTED_jp.md#5-サンプルを実行する) から始めてください。
[English](RECIPES.md) · [GET_STARTED_jp.md › はじめに](GET_STARTED_jp.md)

## スマートフォンから操作するパネル

信頼できる同じ LAN のスマートフォンから、フルスクリーンのアプリの色と明るさを操作できます。[phonePanelExample › README.md](../examples/network/phonePanelExample/README.md) では、ビルド、画面に表示する URL の開き方、HTTP トークンと入力制限を説明しています。サンプルにはページ、メインスレッドへの値の受け渡し、スクロールするログ窓が含まれます。

## CLI ツール

CLI ツールは16進カラーを TrussC の正規化 RGB 値に変換して表示し、終了コードを返します。[colorToolExample › README.md](../examples/utils/colorToolExample/README.md) では、`tcApp.cpp` での引数処理と、引数がないときに GUI を開く形を示します。[tools/src/main.cpp](../tools/src/main.cpp) にある `trusscli` の形に倣っています。

## 常駐アプリ

常駐アプリはバックグラウンドで監視処理などを行い、毎秒数回だけ確認し、ファイルにログを残せます。ヘッドレスアプリで `setup()` と `update()` を使います。

```cpp
#include <TrussC.h>
using namespace tc;

class tcApp : public App {
public:
    void setup() override {
        if (!setLogFile("watcher.log")) {
            logError("watcher") << "Could not open log file";
            exitApp();
        }
    }
    void update() override {
        // Check for work here.
    }
};

int main() {
    HeadlessSettings settings;
    settings.setFps(2);
    return runHeadlessApp<tcApp>(settings);
}
```
