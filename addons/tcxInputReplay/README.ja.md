# tcxInputReplay

[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

TrussCのウィンドウへの入力を記録し、コード変更後に同じ操作をリアルタイムで再生するデバッグ用アドオンです。場面のタイミングが変わっても、markでアプリの到着を待てます。入力だけではアプリの状態を決定論的に再現できません。状態全体のスナップショットAPIはありません。

`addons.make`に`tcxInputReplay`を追加し、`<tcxInputReplay.h>`をincludeします。すべてのAPIはメインスレッドで呼びます。`tcApp::setup()`で一度`setup()`を呼ぶと、そのウィンドウを選び、アプリの終了処理でスコープ付きリスナーを解放します。

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
}
```

## 入力リプレイのツールバーを使う

`setup()` は呼び出したウィンドウの上端中央に小さなツールバーを表示します。Record は一時ファイルへの録画を開始し、Stop recording はOSの保存ダイアログを開いてJSONLを保存します。キャンセルするとツールバーで開始した録画を破棄します。コードでファイル録画を開始した場合は元のファイルを残します。リングをツールバーから停止した場合も保存ダイアログを開きます。コードからの `stopRecording()` と `flush()` はダイアログを開きません。 録画中は Record が赤くなり Stop recording と表示され、再生中は Play が青くなり Stop playback と表示されます。リング録画中は保持している入力の数（`Ring: N`）も表示します。

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
}
```

保存に失敗した場合は録画を保持し、Save から再試行できます。Errorログに保存元のパスを出します。ツールバーからの保存にはデスクトップのファイルダイアログが必要です。WebとAndroidではSaveを押すたびに一時ファイルを保持し、そのパスをWarningログに出します。ダイアログを使わずにファイル録画を閉じる場合はコードから停止します。

```cpp
void tcApp::saveCapture() {
    tcx::inputreplay::stopRecording();
}
```

リングは保存先を指定して書き出します。

```cpp
void tcApp::saveCapture() {
    tcx::inputreplay::flush("session.jsonl");
}
```

ツールバーの一時ファイルは `setFlushDirectory()` のフォルダー内に作成します（既定はデータフォルダー内の `input-replay`）。POSIXでは所有者だけが読み書きできます。クラッシュ時や未保存のまま終了した場合は一時ファイルが残ることがあります。新しい録画を開始すると、未保存の一時ファイルをWarningログとともに破棄します。


Open で録画ファイルを選びます。ツールバーにファイル名を表示し、ポインターを重ねるとフルパスを表示します。セットアップしたウィンドウへのドロップは、最初の `.jsonl` パスを選択します（大文字小文字を区別します）。再生は開始しません。すべてのドロップはアプリにも通常どおり届きます。録画は続き、ドロップも通常どおり記録します。ほかの拡張子はアドオンが無視します。Open とドロップはパスを選択し、Play が検証して再生を開始します。

既定ではF9でツールバーの表示だけを切り替えます。ホットキーは最初から有効で、キーリピートは無視します。macOSではfnと一緒に押すか、キーを変更します。アプリで同じキーを使う場合は、coreの整数キー定数で変更するか無効にします。

```cpp
void tcApp::configureReplay() {
    tcx::inputreplay::setHotKey(tc::KEY_F8);
    tcx::inputreplay::setHotKeyEnabled(false);
}
```

再生開始時にツールバーを隠し、再生終了または停止時に再生前の表示状態へ戻します。再生中もホットキーで表示を切り替えられます。ファイル再生中はツールバーへの入力を無効にし、再生終了後に処理されるキュー内の再生入力も無視します。

アドオンはtcxImGuiを自動で読み込みます。アプリが `imguiBegin()` / `imguiEnd()` を使う場合はそのフレームに自動で相乗りし、使わない場合は描画前にアドオンがフレームを開始します。ツールバーを手動で描画する必要はありません。ツールバーはセットアップしたウィンドウのImGuiコンテキストに接続します。アプリのクリーンアップまでコンテキストを維持してください。明示的な `imguiShutdown()` の後に再接続するAPIはありません。


ホットリロードを使う場合は `addons.make` にtcxImGuiも明示します。

```makefile
tcxInputReplay
tcxImGui
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

マウス移動、ドラッグ（移動として1回）、ボタン、スクロール、リピートを含むキー、文字、修飾キー、タッチのID・キャンセル・筆圧、ファイルドロップのパスを記録します。注入入力は`injected`と`tag`を保持し、このアドオン自身の同期的な再生入力は除外します。タッチから合成したマウス入力は、タッチの再注入で作られるため記録しません。再生ツールバーが消費するマウス・キーボード入力は、ツールバー外でのリリースも含めて記録しません。タッチイベントと他のImGuiへの入力は記録します。

ファイルドロップはパスだけを保存し、ファイル自体は再作成しません。アドオンにファイルの復元APIはありません。coreのドロップ引数には出どころを取得するAPIがないため、記録では`injected: false`と空のtagになります。同期的な再生ガードにより再記録は防ぎます。スクロール位置はデータとして保存しますが、注入APIは再生先の現在のポインター位置を使います。スクロール位置を直接渡すAPIはありません。

## 記録した入力の件数を調べる

`getRecordedCount()`は現在の記録に書いた入力の件数（Ringでは保持中の入力）を返します。markとheader/end行は含まず、停止中は0です。この値で独自の上限を設けられます。

```cpp
void tcApp::update() {
    if (tcx::inputreplay::getRecordedCount() > 100000)
        tcx::inputreplay::stopRecording();
}
```

## 最近の入力をリングに保持する

リング記録は障害の調査用に直近N分の入力をメモリに保持します。flushまではファイルを開きません。件数は任意で指定でき、次のコードは最大5分、100,000件の入力・markを保持します。

```cpp
void tcApp::beginCapture() {
    tcx::inputreplay::startRecording(tcx::inputreplay::RecordingMode::Ring, std::chrono::minutes(5), 100000);
}
```

時間には正の`std::chrono::milliseconds`を指定し、chronoのminutesやsecondsからも変換できます。件数はheader/endを含まず、正の値を指定します。既定では件数を制限しません。時間と、指定した場合は件数に応じて古い完全なレコードを取り除きます。

`flush(path)`は保持区間を書き出し、ファイルエラーの場合も記録を停止します。親フォルダを作成し、ダイアログは開きません。次の障害も記録する場合は明示的に再開します。

```cpp
void tcApp::saveIncident() {
    tcx::inputreplay::flush("incidents/session.jsonl");
}
```

`flush()`はdataフォルダ内の`input-replay`にUnixマイクロ秒の時刻を使った名前で保存します。setup後にフォルダを変更できます。

```cpp
void tcApp::configureCapture() {
    tcx::inputreplay::setFlushDirectory("incidents");
}
```

自動の名前で保存します。

```cpp
void tcApp::saveIncident() {
    tcx::inputreplay::flush();
}
```

flushは有効なheader/endを書き、時刻とフレームを最初の保持レコードに合わせ直します。先頭のmarkが取り除かれた場合、再生は保持区間から始まり、以前の場面は復元できません。アプリの状態を復元するAPIはありません。`stopRecording()`はリングを破棄します。保存する場合は`flush()`を使います。

```cpp
void tcApp::saveIncident() {
    tcx::inputreplay::flush();
}
```

停止中のflushは毎回Warningを出します。ファイル記録には引き続き`startRecording(path)`と`stopRecording()`を使い、flushはリング記録に使います。flushの前にリング記録を開始します。

```cpp
void tcApp::beginCapture() {
    tcx::inputreplay::startRecording(tcx::inputreplay::RecordingMode::Ring, std::chrono::minutes(5), 100000);
}
```

## 複数ウィンドウを明示的に対応付ける

引数なしの`setup()`は現在のウィンドウを`main`に対応付けます。複数のウィンドウを記録する場合は先に作成し、安定したキーとWindowポインターをsetupに渡します。tcAppに`std::shared_ptr<tc::Window> preview;`を宣言し、次のsetupの前に`tc::createWindow(settings)`の戻り値を代入します。

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup({{"main", getWindow()}, {"preview", preview.get()}});
}
```

対応付けたウィンドウだけを記録します。作成順やタイトルが変わっても再生時には同じキーを使います。欠けた・閉じた・重複したキーや保存先ではsetup/playが失敗します。対応付けたウィンドウが閉じると、markの待機中もErrorを出して再生を停止します。記録・再生の前に、開いている各保存先を一度ずつ対応付けます。

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup({{"main", getWindow()}, {"preview", preview.get()}});
}
```

対応付けはアプリの終了処理まで固定です。実行中に対応を変更するAPIはありません。座標とスケーリングは各キーの記録時と現在の寸法を使います。coreの開いているウィンドウ一覧で寿命を確認し、一時的なネイティブIDやタイトルをキーとして保存しません。

## MCPから入力の記録と再生を操作する

setupは5つのツールを`Control`レベルで登録します。登録だけでは権限の付与やサーバーの起動はしません。デバッグ用アプリで既存のMCP Control権限を有効にして使います。

```cpp
void tcApp::enableRemoteDebugging() {
    if (debugBuild) tc::mcp::setToolLevel(tc::mcp::ToolLevel::Control);
}
```

| ツール | 引数 |
| --- | --- |
| `tcx_inputreplay_start_recording` | ファイルには`path`、省略するとリングです。任意の`durationMs`（既定300000）と`capacity`（既定は件数無制限）を指定できます |
| `tcx_inputreplay_stop_recording` | なし。リングは破棄します |
| `tcx_inputreplay_play` | `path`、任意の`scale`: `none`、`stretch`、`fit` |
| `tcx_inputreplay_stop_playback` | なし |
| `tcx_inputreplay_flush` | 任意の`path`。省略時は設定したフォルダを使います |

操作関数は`bool`を返し、ツールは`status: "ok"`またはエラーの文章を返します。MCP呼び出し自体は記録しません。デバッグ用なので、配布アプリでは有効にしないことを勧めます。setupとControl権限はデバッグビルドでだけ有効にします。

```cpp
void tcApp::setup() {
    if (debugBuild) tcx::inputreplay::setup();
}
```

ビルド設定に従う`bool debugBuild`メンバーを宣言し、`enableRemoteDebugging()`もデバッグビルドでだけ呼びます。

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

UTF-8で1行に1つのオブジェクトを保存します。先頭のヘッダーは`type: "header"`、`version: 1`、`app`、`trussc`、`started`（Unixミリ秒の文字列）、`fps`と、一意のキーを持つ`windows`配列を持ちます。ウィンドウ要素は`key`（既定は`"main"`）、`width`、`height`、`dpi`、`pixelPerfect`です。widthとheightは記録時のApp座標の大きさです。

以降の全レコードは`type`、経過`ms`、相対`frame`を持ちます。入力は対応付けた`window`キー（既定は`"main"`）、`injected`、`tag`も持ちます。マウスは`x`、`y`、`mods`（shift=1、ctrl=2、alt=4、super=8）、ボタンは`button`（0〜2）、スクロールは`scrollX`と`scrollY`を追加します。キーは`key`、`mods`、`repeat`、文字は`codepoint`、`mods`、`repeat`です。タッチは`canceled`と`touches`（`id`、`x`、`y`、`pressure`、`changed`）、ドロップは`x`、`y`、`files`、markは`name`、最後のレコードは`type: "end"`です。

再生はファイル全体を検証してから注入し、エラーに行番号を付けます。時刻とフレーム番号は非減少です。不正・不足フィールド、非有限値や範囲外の値、未知のバージョン・イベント、endの欠落やend後のレコードは拒否します。失敗・中断した記録は不完全な場合があります。復旧APIはありません。

## サンプルとテスト

`example-inputReplay`はRで記録、Sで記録停止、Pで再生、Mでmarkを示します。`tests`は画面なしで、ファイル検証・長い記録・往復変換・座標変換・仮の時計によるmark同期を確認します。`tests-display --display-check`は注入入力を記録し、画面上で再生時のイベントを比較します。LinuxではXvfbを使えます。
