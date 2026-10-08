# License

## TrussC

TrussC is licensed under the MIT License.

```
MIT License

Copyright (c) 2024-2025 tettou771 <tettou771@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Third-Party Libraries

TrussC includes or depends on the following third-party libraries. All use permissive open-source licenses suitable for commercial use.

This is the one list of third-party code in TrussC and the version the build uses. Other docs link here instead of repeating versions. Per-library provenance (the exact upstream commit, TrussC patches, how to update) stays in the files linked from the Version column.

- **Version**: the release the build fetches or the vendored copy states. `commit` is the upstream commit for code without releases (or a fork branch). `branch ... (not pinned)` means the build fetches the tip of that branch. `not recorded` means the vendored copy carries no version and its upstream commit was not recorded.
- **Pinned / vendored in**: the CMake file with the `FetchContent_Declare` or shader compiler pin, or the vendored path in the repo.
- **Checked on every pull request**: `python3 tools/check_dependencies.py` (CI job `header-state-check`) compares each row with the `GIT_TAG` / `URL` of its `FetchContent_Declare`, the shader compiler commit and host binary SHA-256 values, the version line or macros of its vendored copy, and the provenance file. Recorded SHA-256 values must agree with the build; `URL_HASH` must be a valid SHA-256 when present. It fails when a `FetchContent_Declare` has no row, a version differs, or a path no longer exists. A pull request that changes a dependency updates its row here.
- **Checked every week**: `.github/workflows/upstream-check.yml` asks each upstream for its latest release (or, for a commit, whether the vendored files changed upstream) and comments on one tracking issue, "Third-party updates available", when the set of entries with something newer changes. Incomplete checks appear in the run summary and preserve the last complete report. It never changes a version; a person reads the release notes and decides.

### Core Dependencies

| Library | Version | Pinned / vendored in | Upstream | License | Author/Organization |
|---------|---------|----------------------|----------|---------|---------------------|
| **sokol** | commit `082152c` (+ TrussC patches, see [TRUSSC_MODIFICATIONS.md](../core/include/sokol/TRUSSC_MODIFICATIONS.md)) | `core/include/sokol/` | https://github.com/floooh/sokol | zlib License | Andre Weissflog |
| **stb_image** | 2.30, commit `1cafe0e` (fork, branch `nv/all-fixes`, + 2 TrussC patches, see [stb/README.md](../core/include/stb/README.md)) | `core/include/stb/stb_image.h` | https://github.com/nvpro-samples/stb | Public Domain or MIT | Sean Barrett |
| **stb_image_write** | 1.16, commit `1ee679c` (+ 1 TrussC patch) | `core/include/stb/stb_image_write.h` | https://github.com/nothings/stb | Public Domain or MIT | Sean Barrett |
| **stb_perlin** | 0.5, commit `2bb4a0a` | `core/include/stb/stb_perlin.h` | https://github.com/nothings/stb | Public Domain or MIT | Sean Barrett |
| **stb_truetype** | 1.26, commit `6e9f34d` (+ 3 TrussC patches) | `core/include/stb/stb_truetype.h` | https://github.com/nothings/stb | Public Domain or MIT | Sean Barrett |
| **stb_vorbis** | 1.22, commit `dd0c5ec` (fork, branch `stb_vorbis-sezero`) | `core/include/stb_vorbis.c` | https://github.com/sezero/stb | Public Domain or MIT | Sean Barrett |
| **miniaudio** | 0.11.25 | `core/include/miniaudio.h` | https://github.com/mackron/miniaudio | Public Domain or MIT-0 | David Reid |
| **dr_wav** | 0.14.5 (bundled in miniaudio.h) | `core/include/miniaudio.h` | https://github.com/mackron/dr_libs | Public Domain or MIT-0 | David Reid |
| **dr_mp3** | 0.7.3 (bundled in miniaudio.h) | `core/include/miniaudio.h` | https://github.com/mackron/dr_libs | Public Domain or MIT-0 | David Reid |
| **dr_flac** | 0.13.3 (bundled in miniaudio.h) | `core/include/miniaudio.h` | https://github.com/mackron/dr_libs | Public Domain or MIT-0 | David Reid |
| **nlohmann/json** | 3.12.0 | `core/include/nlohmann/json.hpp` | https://github.com/nlohmann/json | MIT | Niels Lohmann |
| **pugixml** | 1.16 | `core/include/pugixml/` | https://github.com/zeux/pugixml | MIT | Arseny Kapoulkine |
| **cpp-httplib** | 0.18.3 | `core/include/impl/httplib.h` | https://github.com/yhirose/cpp-httplib | MIT | Yuji Hirose |
| **LZ4** | 1.10.0 | `core/include/lz4/` | https://github.com/lz4/lz4 | BSD 2-Clause | Yann Collet |
| **earcut.hpp** | not recorded | `core/include/earcut/earcut.hpp` | https://github.com/mapbox/earcut.hpp | ISC | Mapbox |

### Build Tools

| Library | Version | Pinned / vendored in | Upstream | License | Author/Organization | SHA-256 |
|---------|---------|----------------------|----------|---------|---------------------|---------|
| **sokol-shdc (sokol-tools-bin)** | commit `11d0cf678105d614d675e6d9bd2aaf3eeff12f8c` (2026-08-29T14:15:01Z) | `core/cmake/trussc_shaders.cmake` | https://github.com/floooh/sokol-tools-bin | zlib License | Andre Weissflog | osx: `8b4a6ac1172ec0d90dd41d611067d5e87a51e78dc28acb216cfc341d880b1d78`<br>osx_arm64: `92db37975ad7ff3c3c9bc27cba1503287377cb287ebabf60d1c6b597abfa3244`<br>linux: `ed35e89ef381d521a499096ed4ada85e4d135d8011e151cca6b7d893c43b21df`<br>linux_arm64: `446b4bcea0c81d3ae529bc0d93533ea661b017f5b9ec2b2293a4c85f5fdcb639`<br>win32: `bd616287f9ea689d53c6d260e443ee733e61ae1b73a9b37adc482ead0364d561` |

Updates must change the shader compiler commit and all five host binary hashes together, preserving the `metal_sim` and `wgsl` outputs used by TrussC.

### Addon Dependencies (Optional)

These libraries are only included if you use the corresponding addon.

| Library | Version | Pinned / vendored in | Upstream | License | Author/Organization | Addon |
|---------|---------|----------------------|----------|---------|---------------------|-------|
| **Dear ImGui** | 1.92.9b (+ TrussC patch, see [TRUSSC_MODIFICATIONS.md](../addons/tcxImGui/src/imgui/TRUSSC_MODIFICATIONS.md)) | `addons/tcxImGui/src/imgui/` | https://github.com/ocornut/imgui | MIT | Omar Cornut | tcxImGui |
| **sokol_imgui.h** | not recorded | `addons/tcxImGui/src/sokol_imgui.h` | https://github.com/floooh/sokol | zlib License | Andre Weissflog | tcxImGui |
| **mbedTLS** | 3.6.7 | `addons/tcxTls/CMakeLists.txt` | https://github.com/Mbed-TLS/mbedtls | Apache-2.0 or GPL-2.0-or-later (dual-licensed) | Arm Limited | tcxTls |
| **libcurl** | 8.22.0 (Windows; macOS/Linux use system libcurl) | `addons/tcxCurl/CMakeLists.txt` | https://github.com/curl/curl | curl License (MIT-style) | Daniel Stenberg and contributors | tcxCurl |
| **libremidi** | 5.4.3 | `addons/tcxMidi/CMakeLists.txt` | https://github.com/celtera/libremidi | BSD 2-Clause (parts from RtMidi: MIT) | Jean-Michaël Celerier | tcxMidi |
| **Box2D** | 2.4.1 | `addons/tcxBox2d/CMakeLists.txt` | https://github.com/erincatto/box2d | MIT | Erin Catto | tcxBox2d |
| **cgltf** | 1.15 | `addons/tcxGltf/CMakeLists.txt` | https://github.com/jkuhlmann/cgltf | MIT | Johannes Kuhlmann | tcxGltf |
| **tinyobjloader** | commit `45636bd` (branch `release`, + 2 TrussC patches, see [PROVENANCE.md](../addons/tcxObj/PROVENANCE.md)) | `addons/tcxObj/src/tiny_obj_loader.h` | https://github.com/tinyobjloader/tinyobjloader | MIT | Syoyo Fujita and contributors | tcxObj |
| **Snappy** | 1.2.1 | `addons/tcxHap/CMakeLists.txt` | https://github.com/google/snappy | BSD 3-Clause | Google Inc. | tcxHap |
| **HAP** | commit `d847f6bbd3be88575dd4ef33a877243780e3be76` (2024-07-25) | `addons/tcxHap/CMakeLists.txt` | https://github.com/Vidvox/hap | BSD 2-Clause | Tom Butterworth, Vidvox LLC | tcxHap |
| **bcdec** | 0.985 | `addons/tcxHap/src/impl/bcdec.h` | https://github.com/iOrange/bcdec | MIT or Public Domain (dual-licensed) | Sergii "iOrange" Kudlai | tcxHap |
| **Lua** | 5.4.9 | `addons/tcxLua/lua/` | https://github.com/lua/lua | MIT | Lua.org, PUC-Rio | tcxLua |
| **sol2** | 3.5.0 | `addons/tcxLua/include/sol/` | https://github.com/ThePhD/sol2 | MIT | ThePhD | tcxLua |
| **LuaJIT** | 2.1 (rolling), commit not recorded | `addons/tcxLua/LuaJIT/` | https://github.com/LuaJIT/LuaJIT | MIT | Mike Pall | tcxLua |
| **luajit-cmake** | commit `94444a6` | `addons/tcxLua/luajit-cmake/` | https://github.com/zhaozg/luajit-cmake | MIT | George Zhao (zhaozg) | tcxLua |

> **Note**: Box2D v2.3.x以前はzlib Licenseでした。TrussCはv2.4.0以降 (MIT) を使用しています。

---

## License Details

### zlib License (sokol)

```
zlib License

Copyright (c) 2017 Andre Weissflog

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not
    be misrepresented as being the original software.

    3. This notice may not be removed or altered from any source distribution.
```

### MIT License (Dear ImGui, nlohmann/json, pugixml, cpp-httplib, Box2D, cgltf, tinyobjloader, Lua, sol2, LuaJIT, luajit-cmake)

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Public Domain / MIT (stb)

stb ライブラリはPublic Domain（Unlicense）として公開されていますが、Public Domainを認めない法域のためにMITライセンスも選択可能です（dual-licensed）。

**Public Domain (Unlicense):**
```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
```

**MIT License (alternative):**

MITライセンス（Copyright (c) 2017 Sean Barrett）と Public Domain の全文は [core/include/stb/LICENSE](../core/include/stb/LICENSE) にあります。各 stb ファイルの取り込み元・コミット・TrussC 側のパッチは [core/include/stb/README.md](../core/include/stb/README.md) にまとめています。

### Public Domain / MIT-0 (dr_libs, miniaudio)

dr_libs と miniaudio はPublic Domain（Unlicense）または MIT-0（MIT No Attribution）のいずれかを選択できます。

**MIT-0 (MIT No Attribution):**
```
MIT No Attribution

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Apache-2.0 / GPL-2.0-or-later (mbedTLS)

mbedTLS is dual-licensed. You may choose either Apache-2.0 or GPL-2.0-or-later.

For commercial projects, Apache-2.0 is typically preferred as it has fewer restrictions.

See the full license text at:
- Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
- GPL-2.0: https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

### MIT or Public Domain (bcdec)

bcdec is dual-licensed under MIT or Public Domain. You may choose either license.

**bcdec:**
```
bcdec - BC texture decompression library
Copyright (c) 2020 Sergii "iOrange" Kudlai

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### BSD 2-Clause License (LZ4, HAP, libremidi)

**LZ4:**
```
LZ4 Library
Copyright (c) 2011-2020, Yann Collet
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

**HAP:**
```
Copyright (c) 2012-2013, Tom Butterworth and Vidvox LLC. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### BSD 3-Clause License (Snappy)

```
Copyright 2011, Google Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
    * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### ISC License (earcut.hpp)

```
ISC License

Copyright (c) 2015, Mapbox

Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
```

### libremidi

libremidi's own code is under the BSD 2-Clause License (Copyright (c) 2017-2023, Jean-Michaël Celerier & the libremidi contributors). It is derived from RtMidi (MIT) and ModernMidi (BSD 2-Clause). Full text: [LICENSE.md](https://github.com/celtera/libremidi/blob/master/LICENSE.md).

### curl License (libcurl)

libcurl is under the curl License, an MIT-style license (Copyright (c) 1996-2026 Daniel Stenberg, and many contributors). Full text: [COPYING](https://github.com/curl/curl/blob/curl-8_22_0/COPYING).

---

## Summary

All dependencies use permissive open-source licenses that allow:
- Commercial use
- Modification
- Distribution
- Private use

The only requirement common to all is to include the copyright notice and license text when redistributing the source code.

For binary distributions, most of these licenses (MIT, zlib, Public Domain, MIT-0) do not require attribution in the binary itself, though it is appreciated.

If you use **mbedTLS** (tcxTls addon) and choose GPL-2.0, additional obligations apply to derivative works.
