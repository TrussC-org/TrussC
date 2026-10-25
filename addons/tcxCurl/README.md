# tcxCurl

HTTPS client addon for TrussC, powered by [libcurl](https://curl.se/libcurl/).

## When to use tcxCurl

TrussC includes [cpp-httplib](https://github.com/yhirose/cpp-httplib) for HTTP communication (used internally by the MCP server). However, **cpp-httplib only supports HTTPS via OpenSSL**, which is not bundled with TrussC.

Use **tcxCurl** when you need:
- **HTTPS** connections (TLS/SSL)
- File uploads (multipart)
- Communication with external APIs that require HTTPS

If you only need plain **HTTP**, you can use `httplib::Client` directly from `<impl/httplib.h>` without any addon.

## Platform Setup

| Platform | libcurl | 備考 |
|----------|---------|------|
| **macOS** | システム同梱 | 追加インストール不要 |
| **Linux** | `sudo apt install libcurl4-openssl-dev` | |
| **Windows** | **自動ダウンロード** | CMake の FetchContent で curl をソースからビルド（Schannel 使用） |

On Windows the bundled libcurl is built HTTP(S)-only; other platforms use the system libcurl with its own protocol set.

Windows同梱のlibcurlはHTTP(S)専用で、他のプラットフォームはシステムのlibcurlが対応するプロトコルを使用します。

### Windows での動作

Windows では `find_package(CURL)` が失敗した場合、CMake が自動的に libcurl（バージョンは [docs/LICENSE.md](../../docs/LICENSE.md#third-party-libraries)）をダウンロードしてスタティックライブラリとしてビルドします。TLS には Windows ネイティブの **Schannel** を使用するため、OpenSSL のインストールは不要です。

手動で curl をインストールする場合は vcpkg も使えます:

```bash
vcpkg install curl:x64-windows
cmake -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake ..
```

Windows では既定で OS の証明書ストアを使用し、失効確認はブラウザと同様に best-effort で行います。`setTlsCACertificate()` で PEM を指定すると、その PEM が OS の証明書ストアを置き換え、失効確認は引き続き best-effort で行います。証明書とホスト名の検証は常に有効です。

### WASM (Emscripten)

Emscripten Fetch API support is planned but not yet implemented.

## Usage

```cpp
#include <TrussC.h>
#include <tcxCurl.h>

using namespace tc;
using namespace tcx;

// Create client
HttpClient client;
client.setBaseUrl("https://api.example.com");

// GET
auto res = client.get("/endpoint");
if (res.ok()) {
    auto data = res.json();  // nlohmann::json
}

// POST JSON
auto res = client.post("/endpoint", json{{"key", "value"}});

// Bearer token authentication
client.setBearerToken("your-token-here");

// Custom headers
client.addHeader("X-Custom", "value");

// File upload (multipart)
auto res = client.uploadFile("/upload", "/path/to/file.png");
```

### Devices with a self-signed certificate

Projectors, cameras, NAS boxes and similar devices often serve HTTPS with a
self-signed certificate that is not in the OS trust store. Give `HttpClient`
that certificate (or the CA that signed it) as PEM text:

```cpp
HttpClient http;
http.setBaseUrl("https://192.168.0.50");
http.setTlsCACertificate(loadTextFile("projector-ca.pem"));  // PEM; "" = OS default store
auto res = http.get("/api/status");
```

- The server certificate and host name are still verified. While a PEM is
  set, it is the only trust source: the OS default store is not used.
- `request()`, the typed helpers and `uploadFile()` all use it.
- On Windows, revocation is checked best-effort while a PEM is set, so a
  certificate without a CRL/OCSP URL is accepted.
- If the libcurl in use cannot take a PEM from memory (`CURLOPT_CAINFO_BLOB`,
  libcurl 7.77.0+ with a TLS backend that supports it), the request fails and
  `res.error` says so.
- Android: a request with a CA set fails with `res.error` for now.
- There is no option to turn verification off.

## API

### `HttpClient`

| Method | Description |
|--------|-------------|
| `setBaseUrl(url)` | Set base URL for all requests |
| `get(path)` | GET request |
| `post(path, json)` | POST with JSON body |
| `postRaw(path, body, contentType)` | POST with raw body |
| `del(path)` | DELETE request |
| `uploadFile(path, filePath)` | Multipart file upload |
| `setResponseLimit(bytes)` | Set per-client response limit (default 64 MiB) |
| `setFollowRedirects(follow)` | Follow redirects by default; `false` returns the 3xx response |
| `setBearerToken(token)` | Set Bearer authentication |
| `addHeader(key, value)` | Add custom header |
| `clearHeaders()` | Remove all custom headers |
| `setTlsCACertificate(pem)` | Verify the server against this PEM instead of the OS store (`""` = OS store) |
| `isReachable()` | Check if server responds |

### `HttpResponse`

| Member | Description |
|--------|-------------|
| `statusCode` | HTTP status code (0 if connection failed) |
| `body` | Response body as string |
| `error` | Error message (empty on success) |
| `ok()` | `true` if status is 2xx |
| `json()` | Parse body as JSON |

### Response limits

`setResponseLimit(size_t bytes)` sets a per-client limit, default **64 MiB
(67,108,864 bytes)**. It covers the whole request, including intermediate
redirect bodies and upload replies, and counts decoded bytes. Wire bytes are
independently limited to the same value. The bound exists because a server
controls the response size. Exceeding it fails the whole response:
`statusCode` is 0, `body` is empty and `error` is set. The first body-limit loss
logs one Warning shared across clients; subsequent losses still fail.
Passing 0 preserves the previous limit and logs a once-only Warning shared
across clients.

Storage grows only with what arrives. Peak initialized payload is about the
response size on desktop and about twice on Android. Desktop allocated/reserved
capacity can temporarily approach twice the response size during final copying;
these figures exclude backend networking allocations and allocator retention.
There is no stream-to-file API yet. The setter has no effect on the Web stub;
Android multipart upload remains unsupported.

レスポンス上限はクライアントごとに既定64 MiBです。超過時は全体が失敗し、Webのスタブには影響しません。

### Redirects

Redirects are followed by default on every backend (**v0.8 change**).
Call `setFollowRedirects(false)` to receive the 3xx itself. The Web backend
remains a stub. At most **5 redirects** are followed, with targets limited to
`http` and `https` and no `https` to `http` downgrade.

When the origin changes (scheme, host, port or IPv6 zone), these credential
headers are omitted for the remaining hops of that request: `Authorization`,
`Proxy-Authorization`, `X-Api-Key`, `Api-Key`, `X-Auth-Token`, `Cookie`.

v0.8からリダイレクトを既定で追跡します。`setFollowRedirects(false)`で3xxを受け取れます。

### Request headers

Header names are trimmed of surrounding ASCII whitespace and must be valid
HTTP field names. Values must not contain CR, LF or NUL. An invalid header
fails the request with `response.error` set, `statusCode` 0 and an empty body;
nothing is sent. Names and values share one Warning per client, logged on its
first invalid-header request. This also applies to `setBearerToken()` and the
Content-Type argument sent with a request body. The Web client remains a stub.

ヘッダー名は前後の空白を除去して検証します。不正な名前やCR・LF・NULを含む値は送信前に失敗します。

### JSON responses

`response.json()` parses within **16 MiB input, depth 64 and 1,000,000 nodes
(including keys)**, plus an internal **128 MiB working budget** that can reject
input below those individual limits. Budget rejection returns an empty object
and logs a Warning only on the first JSON-budget loss, shared across clients.
Empty or invalid JSON also returns an empty object; it does not log a budget
Warning unless a budget was exceeded. Parsing does not change the response's
status, body or error.

JSON解析の上限超過時は空のオブジェクトを返し、最初の超過時のみWarningを記録します。
