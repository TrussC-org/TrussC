# cgltf provenance

CMake fetches cgltf v1.15 from https://github.com/jkuhlmann/cgltf.
The upstream `cgltf.h` SHA-256 is
`e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec`.
Its MIT license is retained in the header.

The fetched file stays intact. CMake generates `cgltf-safe/tcxGltfCgltf.h`
in the addon build directory with two integration patches. The first runs immediately after
`cgltf_parse_json_root` returns:

```c
if (CGLTF_TRUSSC_CALLBACK_FAILED(options->memory.user_data))
{
    options->memory.free_func(options->memory.user_data, tokens);
    return cgltf_result_out_of_memory;
}
```

The implementation defines this hook to inspect the per-load callback failure
state. cgltf's parse error cleanup otherwise walks tables whose counts can
already be set when their allocation returned NULL. Its material-variant
mapping parser can also continue after a NULL allocation, so the guard must
precede pointer fixup as well as cleanup. On callback failure no partial tree
is published or passed to `cgltf_free`. The addon's deleter also skips
`cgltf_free` after buffer-loading callback failure. The per-load allocation
registry releases every remaining block directly. Successful loads and
failures without a callback failure use ordinary cgltf cleanup.

The second adds `CGLTF_TRUSSC_RECORD_OOM(options->memory.user_data);` before
the overflow return in `cgltf_calloc`. Otherwise that NULL result bypasses the
allocator callback and its failure state. A synthetic overflow test exercises
this branch without a large input or allocation. CMake checks the upstream
LF-normalized checksum and both insertion sites before creating the build copy,
including when the Windows checkout uses CRLF.

All allocator and file callbacks contain exceptions and return the library's
failure value. Diagnostics run only after the cgltf call returns. This avoids
unwinding through C linkage functions, including under MSVC `/EHsc`.
