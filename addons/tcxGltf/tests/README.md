# tcxGltf tests

Headless console test (no window). Each case writes a small `.gltf` (buffer
embedded as a base64 data URI) or `.glb` (JSON and BIN chunks, built at
runtime) to a temp directory and loads it with `GltfModel`, checking that
model data is validated before it is read:

- valid models (indexed, non-indexed, a node hierarchy, a sparse accessor, a
  GLB with positions and indices in its BIN chunk) load with the same vertex
  and index data as before, and sparse values are read tightly packed, as
  glTF lays them out, also when the base view has a byteStride;
- an accessor or buffer view that runs past its buffer view or buffer (in a
  GLB, past the BIN chunk), or a reference to an accessor or buffer view that
  does not exist, fails to load;
- a count too large to address fails to load, also on an accessor without a
  buffer view;
- a buffer view byteStride smaller than the accessor's element fails to load;
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
  `internal::setTextureLoadHookForTests()`, `std::bad_alloc` or
  `std::length_error`) fails the load, logs an Error (with `what()` when there is one)
  and leaves the model empty, also of the primitive read before it. Under
  ASan this also checks that the parsed data is freed on that path;
- a component type glTF 2.0 does not allow fails validation;
- a file with no scene loads from its root nodes; a file with no scene and no
  nodes fails to load;
- a node chain 20000 deep loads (the hierarchy is walked without recursion,
  each world transform computed once from its parent's). A node cycle is
  refused by cgltf_validate(), and a scene that lists a node twice by the
  loader;
- Japanese-named glTF files in a Japanese-named folder load an external
  PNG, with either a data URI buffer or a Japanese-named external `.bin`.

Sampler fixtures check explicit REPEAT, MIRRORED_REPEAT and CLAMP_TO_EDGE,
independent U/V axes, empty/missing samplers, supported min/mag filters and
mipmap counts, plus independent textures when two samplers share an image.
They inspect both Texture state and dummy-backend sampler descriptors. Unsupported
nearest-mipmap interpolation modes retain the existing default.
A 22-primitive model also checks every texture with `sg_query_sampler_state`
and verifies wrap/filter descriptors in a 64-slot sampler pool, covering both
missing samplers and explicit wraps with nearest filters.

Texture cases use sokol's dummy graphics backend (no window or GPU). On
Windows, the test executable uses the UTF-8 manifest from `trussc_app()`.

Rejected input logs one Error and leaves the model empty. A test-only
`cgltf_options` memory callback checks early parser allocation failure, retry,
and buffer allocation failure followed by ordinary `cgltf_free`. Another
test first locates a valid mesh's attribute-table and material-variant mapping
allocations, then fails each exact Nth allocation after a partial tree has been
built, checks OOM without a published tree, and retries successfully. Production
uses cgltf's default allocator without tracking or failure-injection hooks.
Model-building tests throw `std::bad_alloc` after the first primitive and
`std::length_error` at texture loading, checking empty state and one Error.
Existing image encoder failure tests remain independent of parser allocation.

The test executable's STL allocator also refuses every allocation starting at
the second primitive's texture step, through failure diagnostics and result
construction. The result must be `DecodeFailed` with empty state and no escaped
exception; a later retry must load. A separate case starts exhaustion during
path resolution. No production allocator hook is used. These persistent cases
are skipped with MSVC iterator debugging, whose STL proxy construction can
terminate on allocation failure. Throwing logger listeners are also contained.
Under persistent exhaustion, diagnostics use a fixed stderr fallback and the
result message may be empty; an Error event is not required.

Synthetic size tests exercise the actual token-count, individual-length and
ptrdiff_t offset limits without large fixtures. A synthetic trailing-whitespace
size seam reaches the load-time document-size gate beyond INT_MAX. Load tests
also cover trailing NUL glTF and NUL-padded GLB JSON. Width errors retain their
cause. Misaligned accessor offsets/strides and sparse offsets report alignment errors,
as required by glTF 2.0 component alignment. JSON nesting is limited to 64
containers for untrusted input; cgltf skips iteratively. Synthetic tests check
cumulative skip-end arithmetic as well as per-object products.
Node hierarchies use the existing iterative walk with cycle and duplicate
protection. Local model and referenced files have no policy size or per-table
count caps. A roughly 5.2 MB JSON file with large parse tables loads twice
without a parse-table Notice. Both unpadded base64 tails load; a one-character
tail fails.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```

The load-entry tests check `FileNotFound` and the exact shared missing-file
Error, `DecodeFailed` with exactly one Error for rejected data, relative paths
through `getDataPath()`, Japanese model/buffer/texture filenames, escaped spaces
and UTF-8 in buffer/image URIs, and `%25` decoded exactly once. All fixtures
are generated at runtime.
