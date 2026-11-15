# レシピ

レシピでは、スマートフォンの操作パネル、CLI ツール、常駐アプリを紹介します。
[English](RECIPES.md) · [GET_STARTED_jp.md › はじめに](GET_STARTED_jp.md)

## スマートフォンの操作パネル

レシピ › スマートフォンの操作パネル：信頼できる同じ LAN 上のスマートフォンから、フルスクリーンアプリのモードと値を変えます。[phonePanelExample › README.md](../examples/network/phonePanelExample/README.md) では、ビルド、画面に表示する URL の開き方、HTTP トークンと入力制限を説明しています。サンプルにはページ、メインスレッドへの値の受け渡し、スクロールするログ窓が含まれます。

## CLI ツール

レシピ › CLI ツール：16進カラーを TrussC の正規化 RGB 値に変換して表示し、終了コードを返します。[colorToolExample › README.md](../examples/utils/colorToolExample/README.md) では、`tcApp.cpp` での引数処理と、引数がないときに GUI を開く形を示します。[tools/src/main.cpp](../tools/src/main.cpp) にある `trusscli` の形に倣っています。

## 常駐アプリ

レシピ › 常駐アプリ：バックグラウンドの監視処理などでは、毎秒数回だけ確認し、ファイルにログを残せます。ヘッドレスアプリで `setup()` と `update()` を使います。

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
