# TrussC Security Notes

Security-relevant usage guidance for TrussC and its official addons. If you ship
an application built on TrussC, please read this before accepting any file,
URL, or network input from outside your own build.

> This doc focuses on **what you need to know as an application author**.
> Implementation-level audit findings and their fixes live in the repo's commit
> history (prefix `fix(…)` / `deps:`).

---

## Untrusted input — what is and isn't safe

TrussC's parsers and loaders fall into three tiers. Your application's threat
model should match the tier of each input source.

| Tier | Meaning | Examples |
|:-----|:--------|:---------|
| **Safe** | Hardened or defense-in-depth covered. Okay for untrusted input. | `Xml` (pugixml), `Json` (nlohmann), `OscReceiver`, `TcpClient` byte stream. |
| **Caution** | Works for well-formed files. Malformed or crafted input may cause crashes or resource exhaustion. Okay for files you ship; review before exposing to network/drop-in. | `Image::load` (stb_image), `VideoPlayer`, `tcxHap` MOV parser, `tcxObj` loader, `tcxGltf`. |
| **Unsafe** | Upstream does not claim memory safety. **Must not** be fed attacker-controlled data. | `Font::load` / anything going through `stb_truetype`. |

### `stb_truetype` (Font) — do not load untrusted fonts

`core/include/stb/stb_truetype.h` ships with this note from the author:

> NO SECURITY GUARANTEE — This library does no range checking of the offsets
> found in the file, meaning an attacker can use it to read arbitrary memory.

TrussC's `Font` class and anything that renders text through it uses
`stb_truetype`. This is fine for fonts bundled with your application. It is
**not** fine if your application lets a user drop in a font, downloads a font
from a URL, or otherwise loads fonts the author did not ship.

If you need to accept user-supplied fonts, options in order of preference:

1. Reject the feature (only ship fonts you built with).
2. Run font rendering in a sandbox/subprocess.
3. Replace the backend with FreeType (no built-in support today — patches
   welcome; tracked informally in `docs/ROADMAP.md`).

### `tcxHap` MOV parser

Best-effort validation is applied (atom size bounds, progress checks). Crafted
files can still trigger early-return on parse, so make sure your app tolerates
`MovParser::open()` returning `false`. Do not trust any field read from the
file until it has been validated.

### `tcxObj` / `tcxGltf`

Mesh loaders. Index/offset validation is limited; crafted files can cause
out-of-bounds reads. Treat these as "caution" tier.

---

## TLS (`tcxTls`, `tcxWebSocket`)

### Certificate verification is ON by default

`TlsClient` and `WebSocketClient` verify the server certificate chain against
a trust anchor. A failed verification aborts the handshake.

### Where trust anchors come from (priority order)

On the first handshake, `TlsClient` resolves a trust anchor set using the first
of the following that succeeds:

1. **User-supplied PEM** — if you called `setCACertificate()` /
   `setCACertificateFile()` (on `TlsClient`) or `setTlsCACertificate()` (on
   `WebSocketClient`). This replaces the default set; nothing else is consulted.
   Use this for private CAs or when you want to pin to a specific issuer.
2. **OS trust store** — on POSIX, a well-known bundle path
   (`/etc/ssl/cert.pem`, `/etc/ssl/certs/ca-certificates.crt`,
   `/etc/pki/tls/certs/ca-bundle.crt`, …). On Windows, the system `ROOT`
   store via `CertOpenSystemStoreW`. This is the typical path and picks up
   OS updates automatically.
3. **Bundled Mozilla roots** — a copy of `https://curl.se/ca/cacert.pem` is
   embedded in `tcxTls` at build time and used as a fallback when no OS store
   is readable. Refresh with `addons/tcxTls/scripts/update_ca_bundle.sh`; this
   should be done every few months so the bundled set tracks Mozilla's
   distrust decisions.

The source that was used is logged at notice level on first connect, e.g.:

```
[notice] TlsClient: loaded 150 CAs from /etc/ssl/cert.pem
[notice] TlsClient: loaded 130 CAs from bundled cacert.pem (Certificate data from Mozilla last updated on: …)
```

If all three sources fail, an error is logged and every subsequent handshake
will fail until an explicit PEM is provided or verification is turned off.

### Opting out for development

Self-signed certs on localhost, staging boxes, etc.:

```cpp
// tcxTls
TlsClient tls;
tls.setVerifyNone();   // skip verification entirely
tls.connect(host, port);

// tcxWebSocket
WebSocketClient ws;
ws.setTlsVerifyNone();        // skip verification
// or
ws.setTlsCACertificate(pem);  // supply a custom CA (PEM string)
ws.connect("wss://internal.example/");
```

Never ship `setVerifyNone()` to end users — it silently allows MITM.

### Upstream mbedTLS version

`tcxTls` pins mbedTLS to the v3.6.x LTS branch via FetchContent. When bumping
to a newer patch release, also update the version row in `docs/LICENSE.md` and
the note in `docs/ROADMAP.md`.

---

## Safe defaults for HTTP and WebSocket

- `tcxCurl` uses libcurl with `CURLOPT_SSL_VERIFYPEER` / `VERIFYHOST` at their
  defaults (both enabled). Automatic redirect following is off.
- `tcxWebSocket` refuses to silently downgrade `wss://` to an unverified
  session. A user who wants that must call `setTlsVerifyNone()` explicitly.

---

## Command execution / path handling

- `getDataPath(p)` does **not** reject `..` segments. Do not pass attacker-
  controlled strings to it.
- `HotReloadHost` invokes the CMake build tool via `std::system()`; the build
  directory path is derived from the canonicalized executable path, not user
  input. Only a developer who places their build under a path containing shell
  metacharacters is exposed.
- `IdeHelper` in `trusscli` executes `system()` with the imported project
  path. Do not run `trusscli` against project paths you don't trust.

---

## Threading & memory safety

Not adversarial-input related, but a memory-safety pitfall worth knowing: the
**Node tree and GPU state are owned by the main thread**. Touching them from a
worker thread — most commonly inside a network `onReceive` handler, which fires
on the receive thread — is a data race that crashes. Use `runOnMainThread(...)`
or an `Event` listener registered with `Deliver::Main`. See
[ARCHITECTURE.md → 5.E Threading](ARCHITECTURE.md#e-threading) for the full
contract (which callbacks run off the main thread, the safe patterns, and the
debug-only main-thread assert).

---

## Reporting

Security issues should be reported privately via GitHub Security Advisories on
the repo, or by email to the maintainer (see `README.md`). Please do not open
public issues for security bugs.
