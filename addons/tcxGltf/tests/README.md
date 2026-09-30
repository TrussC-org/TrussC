# tcxGltf tests

Headless console test (no window). Each case writes a small `.gltf` (buffer
embedded as a base64 data URI) to a temp directory and loads it with
`GltfModel`, checking that model data is validated before it is read:

- valid models (indexed, non-indexed, a node hierarchy, a sparse accessor) load
  with the same vertex and index data as before, and sparse values are read
  tightly packed, as glTF lays them out, also when the base view has a
  byteStride;
- an accessor or buffer view that runs past its buffer view or buffer, or a
  reference to an accessor or buffer view that does not exist, fails to load;
- a count too large to address fails to load, also on an accessor without a
  buffer view;
- a primitive whose attribute counts differ, or whose indices point past its
  vertices (checked by cgltf_validate() against the accessor count, and by the
  loader against the vertices actually read), fails to load;
- a primitive without POSITION, or whose POSITION or index accessor has no
  data in memory (no buffer view, or a view on a buffer without data), is
  skipped with one warning per load (with the count) and the rest of the
  model loads, without allocating an array from that accessor's count;
- a sparse accessor with more values than elements, or with a repeated or
  decreasing index (glTF requires them strictly increasing), fails to load;
- an image in a buffer without data (no uri) is skipped; the mesh loads;
- an external image that cannot be loaded, or whose uri holds a byte that is
  not valid UTF-8, is skipped with a warning; the mesh loads. The image path
  is built with `utf8ToPath()`, which throws for such a uri on Windows (the
  loader catches it and skips that image). On POSIX the bytes are a valid
  file name, so there the image is skipped because the file does not exist;
- an exception while the model is read (injected through
  `internal::setTextureLoadHookForTests()`, a `std::exception` and one that
  is not) fails the load, logs a warning (with `what()` when there is one)
  and leaves the model empty, also of the primitive read before it. Under
  ASan this also checks that the parsed data is freed on that path;
- a component type glTF 2.0 does not allow fails validation;
- a file with no scene loads from its root nodes; a file with no scene and no
  nodes fails to load;
- a node chain 20000 deep loads (the hierarchy is walked without recursion,
  each world transform computed once from its parent's). A node cycle is
  refused by cgltf_validate(), and a scene that lists a node twice by the
  loader.

Every failed load logs a warning and leaves the model empty.

`load()` also catches an allocation failure (`std::bad_alloc`,
`std::length_error`) and fails the same way. No case here reaches it, since
each array the loader allocates has a size bounded by the data loaded for
it. Like the injected exceptions above, it applies where exceptions are
caught: native builds. TrussC's web (Emscripten) builds do not enable
exception catching, so there an allocation failure aborts.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
