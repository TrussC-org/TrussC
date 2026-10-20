# tcxCurl tests-curl

Headless console test (no window) for `HttpClient` over a real TLS
connection. It links tcxCurl (libcurl), so it is kept apart from the
curl-free `../tests` and carries a `daily-only` marker.

The HTTPS peer is a small Python `http.server` on 127.0.0.1 (port chosen by
the OS). Its key and self-signed certificate are made with `openssl req` at
startup with an explicit SHA-256 signature in a temporary directory. The
loopback server skips DNS lookup, and startup failures include Python's
diagnostic output. Nothing leaves the machine and no key is committed.
Without `openssl` or a Python with `ssl` on PATH, the handshake checks print
SKIP; the option checks still determine the exit status.

Before starting the peer, a libcurl call trace checks both request paths:
CA blob and replacement of the default CA file/directory, unchanged empty-PEM
options, and explicit errors before transfer when a TLS option is unsupported.
On Windows it also checks best-effort revocation without native CA. These
checks run even when the external test tools are unavailable.

`setTlsCACertificate()` (#401):

- without a CA, `get()` and `uploadFile()` to the self-signed server fail;
- with the server's certificate as the CA, `get()` and `uploadFile()` return
  200;
- with another self-signed certificate, or text that is not a PEM, the request
  fails (verification stays on);
- a trusted certificate for a different host name still fails;
- an empty string goes back to the OS default store, and the request fails
  again.

On Linux the PEM must be taken. On other platforms, if the system libcurl
cannot take a PEM from memory, the request must fail with an error naming
`setTlsCACertificate`; the test prints a NOTE line.

CI builds and runs this in the daily workflow
(`examples/build_all.py --addon-tests-only --include-daily`); the per-PR lane
skips it. A non-zero exit fails the job. Run it locally with:

```bash
trusscli run -p .          # from this directory
```
# Response admission

The local HTTPS fixture also exercises complete response boundaries, decoded
responses, streaming responses, metadata admission, upload replies, redirects,
incomplete transfers and timeout/retry cleanup. Linux checks the peak and release
of C++-owned response allocations. These checks do not measure libcurl/TLS C-heap
allocations or Android SDK allocations.

Additional loopback cases cover response framing/trailers, bodyless HTTP
semantics, backend header-loss reporting, per-client limits, upload replies,
and invalid-zero setter input. C++ allocation tracing counts reserved string
capacity along with receive chunks.

Redirect fixtures start two plain HTTP listeners and one local HTTPS listener,
all on 127.0.0.1 with ephemeral ports. They cover default following, explicit
false, HTTP(S) target policy, HTTPS downgrade refusal and HTTP upgrade, all six
credential headers across same/different origins, neutral headers, POST method
rules (301/302/303/307/308), PUT/HEAD on 303, multipart 303/307, five-hop success,
sixth-hop failure, intermediate body admission and cumulative metadata.
App-supplied initial file URLs are tested independently of the HTTPS tools,
including escaped filenames and binary/CRLF data. Failures include the URL,
error and received/file sizes. If the linked libcurl does not list FILE support,
these checks print `N/A: this libcurl has no FILE support` and are skipped.
The bundled Windows libcurl is HTTP(S)-only. Initial URLs remain unrestricted
by the client; redirect targets are restricted to HTTP(S).

Request-header regressions cover all six credential names with surrounding
space/tab and mixed case: same-origin peers receive trimmed names, and other
origins receive no credential headers. Invalid names fail request/upload before
any perform call. A transport trace drives the production redirect loop with
real curl URL parsing to check same/changed scoped IPv6 zones; it never contacts
link-local interfaces. Body budgets are cumulative across hops: 4+4 bytes fail
under limit 4, while 2+2 publishes the final two bytes. Empty gzip bodies also
check the independent cumulative wire budget.

Zone traces extract scopes from the original URL and raw Location header.
Actual curl zone parsing is printed as information because curl versions differ.
Hard policy checks also inject a decoding parser and normalized effective/redirect
URLs, checking escaped zones, parser disagreement, relative/network-path targets,
and missing raw evidence. Plain identical zones keep credentials only when the
raw and parsed scope texts agree.

Invalid name/value tests also query a loopback peer's accepted-connection count
before and after request/upload attempts, asserting that it receives nothing.
Value cases cover CR, LF and embedded NUL at the start, middle and end through
addHeader, setBearerToken and Content-Type arguments for requests with a body.
Names and values share one Warning per client
across retries, clearing headers and a successful request; fresh clients warn
independently. Warning text contains no app values. Android validation ordering
is covered by source review and test_redirect_policy.py, without runtime claims.
