# tcxWebSocket tests

Headless console test (no window; loopback connections only). It checks how the web build
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

Frame header validation (#514) uses the same in-process server:

- each RSV bit, every reserved opcode, fragmented Close/Ping/Pong, control
  payloads over 125 bytes, and masked server frames -> exactly one Close
  1002, one `onError` naming the violation, and one `onClose`, in that order;
- both complete frames and headers without their advertised payload are
  rejected; oversized control frames use 1002 even above the message limit;
- valid 125-byte Ping/Pong/Close frames and unfragmented text/binary keep
  their existing behavior, including a masked Pong echoing the Ping.

These checks wait on conditions with deadlines, then join the transport
threads before asserting exact event and frame counts.

Handshake deadline and reconnecting from the events (#262). The test pumps
the update event, as the app's frame loop would, for the 101 deadline:

- `ws://` to a server that accepts and never answers the upgrade request,
  with `setHandshakeTimeout(1)` -> disconnect, then `onError`, then
  `onClose`; the error callback already sees `Disconnected`;
- a 101 timeout `onError` listener that reconnects or destroys the client
  -> no stale `onClose`, and a replacement connection can open;
- `setConnectTimeout(2)` forwards a separate TCP deadline for ws:// and wss://;
- `wss://` to a server that accepts and never speaks TLS -> the same,
  through `TlsClient`'s handshake deadline;
- `ws://` to a server that closes the connection before the `101` ->
  `onClose` only, and no deadline `onError` after it;
- an inline `onClose` listener that calls `connect()` when the server drops
  the connection, and when it sends Close -> 20 reconnects, each reaching
  `onOpen`;
- `wss://`: an inline `onError` listener that calls `connect()` after each
  failed TLS handshake -> 20 reconnects, each reaching the server.

`connect()` from those listeners destroys the `TcpClient` / `TlsClient`
whose receive thread runs them; build with AddressSanitizer
(`-DCMAKE_CXX_FLAGS=-fsanitize=address`) to check that the thread does not
read it afterwards.

tcxWebSocket depends on tcxTls, which builds mbedTLS, so this harness has a
`daily-only` marker: CI runs it in the daily workflow
(`examples/build_all.py --addon-tests-only --include-daily`), not per PR. Run
it locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --include-daily --verbose
```

The tcxTls harness also checks a silent HTTP upgrade after successful TLS,
20 successful wss:// connections reopened inline from `onClose`, and
reconnection from `onError` after 20 self-signed certificate failures with
certificate verification enabled. On Linux it also fills a loopback accept
queue to check the inherited TCP deadline through TlsClient, with threads
enabled and disabled.
