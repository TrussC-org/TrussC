# tcxTcv Roadmap

## Current Status: v4 (GPU Layout + ImGui Encoder)

基本的なエンコード/デコード機能は完成。以下は今後の拡張計画。

---

## Phase 1: Foundation (DONE)

- [x] BC7エンコード/デコード
- [x] I/P/REFフレーム構造
- [x] SKIPブロック（フレーム間差分）
- [x] LZ4圧縮
- [x] GUIエンコーダ
- [x] プレイヤー (TcvPlayer)

---

## Phase 2: Application Improvements (DONE)

### Encoder
- [x] `example-tcvEncoder` → `TrussC_Video_Codec_Encoder` リネーム
- [x] ImGui GUI (左右ペイン、ログウィンドウ、プレビュー)
- [x] P/Uスライダーでパラメータ調整
- [x] ファイル名衝突時の枝番処理 (-1, -2...)

### Player
- [x] シークバー追加 (ドラッグでシーク)
- [x] フレーム番号/タイムコード表示
- [x] キーボードショートカット (SPACE, LEFT/RIGHT, R, D)

### Format v4
- [x] GPUレイアウト直接保存 (デコード時の変換不要)
- [x] デコード時間: ~0.5ms/frame

---

## Phase 3: HAP Support (DONE)

### HAP Input
- [x] tcxHapブランチをマージ
- [x] HAPファイルからTCVへの変換対応
- [x] HAP → TCV ワークフロー最適化

---

## Phase 4: Audio Support (DONE)

### Audio Embedding
- [x] TCV内に音声トラック埋め込み
- [x] 対応コーデック: AAC, MP3（元のまま埋め込み、AACはADTSヘッダー付加）
- [x] VideoPlayerから音声抽出API追加

### Audio Playback
- [x] TcvPlayerにSoundPlayer内蔵
- [x] 映像/音声の同期再生
- [x] シーク時の音声同期

### Platform Support
- [x] macOS: AudioToolbox (AAC/MP3デコード)
- [ ] Windows: Media Foundation (未実装)
- [ ] Linux: GStreamer等 (未実装)

### Windows Build Issue (FIXED)
- [x] TrussC_Video_Codec_Encoder がWindowsでビルドできない
  - 原因: `WAIT_TIMEOUT` が Windows ヘッダーでマクロ定義されている
  - 修正: `WAIT_TIMEOUT` → `kWaitTimeout` にリネーム

---

## Phase 5: Performance & Flexibility

### Playback Speed
- [ ] 再生速度変更 (0.5x - 2.0x)
- [ ] VideoPlayerBaseの拡張確認
- [ ] 音声ピッチ補正（オプション）

### Parallel Encoding
- [x] CPUコア数に応じたスレッド数自動設定 (`-j` オプション)
- [x] ブロック並列BC7エンコード（I-frame）
- [x] P-frameのBC7ブロックも並列化

---

## Phase 6: Platform & Distribution

### CLI Encoder
- [ ] noWindowModeの完全対応
- [ ] バッチ処理のサポート
- [ ] ffmpegライクなオプション体系

### Web Support (Nice to have)
- [ ] WebGPU + WASM対応
- [ ] BC7テクスチャのWeb対応状況確認
- [ ] ストリーミング再生の検討

---

## Ideas / Future

- **Alpha channel optimization**: アルファが単純な場合の最適化
- **HDR support**: 10bit色深度対応
- **Streaming format**: ネットワーク再生向けセグメント形式
- **Hardware encoding**: Metal/CUDA利用の検討

---

## Notes

### Version History

| Version | Changes |
|---------|---------|
| v4 | GPU直接レイアウト、デコード高速化 |
| v3 | SKIP + BC7 + LZ4 (SOLID/Q-BC7削除) |
| v2 | SOLID, Q-BC7追加 |
| v1 | 基本BC7エンコード |

### Performance (v4)

| Metric | Value |
|--------|-------|
| Decode Time | ~0.5ms/frame |
| Encode (Balanced) | P:16, U:1 |
| Encode (High) | P:64, U:4 |
