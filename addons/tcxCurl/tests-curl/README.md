# tcxCurl tests-curl

Headless console test (no window) for `HttpClient` over a real TLS
connection. It links tcxCurl (libcurl), so it is kept apart from the
curl-free `../tests` and carries a `daily-only` marker.

The HTTPS peer is a small Python `http.server` on 127.0.0.1 (port chosen by
the OS). Its key and self-signed certificate are made with `openssl req` at
startup in a temporary directory. Nothing leaves the machine and no key is
committed. Without `openssl` or a Python with `ssl` on PATH, the test prints
SKIP and passes.

`setTlsCACertificate()` (#401):

- without a CA, `get()` and `uploadFile()` to the self-signed server fail;
- with the server's certificate as the CA, `get()` and `uploadFile()` return
  200;
- with another self-signed certificate, or text that is not a PEM, the request
  fails (verification stays on);
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
