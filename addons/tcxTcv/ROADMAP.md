# tcxTcv Roadmap

## Current Status: v3 (SKIP + BC7 + LZ4)

基本的なエンコード/デコード機能は完成。以下は今後の拡張計画。

---

## Phase 1: Foundation (DONE)

- [x] BC7エンコード/デコード
- [x] I/P/REFフレーム構造
- [x] SKIPブロック（フレーム間差分）
- [x] LZ4圧縮
- [x] GUIエンコーダ (example-tcvEncoder)
- [x] プレイヤー (TcvPlayer)

---

## Phase 2: Application Improvements

### Encoder Rename
- [x] `example-tcvEncoder` → `TrussC_Video_Codec_Encoder`
- [ ] スタンドアロンアプリとしてリリース

### HAP Input Support
- [ ] tcxHapブランチをマージ
- [ ] HAPファイルからTCVへの変換対応
- [ ] HAP → TCV ワークフロー最適化

### Player UI
- [ ] シークバー追加
- [ ] フレーム番号/タイムコード表示
- [ ] キーボードショートカット（矢印キーでフレーム送り等）

---

## Phase 3: Audio Support

### Audio Embedding
- [ ] TCV内に音声トラック埋め込み
- [ ] 対応コーデック: AAC, MP3, Opus（元のまま埋め込み）
- [ ] VideoPlayerから音声抽出API追加

### Audio Playback
- [ ] TcvPlayerにSoundPlayer内蔵
- [ ] 映像/音声の同期再生
- [ ] シーク時の音声同期

---

## Phase 4: Performance & Flexibility

### Playback Speed
- [ ] 再生速度変更 (0.5x - 2.0x)
- [ ] VideoPlayerBaseの拡張確認
- [ ] 音声ピッチ補正（オプション）

### Parallel Encoding
- [x] CPUコア数に応じたスレッド数自動設定 (`-j` オプション)
- [x] ブロック並列BC7エンコード（I-frame）
- [x] P-frameのBC7ブロックも並列化

---

## Phase 5: Platform & Distribution

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

### Removed Features (v2 → v3)

以下はv3で削除。LZ4圧縮で十分カバーでき、再生パフォーマンスが大幅に向上したため。

- SOLID block (単色ブロック)
- Quarter-BC7 block (4x4→16x16アップスケール)

### Performance Comparison

| Version | Encode Speed | Decode Time | File Size |
|---------|--------------|-------------|-----------|
| v2 (SOLID+Q-BC7) | Fast | ~5.7ms/frame | Smaller |
| v3 (BC7 only) | Moderate | ~0.6ms/frame | +18% |

→ 再生パフォーマンス優先でv3を採用
