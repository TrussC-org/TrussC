# tcxInputReplay

[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

TrussCの1つのウィンドウへの入力を記録し、コード変更後に同じ操作をリアルタイムで再生するデバッグ用アドオンです。場面のタイミングが変わっても、markでアプリの到着を待てます。入力だけではアプリの状態を決定論的に再現できません。状態全体のスナップショットAPIはありません。

`addons.make`に`tcxInputReplay`を追加し、`<tcxInputReplay.h>`をincludeします。すべてのAPIはメインスレッドで呼びます。`tcApp::setup()`で一度`setup()`を呼ぶと、そのウィンドウを選び、アプリの終了処理でスコープ付きリスナーを解放します。

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
}
```

## 入力の記録と再生

パスは`getDataPath()`で解決します。保存先を渡して記録を開始し、停止するとendレコードを書きます。

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'R') tcx::inputreplay::startRecording("session.jsonl");
    if (key == 'S') tcx::inputreplay::stopRecording();
}
```

元の座標で再生するか、スケーリング方式を選びます。

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'P') tcx::inputreplay::play("session.jsonl", tcx::inputreplay::ScaleMode::Fit);
}
```

- `play(path)`の既定値は`ScaleMode::None`です。ウィンドウサイズが異なるとWarningを出します。
- `ScaleMode::Stretch`は、記録時と現在のサイズからxとyを別々に拡大縮小します。
- `ScaleMode::Fit`は縦横比を保ち、記録したキャンバスを中央に配置します。記録したキャンバスの外の座標は、余白へ移さず元の値を通します。
- 通常は論理ポイント、pixelPerfectではフレームバッファのピクセルを使います。リサイズは次の入力から反映します。スクロール量は拡大縮小しません。
- DPIやpixelPerfectの設定が異なる場合もWarningを出します。アドオンにはDPIやpixelPerfectを変更するAPIはありません。起動前に同じ設定にしてください。`None`は記録した座標値をそのまま使います。

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'P') tcx::inputreplay::play("session.jsonl", tcx::inputreplay::ScaleMode::None);
}
```

マウス移動、ドラッグ（移動として1回）、ボタン、スクロール、リピートを含むキー、文字、修飾キー、タッチのID・キャンセル・筆圧、ファイルドロップのパスを記録します。注入入力は`injected`と`tag`を保持し、このアドオン自身の同期的な再生入力は除外します。タッチから合成したマウス入力は、タッチの再注入で作られるため記録しません。ImGuiが型付き入力を消費する前に記録します。

ファイルドロップはパスだけを保存し、ファイル自体は再作成しません。アドオンにファイルの復元APIはありません。coreのドロップ引数には出どころを取得するAPIがないため、記録では`injected: false`と空のtagになります。同期的な再生ガードにより再記録は防ぎます。スクロール位置はデータとして保存しますが、注入APIは再生先の現在のポインター位置を使います。スクロール位置を直接渡すAPIはありません。

## markで場面を同期する

記録時も再生時も、各場面に到着したところで同じmarkを呼びます。

```cpp
void tcApp::enterScene() {
    tcx::inputreplay::mark("scene2");
}
```

再生は次の記録済みmarkで、同名のアプリmarkが来るまで無期限に待ちます。早く来た場合は前の場面の残りの入力を捨て、遅く来た場合は待機を解除します。どちらもそのmarkを時計の起点とし、後続入力は記録したmarkからの経過時間で流します。別名のmarkは再生を進めません。markは状態を復元しません。記録から場面を再構築するAPIはありません。

本物の入力は再生入力と到着順に混ざり、自動停止しません。アプリで必要なときに明示的に停止します。

```cpp
void tcApp::stopDemo() {
    tcx::inputreplay::stopPlayback();
}
```

自然終了では`onPlaybackEnd()`を通知します。RAIIリスナーは`tcApp`のメンバーとして保持します。明示的な停止とエラーでは通知しません。このコールバックから再生を開始すればループできます。

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
    endListener = tcx::inputreplay::onPlaybackEnd().listen([] {
        tcx::inputreplay::play("session.jsonl");
    });
}
```

`tcApp`に`tc::EventListener endListener;`を宣言します。重複したsetup・開始・停止は毎回Warningを出します。ファイルや注入の失敗はErrorを出し、記録と再生を停止します。終了処理では可能ならendレコードを書きます。

## 記録ファイルを機密として扱う

記録した文字にはパスワードが含まれることがあります。ファイルは機密として扱い、秘密を入力する前に記録を停止してください。伏せ字や暗号化のAPIはありません。

```cpp
void tcApp::beforePasswordEntry() {
    tcx::inputreplay::stopRecording();
}
```

入力だけでは乱数の状態、時計、ネットワーク、カメラ、センサーを再現できません。アプリ固有の条件を別途復元してください。このアドオンには、それらのスナップショットAPIはありません。

## JSON Linesバージョン1

UTF-8で1行に1つのオブジェクトを保存します。先頭のヘッダーは`type: "header"`、`version: 1`、`app`、`trussc`、`started`（Unixミリ秒の文字列）、`fps`と、1件の`windows`要素を持ちます。ウィンドウ要素は`key: "main"`、`width`、`height`、`dpi`、`pixelPerfect`です。widthとheightは記録時のApp座標の大きさです。

以降の全レコードは`type`、経過`ms`、相対`frame`を持ちます。入力は`window: "main"`、`injected`、`tag`も持ちます。マウスは`x`、`y`、`mods`（shift=1、ctrl=2、alt=4、super=8）、ボタンは`button`（0〜2）、スクロールは`scrollX`と`scrollY`を追加します。キーは`key`、`mods`、`repeat`、文字は`codepoint`、`mods`、`repeat`です。タッチは`canceled`と`touches`（`id`、`x`、`y`、`pressure`、`changed`）、ドロップは`x`、`y`、`files`、markは`name`、最後のレコードは`type: "end"`です。

再生はファイル全体を検証してから注入し、エラーに行番号を付けます。上限はLFを除く1行64 KiB、改行を含むファイル128 MiB、headerとendを含む1,000,000オブジェクト、`ms`は7日です。時刻とフレーム番号は非減少です。不正・不足フィールド、非有限値や範囲外の値、未知のバージョン・イベント、endの欠落やend後のレコードは拒否します。失敗・中断した記録は不完全な場合があります。復旧APIはありません。

## サンプルとテスト

`example-inputReplay`はRで記録、Sで記録停止、Pで再生、Mでmarkを示します。`tests`は画面なしで、ファイル検証・上限の境界値・往復変換・座標変換・仮の時計によるmark同期を確認します。`tests-display --display-check`は注入入力を記録し、画面上で再生時のイベントを比較します。LinuxではXvfbを使えます。

ツールバー、ホットキー、リングバッファ、ファイルダイアログ、複数ウィンドウの対応付け、MCP操作はstage Bです。
