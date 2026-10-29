# cgltf provenance

CMake fetches cgltf v1.15 from https://github.com/jkuhlmann/cgltf and leaves
that source intact. The LF-normalized upstream `cgltf.h` SHA-256 is
`e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec`.

C1 consists of two small insertions fixing an upstream allocation-failure path
during parsing: reset `*out_attributes_count` to zero if its table allocation
fails; check the material-mapping allocation, reset `mappings_count` to zero
and return `CGLTF_ERROR_NOMEM` on failure. No other cgltf source changes are made.
The generated implementation copy, `cgltf-safe/tcxGltfCgltf.h`, has SHA-256
`ad63ae638f790df8346a2fc7abc56c1a03b46a27e1c1f71a9093999892cebf74`.
CMake fails configuration if either checksum differs. The MIT license remains
in the generated header. Tests use the fetched header's unchanged declarations.

M42 parser allocation tracking is removed for #1105. Production uses cgltf's
default allocator, result codes and `cgltf_free`, without an allocation list,
callback state or allocation hooks.

`CGLTF_ATOI` / `CGLTF_ATOLL` remain defined in `tcxGltf.cpp`, using
`invalidModelInteger` to reject malformed integer counts and references. They
are integer-conversion correctness checks, independent of allocation tracking,
and require no changes to upstream source.

Filesystem-aware file callbacks contain exceptions and return cgltf result
codes; exceptions never unwind through cgltf callbacks. TrussC model building
has one exception boundary that clears partial state and returns a load failure.
