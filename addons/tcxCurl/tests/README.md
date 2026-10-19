# tcxCurl tests

Headless console test (no window) for the code behind
`HttpClient::setVerbose(true)`: which parts of curl's debug output are shown
as `<redacted>`. Also checks the default SSL option bits: Windows uses native
CAs and best-effort revocation without disabling revocation; other platforms
keep curl defaults. No network or display is needed.

`addons.make` lists tcxCurl, so the libcurl glue is compiled as well. On
Windows this builds curl from source with Schannel; macOS/Linux use the
system libcurl.

Line rules (`redactCredentialLine`):

- the values of `Authorization`, `Proxy-Authorization`, `X-Api-Key` and
  `Api-Key` header lines, any letter case;
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
