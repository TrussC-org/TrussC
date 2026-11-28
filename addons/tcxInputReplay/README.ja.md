# tcxInputReplay

このアドオンは実験的なもので、JSONL 形式は今後のバージョンで変更される可能性があります。

通常の Node で構成した、小さな入力録画・再生ツールバーです。App に `Toolbar` を追加すると、そのウィンドウのマウス、キーボード、タッチ、ファイルドロップの入力を録画できます。再生は記録した時刻に入力を注入し、シーンマークで待機します。既存の単一ウィンドウの JSONL バージョン 1 と互換性があり、ImGui は不要です。

このアドオンは `Window::injectInput` を使っています。独自の入力注入ツールを作りたい場合は、`Player.h` を読んでください。

サンプルでは、水色のマウスの軌跡とオレンジ色のクリック点が時間とともに消えていくため、再生中の入力を目で確認できます。

## App にツールバーを追加する

`<tcxInputReplay.h>` をインクルードし、サンプルと同じ `using namespace std;`、`using namespace tc;`、`using namespace tcx;` を使います。`tcApp` に次のメンバーを宣言します。

```cpp
shared_ptr<inputreplay::Toolbar> replay;
vector<Vec2> dots;
```

`setup()` でツールバーを追加します。App のマウス処理では `isPointerOver()` を使うと、ツールバーへのクリックを描画から除外できます。

```cpp
void tcApp::setup() {
    // F9 shows and hides the toolbar. With no argument, no key is assigned.
    replay = make_shared<inputreplay::Toolbar>(KEY_F9);
    // Optional: start and stop recording from the keyboard (no key by default).
    // replay->setRecordKey(KEY_F8);
    addChild(replay);
}
void tcApp::mousePressed(const MouseEventArgs& e) {
    if (replay->isPointerOver()) return;   // the click is for the toolbar
    dots.push_back(e.pos);
}
```

ツールバーは親の上部中央に一度だけ配置されます。移動には通常の Node API を使えます。録画中は Record が赤い Stop に、再生中は Play が青い Stop に変わります。Open または `.jsonl` ファイルのドロップで録画を選択でき、ファイル名にポインターを重ねるとフルパスが表示されます。ツールバーの Stop（リング録画の書き出しを含みます）を押すと、すぐに OS の保存ダイアログが開きます。保存すると、データディレクトリの `input-replay` フォルダーにある一時ファイルを選択したパスへ移動します。別ボリュームへの移動では、コピーしてから一時ファイルを削除します。キャンセルすると一時ファイルを削除します。保存に失敗した場合だけ Save が表示され、再試行できます。コードから `stopRecording()` や `flush()` を呼んでもダイアログは開きません。

保存ダイアログは、名前の末尾に `.jsonl` がない場合、既存ファイルの上書きを確認する前に `.jsonl` を付けます。拡張子の大文字と小文字は区別しません。

## ファイルまたはリングに入力を録画する

ファイルパスは App のデータディレクトリを基準に解決されます。ファイル録画は終了レコードを書いて閉じます。

```cpp
replay->startRecording("session.jsonl");
replay->stopRecording();
```

リング録画は直近の指定時間をメモリに保持します。Flush はその区間を保存して録画を止めます。リング録画中の `stopRecording()` は内容を破棄します。

```cpp
replay->startRecording(inputreplay::RecordingMode::Ring, chrono::seconds(30));
replay->flush("last-30-seconds.jsonl");
```

[example-ringRecording](example-ringRecording) は、素早いドラッグでボールが壁をすり抜けるバグを捉え、自動で保存し、再生時に再現します。

`isRecording()` は両モードの録画状態を返します。`getRecordedCount()` はヘッダー、マーク、終了レコードを除く入力数を返し、リングでは保持中の入力だけを数えます。各 Toolbar は自身のウィンドウだけを録画し、ツールバーへのクリックも含めます。注入された入力は録画しません。

## 入力を再生してシーンを同期する

再生先は Toolbar と同じウィンドウです。`ScaleMode::None` は座標を保持し、`Stretch` は各軸を拡大縮小し、`Fit` は縦横比を保って中央に配置します。

```cpp
replay->play("session.jsonl", inputreplay::ScaleMode::Fit);
replay->mark("scene");
```

録画時と再生時の対応するシーン遷移で `mark("scene")` を呼びます。再生は次の一致するマークまで待機し、早くマークに到達すると前のシーンの残りを飛ばします。`stopPlayback()` で再生を止め、`isPlaying()` で状態を取得できます。再生入力には `injected = true` とソースタグ `"replayed"` が付き、ボタンとツールバーのショートカットキーは反応しません。

## MCP 操作を明示的に有効にする

アドオンの MCP ツールから操作できるのは、`registerMcpTools()` を呼んだ Toolbar だけです。

```cpp
replay->registerMcpTools();
```

Control レベルのツールは `tcx_inputreplay_start_recording`（任意の `path`、省略時はリングで任意の `durationMs`）、`tcx_inputreplay_stop_recording`、`tcx_inputreplay_play`（`path` と任意の `scale`）、`tcx_inputreplay_stop_playback`、`tcx_inputreplay_flush`（`path`）の5つです。最後に明示的に登録したインスタンスが操作対象になります。それ以外の録画・再生処理はアドオンのグローバル状態を使いません。
