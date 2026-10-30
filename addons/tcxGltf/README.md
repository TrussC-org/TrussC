# tcxGltf

`tcx::gltf::GltfModel` loads glTF 2.0 (`.gltf` / `.glb`) geometry, materials and textures using cgltf v1.15.

```cpp
tcx::gltf::GltfModel model;
auto result = model.load("model.gltf");
if (result) {
    model.draw(); // in a graphics context
}
```

`LoadResult load(const fs::path& path)` resolves relative paths through `getDataPath()` and accepts absolute paths unchanged. A missing model returns `FileNotFound` with one Error (`file not found: <resolved path>`); parse, buffer-read and model-validation failures return `DecodeFailed` with one Error and leave the model empty. There is no `getPath()`. `if (model.load(path))` still works; use `.ok()` when assigning the result to a `bool`.

Pass a native `fs::path`, or use `utf8ToPath(text)` for UTF-8 text on Windows. Model files and external buffers open through filesystem-aware cgltf callbacks; external images use the core pixel loader. Buffer and image URI escapes are decoded once before joining the model's directory, so `a%20b.png` selects `a b.png`. Data URI buffers remain handled by cgltf; inline data URI images remain unsupported. Unavailable optional textures are still skipped, with diagnostics.

glTF sampler wrap modes (`REPEAT`, `MIRRORED_REPEAT`, `CLAMP_TO_EDGE`) are applied
independently to U/S and V/T. Missing samplers and omitted wrap fields use
`REPEAT`. Magnification supports `NEAREST` and `LINEAR`; minification supports
`NEAREST`, `LINEAR` (without mipmaps), `NEAREST_MIPMAP_LINEAR` and
`LINEAR_MIPMAP_LINEAR`. Texture has no nearest-mipmap interpolation setting, so
`NEAREST_MIPMAP_NEAREST` and `LINEAR_MIPMAP_NEAREST` retain the existing Linear
filter with linear mipmap interpolation. Unspecified filters keep that default.
Each material texture load owns a separate Texture, even when the image or glTF
texture is shared; different samplers do not overwrite each other. Repeated
references still duplicate image decoding and GPU storage.

C1 keeps two small insertions fixing an upstream allocation-failure path during
parsing in a hash-verified build copy; see [PROVENANCE.md](PROVENANCE.md).

Local glTF files have no parse-table cap or allocation accounting. cgltf
allocation errors fail the load through its result codes and normal cleanup.
Allocation and storage-width exceptions while building the TrussC model clear
partial state and return `DecodeFailed`. Diagnostics normally log one Error.
If memory remains unavailable or a logger listener throws, the fallback writes
a fixed message to stderr and returns an empty result message. An Error event
and a detailed message are not guaranteed in that case. Integer/addressing
checks and the JSON nesting limit remain in place.

Persistent exhaustion is not supported with MSVC iterator debugging: its STL
can terminate while allocating internal container proxies. Those test cases
are skipped in that configuration.

ローカル glTF ファイルのパーステーブルには容量上限も確保量の追跡もありません。
cgltf の確保失敗は結果コードと通常の解放処理で読み込み失敗として扱います。
TrussC モデル構築中の確保・格納幅の例外では途中の状態を消去して `DecodeFailed` を返します。
通常は Error を一度記録します。メモリ不足が続く場合やログリスナーが例外を投げる場合は、
固定文を stderr に出力し、結果のメッセージは空になります。この場合、Error イベントや
詳しいメッセージは保証しません。整数・アドレス範囲の検査と JSON の深さ制限は維持します。
MSVC のイテレータデバッグでは、STL 内部の確保失敗で終了する場合があるため、
継続的なメモリ不足は保証対象外で、そのテストをスキップします。

로컬 glTF 파일의 파싱 테이블에는 용량 상한이나 할당량 추적이 없습니다.
cgltf 할당 오류는 결과 코드와 일반 해제 처리로 로드 실패를 반환합니다.
TrussC 모델 구성 중 할당 및 저장 범위 예외가 발생하면 부분 상태를 지우고
`DecodeFailed`를 반환합니다. 보통 Error를 한 번 기록합니다. 메모리 부족이 계속되거나
로그 리스너가 예외를 던지면 고정 문구를 stderr에 출력하고 결과 메시지는 비어 있습니다.
이 경우 Error 이벤트와 자세한 메시지는 보장하지 않습니다.
정수·주소 범위 검사와 JSON 중첩 깊이 제한은 유지합니다.
MSVC 반복자 디버깅에서는 STL 내부 할당 실패로 종료될 수 있으므로 지속적인
메모리 부족은 보장하지 않으며 해당 테스트를 건너뜁니다.

The [headless tests](tests/README.md) cover the load contract, Japanese paths, URI decoding and model validation. Animation, skinning and morph targets remain unsupported.
