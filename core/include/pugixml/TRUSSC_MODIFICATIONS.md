# TrussC XML integration

Base: bundled pugixml 1.16. `pugixml.cpp` is restored byte-for-byte from the
pristine 1.16 import in TrussC commit `5c7f8499`; it has no TrussC parser patches.

`tc::Xml` enforces its depth-64 policy by walking the temporary parsed tree
iteratively, and rejects every NUL code unit in file input (including a final
one) using pugixml's detected encoding; the terminator pugixml adds itself is not
part of the file and is accepted. String input is UTF-8 and rejects every NUL
byte. File input supports UTF-16/32 documents.
Failures preserve the previous document. Local files have no byte-size cap.

Upstream `xml_parser::parse_tree` uses an iterative cursor walk, not recursive
element parsing. Its tree destruction is iterative too. Deep local/app input can
therefore be parsed and then rejected by the wrapper without element-depth
stack growth, but the entire temporary DOM is allocated first. The limit
protects downstream DOM consumers; direct pugixml users retain upstream rules.

Run `xml`, `documentImports`, and load-contract tests when updating.
