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

Local glTF files have no parse-table cap. When parse-table allocations first exceed 256 MiB, one Notice per load reports the resolved file path and the byte count. Allocation failures still reject the load safely; integer/addressing checks and the JSON nesting limit remain in place. The Notice is emitted after cgltf returns, outside its C callbacks.

ローカル glTF ファイルのパーステーブルに容量上限はありません。確保量が初めて 256 MiB を超えると、読み込みごとに一度、解決済みファイルパスとバイト数を Notice に記録します。確保失敗時は安全に読み込みを失敗させ、整数・アドレス範囲の検査と JSON の深さ制限は維持します。Notice は cgltf から戻った後、C コールバックの外で出力します。

로컬 glTF 파일의 파싱 테이블에는 용량 상한이 없습니다. 할당량이 처음 256 MiB를 넘으면 로드마다 한 번, 해석된 파일 경로와 바이트 수를 Notice로 기록합니다. 할당 실패 시 안전하게 로드에 실패하며 정수·주소 범위 검사와 JSON 중첩 깊이 제한은 유지합니다. Notice는 cgltf가 반환한 후 C 콜백 밖에서 기록합니다.

The [headless tests](tests/README.md) cover the load contract, Japanese paths, URI decoding and model validation. Animation, skinning and morph targets remain unsupported.

OOM recovery is supported in Release builds, or with iterator debugging off (`_ITERATOR_DEBUG_LEVEL=0` on MSVC). With iterator debugging enabled, constructing or moving even an empty `LoadResult` / `SaveResult` string or STL container can allocate a container proxy, so persistent allocation failure can terminate in a `noexcept` operation. Diagnostic text and logging remain best effort.
