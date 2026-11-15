# libcurl provenance

Windows builds fetch the unmodified upstream release through `CMakeLists.txt`.
macOS and Linux use system libcurl; Android and Web use their existing platform
HTTP backends.

| Item | Value |
| --- | --- |
| Version | 8.23.0 |
| Upstream tag | `curl-8_23_0` |
| Archive | https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz |
| SHA-256 | `8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5` |
| License | curl License; upstream `COPYING`, copyright 1996–2026 Daniel Stenberg and contributors |
| Release notes | Upstream archive `RELEASE-NOTES` |
| TrussC source patches | None |

## Build configuration for 8.23.0

Keep the static library, Schannel, and existing TLS verification options.
Upstream removed `HTTP_ONLY`; explicit `CURL_DISABLE_*` options now disable the
same protocols it did, including IPFS and WebSocket. LDAP/LDAPS and libssh2
remain disabled separately. Upstream has removed SMB support. Remove the unused `CURL_USE_LIBIDN2`, `CURL_USE_LIBRTMP`, and
`CURL_USE_SECTRANSP` cache entries; `USE_LIBIDN2` remains the supported IDN option.
No upstream source patch needs reapplication.

## Updating

Read the next release's `RELEASE-NOTES` and `COPYING`, verify the archive SHA-256,
and update the FetchContent URL/hash, this record, and `docs/LICENSE.md` together.
Check every configured option against upstream CMake, and run the dependency
checker/tests and both tcxCurl headless suites. Verify the Windows Schannel
build and TLS behavior on Windows.
