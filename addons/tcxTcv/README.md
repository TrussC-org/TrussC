# tcxTcv - TrussC Video Codec

TrussC向け独自ビデオコーデック。UI録画やモーショングラフィックスなど静止領域が多い映像に最適化。

## Features

- **GPU-native**: BC7テクスチャ圧縮でGPU直接デコード（超高速再生）
- **高速シーク**: I/P/REFフレーム構造で任意位置へ即座にシーク可能
- **効率的圧縮**: SKIP + BC7 + LZ4 による高圧縮率
- **アルファ対応**: RGBA形式で透過動画もOK

## File Format (v3)

```
.tcv file structure:

Header (64 bytes):
  - signature: "TCVC"
  - version: 3
  - width, height, frameCount, fps
  - blockSize: 16 (fixed)
  - audio info (reserved)

Frame packets:
  - I_FRAME: 全ブロックBC7 + LZ4圧縮
  - P_FRAME: SKIP/BC7コマンド + LZ4圧縮
  - REF_FRAME: 過去I-frameへの参照
```

## Usage

### Player

```cpp
#include <tcxTcv.h>

TcvPlayer player;
player.load("video.tcv");
player.play();

void update() {
    player.update();
}

void draw() {
    player.draw(0, 0);
}
```

### Encoder

```cpp
#include <tcxTcv.h>

TcvEncoder encoder;
encoder.setQuality(1);  // 0=fast, 1=balanced, 2=high
encoder.begin("output.tcv", width, height, fps);

for (int i = 0; i < frameCount; i++) {
    encoder.addFrame(pixelData);  // RGBA
}

encoder.end();
```

## Compression Details

| Block Type | Description | Size |
|------------|-------------|------|
| SKIP | Same as reference frame | 0 bytes |
| BC7 | 16x16 block (16 BC7 blocks) | 256 bytes |

- RLE: 1-128連続ブロックを1バイトで表現
- LZ4: フレームデータ全体を追加圧縮

## Performance (typical)

| Metric | Value |
|--------|-------|
| Decode time | ~0.6ms/frame (1080p) |
| File size | ~50-70% of raw BC7 |

## Examples

- `TrussC_Video_Codec_Encoder` - エンコーダアプリ（GUI/CLI両対応）
- `example-tcvPlayer` - 再生サンプル

## Dependencies

- LZ4 (bundled in libs/)
- bc7enc (header-only, MIT)
- bcdec (header-only, MIT)

## License

MIT License
