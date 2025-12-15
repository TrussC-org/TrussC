# TrussC サンプル ロードマップ

openFrameworks のサンプル構造を参考に、TrussC に必要なサンプルをリストアップ。

## 現在のサンプル

### templates/
- [x] emptyExample - 最小構成テンプレート

### graphics/
- [x] graphicsExample - 基本図形描画
- [x] colorExample - 色空間と補間

### 3d/
- [x] ofNodeExample - シーングラフ・ノードシステム
- [x] 3DPrimitivesExample - 3Dプリミティブ

### math/
- [x] vectorMathExample - ベクトル・行列演算

---

## 今後実装したいサンプル

### graphics/ (優先度: 高)
- [ ] fontsExample - TrueTypeフォント描画
- [ ] imageLoaderExample - 画像読み込み・描画
- [ ] polygonExample - ポリゴン・パス描画
- [ ] blendingExample - ブレンドモード

### 3d/ (優先度: 高)
- [ ] easyCamExample - マウス操作カメラ
- [ ] meshFromCameraExample - メッシュとカメラ
- [ ] pointCloudExample - ポイントクラウド

### math/ (優先度: 中)
- [ ] noiseField2dExample - パーリンノイズ
- [ ] particlesExample - パーティクルシステム
- [ ] trigonometryExample - 三角関数アニメーション

### input_output/ (優先度: 中)
- [ ] keyboardExample - キーボード入力
- [ ] mouseExample - マウス入力詳細
- [ ] dragDropExample - ファイルドラッグ&ドロップ

### events/ (優先度: 中)
- [ ] eventsExample - イベントシステム
- [ ] customEventsExample - カスタムイベント

### gl/ (優先度: 低 - 上級者向け)
- [ ] fboExample - フレームバッファオブジェクト
- [ ] shaderExample - カスタムシェーダー
- [ ] textureExample - テクスチャ操作
- [ ] vboExample - 頂点バッファ

### sound/ (優先度: 低)
- [ ] soundPlayerExample - サウンド再生
- [ ] audioInputExample - マイク入力

### video/ (優先度: 低)
- [ ] videoPlayerExample - ビデオ再生
- [ ] videoCaptureExample - カメラキャプチャ

---

## 必要な機能（サンプル実装のための前提）

### 近い将来
- [ ] ofTrueTypeFont 相当 - フォント描画
- [ ] ofImage 相当 - 画像読み込み
- [ ] ofEasyCam 相当 - 3Dカメラ操作
- [ ] ofNoise 相当 - パーリンノイズ

### 中期
- [ ] ofFbo 相当 - オフスクリーンレンダリング
- [ ] ofShader 相当 - カスタムシェーダー
- [ ] ofSoundPlayer 相当 - サウンド再生

### 長期
- [ ] ofVideoPlayer 相当 - ビデオ再生
- [ ] ofVideoGrabber 相当 - カメラキャプチャ

---

## 参考リンク

- [oF Examples](https://github.com/openframeworks/openFrameworks/tree/master/examples)
- [oF Documentation](https://openframeworks.cc/documentation/)
