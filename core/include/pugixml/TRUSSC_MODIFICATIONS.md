# TrussC XML integration

Base: bundled pugixml 1.16.

The parser checks element depth before appending a node, rejecting depth beyond
64 with a parse error. It also rejects internal terminators before parsing so a
successful prefix cannot conceal unparsed input. Local files have no byte-size
cap; node and attribute allocation remain proportional to actual input.

The `Xml` wrapper parses into a temporary document and replaces its current
contents only on success. Keep these checks when updating pugixml and run
`xml`, `documentImports`, and load-contract tests.
