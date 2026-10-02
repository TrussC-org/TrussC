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

Windows では `find_package(CURL)` は使用せず、CMake が常に libcurl 8.12.1 をソースから取得してスタティックライブラリとしてビルドします。TLS には Windows ネイティブの **Schannel** を使用するため、OpenSSL のインストールは不要です。

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
| `isReachable()` | Check if server responds |

### `HttpResponse`

| Member | Description |
|--------|-------------|
| `statusCode` | HTTP status code (0 if connection failed) |
| `body` | Response body as string |
| `error` | Error message (empty on success) |
| `ok()` | `true` if status is 2xx |
| `json()` | Parse body as JSON |
