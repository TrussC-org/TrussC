# TrussC XML integration

Base: bundled pugixml 1.16. `pugixml.cpp` is restored byte-for-byte from the
pristine 1.16 import in TrussC commit `5c7f8499`; it has no TrussC parser patches.

`tc::Xml` follows upstream pugixml behavior for element depth. It rejects every
NUL code unit in file input (including a final one) using pugixml's detected
encoding; the terminator pugixml adds itself is not part of the file and is
accepted. String input is UTF-8 and rejects every NUL byte. File input supports
UTF-16/32 documents. Failures preserve the previous document. Local files have
no byte-size cap. NUL rejection ensures success means the whole input was read.

Upstream element parsing, tree destruction and printing are iterative, so they
do not grow the call stack with element depth. DOM memory grows with input.
If you recursively walk untrusted XML in application code, mind the depth.

Run `xml`, `documentImports`, and load-contract tests when updating.
