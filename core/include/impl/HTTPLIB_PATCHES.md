# cpp-httplib local patches

The vendored version remains the version declared in `httplib.h`.
`TRUSSC PATCH` marks the request-admission changes for #863:

- The default payload allowance is 64 MiB. The existing payload setter applies
  to Content-Length, cumulative wire bytes (including chunk framing), and
  cumulative decoded bytes, before receivers and multipart parsing see them.
- Request lines, headers and trailers share configurable metadata limits:
  8 KiB per line, 64 KiB total and 128 header/trailer fields. Line reads stop
  before appending bytes beyond the allowance. Rejections return 431.
- An optional request-scope factory spans each request, response write and
  request/response destruction, including early failures. MCP uses it to retire
  per-request credits on keep-alive sockets while keeping socket ingress credits.
- Admission failures close the connection. The server counts body/header
  rejections and reports the first rejection per store, or calls the configured
  limit handler for integration with the host's diagnostics.

When updating cpp-httplib, retain these guarantees or replace the patch with
upstream equivalents. Run `core/tests/httpAdmission` and
`core/tests/mcpAdmission` before accepting the update. MCP socket admission uses
upstream's existing `new_task_queue` hook and lives in `tcMCP.cpp`.
