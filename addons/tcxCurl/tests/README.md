# tcxCurl tests

Headless console test (no window) for the code behind
`HttpClient::setVerbose(true)`: which parts of curl's debug output are shown
as `<redacted>`. Also checks the default SSL option bits: Windows uses native
CAs and best-effort revocation without disabling revocation; other platforms
keep curl defaults. No network or display is needed.

`addons.make` lists tcxCurl, so the libcurl glue is compiled as well. On
Windows this builds curl from source with Schannel; macOS/Linux use the
system libcurl.

Response admission checks also cover lazy storage, complete boundary values,
whole-response rejection, JSON structure/working memory, cumulative rejection
counts and a single first-loss Warning. Linux additionally traces C++ allocation
peaks and release. SDK/libcurl C-heap allocations are outside that trace.

Run the Web font streaming fixtures (the actual EM_JS implementation, with a
real Node byte ReadableStream and WASM allocation observation) from the repository root:

```bash
node addons/tcxCurl/tests/test_font_stream.js
```

Line rules (`redactCredentialLine`):

- the values of `Authorization`, `Proxy-Authorization`, `X-Api-Key`,
  `Api-Key`, `X-Auth-Token` and `Cookie` header lines, any letter case, with surrounding space/tab variants;
- the same names on `[HTTP/2] [n] [name: value]` info lines (the value runs
  to the last `]`), and on the older `h2h3 [...]` / `h2 [...]` forms;
- curl's echo of an environment proxy with `user:password@`, also when curl
  cut the line: at 2047 characters with no `...` (a quote exactly at the cut
  included), at 2043 characters plus `...`, and `Unsupported proxy ...` error
  lines at 255 characters;
- `no_proxy` / `NO_PROXY` lines and a proxy value without `@` stay readable;
- the user name on `Proxy auth using ...` / `Server auth using ...` lines.

Replayed debug callback sequences (`formatVerbose`, `flushPendingHeaderOut`):

- a request header block split inside a credential line, or into pieces of
  1 to 9 bytes, gives the same output as the whole block;
- `Connection died, retrying`: the cut line is printed redacted with the
  "(request header output ended mid-line)" note before the info line, and
  the rest of that line is hidden;
- `Issue another request to this URL`: the next request's first line is shown;
- a tail that never completes is printed redacted with the note at the end of
  the transfer.

The expected strings follow curl's line formats. If a curl upgrade changes a
format, update `tcxCurl.h` and these cases together. Masking fixes add their
cases here.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
```

The stream fixture also checks default-reader compatibility, storage admission,
cancellation and retry. After building this addon, run the completion lifetime
probe with ASan and a synchronous Logger listener:

```bash
/home/toru/TrussC-dev/trussc-ai-tools/tcbuild.sh python3 addons/tcxCurl/tests/test_font_lifetime.py
```

The probe uses production completion/close functions and the real Logger. It
models the Font callback ownership edge; it does not run a browser. Android
Java/JNI changes have code-review coverage only in this environment.

The six credential headers use one C++ list for verbose redaction and redirect
filtering. The addon tests check both consumers; `tests-curl` exercises all six
on real same-origin and cross-origin hops. Verify the Java mirror and its use with:

```bash
python3 addons/tcxCurl/tests/test_redirect_policy.py
```
