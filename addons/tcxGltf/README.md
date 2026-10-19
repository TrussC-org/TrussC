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

The [headless tests](tests/README.md) cover the load contract, Japanese paths, URI decoding and model validation. Animation, skinning and morph targets remain unsupported.
