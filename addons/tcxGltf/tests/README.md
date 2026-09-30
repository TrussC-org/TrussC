# tcxGltf tests

Headless console test (no window). Each case writes a small `.gltf` (buffer
embedded as a base64 data URI) to a temp directory and loads it with
`GltfModel`, checking that model data is validated before it is read:

- valid models (indexed, non-indexed, a node hierarchy, a sparse accessor) load
  with the same vertex and index data as before;
- an accessor or buffer view that runs past its buffer view or buffer, or a
  reference to an accessor or buffer view that does not exist, fails to load;
- counts large enough to wrap the size arithmetic fail to load;
- a primitive whose attribute counts differ, or whose indices point past its
  vertices, fails to load;
- a file with no scene fails to load.

Every failed load logs a warning and leaves the model empty.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
