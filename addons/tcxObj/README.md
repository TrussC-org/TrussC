# tcxObj

`tcx::obj::ObjLoader` loads Wavefront OBJ geometry, MTL materials and diffuse textures.

```cpp
tcx::obj::ObjLoader model;
auto result = model.load("model.obj");
if (result) {
    Mesh mesh = model.getMesh();
}
```

`LoadResult load(const fs::path& path)` resolves relative paths through `getDataPath()` and accepts absolute paths unchanged. A missing model returns `FileNotFound` with one Error (`file not found: <resolved path>`); a rejected model returns `DecodeFailed` with one Error. Neither the loader nor its groups record a load path. `if (model.load(path))` still works; use `.ok()` when assigning the result to a `bool`.

Pass a native `fs::path`, or use `utf8ToPath(text)` for UTF-8 text on Windows. The bundled tinyobjloader opens OBJ and MTL files through its wide-path Windows support, and textures use the core image loader's filesystem path support.

The [headless tests](tests/README.md) cover the load contract, Japanese model/material/texture paths and geometry handling. `ObjExporter` retains its existing save API.
