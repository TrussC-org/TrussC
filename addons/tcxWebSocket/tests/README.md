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

It also checks fragmented messages on the native client (RFC 6455 5.4) over
loopback: a core `TcpServer` answers the upgrade request and writes frames by
hand. It listens on the first free port from 23380 to 23399, below Linux's
ephemeral port range (`TcpServer` cannot report a port the OS picked).

- text in 3 fragments with a Ping in between -> one `onMessage` with the full
  text, and one masked Pong echoing the Ping;
- binary in 2 fragments, and 100 KB of text in 3 fragments -> one
  `onMessage` each, bytes intact;
- a first fragment only, then `disconnect()` and `connect()` -> the next
  message arrives on its own;
- a continuation with no message in progress, or a new Text frame during
  one -> Close 1002, then `onError`, then `onClose`;
- a frame, or fragments together, over the 64 MiB message limit -> Close
  1009, then `onError`, then `onClose`;
- an `onError` listener that calls `disconnect()` -> one `onClose`.

tcxWebSocket depends on tcxTls, which builds mbedTLS, so this harness has a
`daily-only` marker: CI runs it in the daily workflow
(`examples/build_all.py --addon-tests-only --include-daily`), not per PR. Run
it locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --include-daily --verbose
```
