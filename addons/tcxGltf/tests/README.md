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
- counts large enough to wrap the size arithmetic fail to load, also on an
  accessor without a buffer view;
- a primitive whose attribute counts differ, or whose indices point past its
  vertices (checked by cgltf_validate() against the accessor count, and by the
  loader against the vertices actually read), fails to load;
- a primitive without POSITION, or whose POSITION accessor has no buffer view,
  is skipped with one warning per load (with the count) and the rest of the
  model loads;
- a count too large to allocate fails to load (the allocation failure is
  caught inside `load()`);
- an image in a buffer without data (no uri) is skipped; the mesh loads;
- a component type glTF 2.0 does not allow fails validation;
- a file with no scene loads from its root nodes; a file with no scene and no
  nodes fails to load;
- a node chain 100000 deep loads (the hierarchy is walked without recursion,
  each world transform computed once from its parent's); a node cycle is
  refused by cgltf_validate(), and a scene that lists a node twice by the
  loader.

Every failed load logs a warning and leaves the model empty.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
