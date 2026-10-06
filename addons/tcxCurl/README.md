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
