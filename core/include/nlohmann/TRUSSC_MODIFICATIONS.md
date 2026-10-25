# TrussC JSON integration

Base: bundled nlohmann/json 3.12.0.

The text parser checks nesting depth before calling a DOM/SAX handler. The
default 64-level structural bound limits recursion in DOM consumers and destruction.
A TrussC-only five-argument `parse` overload passes MCP's configured admission
depth directly to the text parser constructor. The parser owns its immutable
limit (the larger of that argument and 64); ordinary parse/accept/SAX entry
points use 64. MCP's callback still enforces any lower configured depth.
There is no ambient policy or mutable inline state. Local document bytes and
node counts have no policy cap.

The internal default base records parsed provenance, including child values.
Copies, moves, assignments and swaps preserve it. Reflection uses this private
information to validate an external update before applying any fields or
setters; app-constructed JSON retains its existing behavior. Parser callbacks
also mark completed values. There is no public provenance API.

Allocation recovery during JSON DOM cleanup is not guaranteed. Reflection
setter exceptions during commit have no rollback guarantee.

Keep these changes when updating the bundled header. Run `documentImports`,
`documentTables`, `nodeReflectRoundTrip`, and the MCP tests after updates.
