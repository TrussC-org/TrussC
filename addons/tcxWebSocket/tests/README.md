# tcxWebSocket tests

Headless console test (no window, no network). It checks how the web build
(Emscripten) turns a received message into `WebSocketEventArgs`
(`detail::fillMessageArgs`, used by `WebSocketClient` on the web). Emscripten
reports a text message as a NUL-terminated UTF-8 string and counts the
terminator in `numBytes`; the web build delivers it like native:

- text `hello` -> `args.message == "hello"` and `args.data` holds 5 bytes;
- an empty text message -> empty `message` and `data`;
- a multi-byte UTF-8 message (`日本語`) -> its 9 UTF-8 bytes, no NUL;
- binary messages are copied as they are, a trailing 0 byte included.

The bytes are laid out as Emscripten hands them over, so the check runs
natively. The web handler itself is only compiled (web CI job).

tcxWebSocket depends on tcxTls, which builds mbedTLS, so this harness has a
`daily-only` marker: CI runs it in the daily workflow
(`examples/build_all.py --addon-tests-only --include-daily`), not per PR. Run
it locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --include-daily --verbose
```
