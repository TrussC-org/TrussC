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

### Core Dependencies

| Library | Version | License | Author/Organization | License URL |
|---------|---------|---------|---------------------|-------------|
| **sokol** | - | zlib License | Andre Weissflog | [LICENSE (in headers)](https://github.com/floooh/sokol/blob/master/sokol_app.h) |
| **Dear ImGui** | 1.92.6 | MIT | Omar Cornut | [LICENSE.txt](https://github.com/ocornut/imgui/blob/master/LICENSE.txt) |
| **stb_image** | 2.30 | Public Domain (dual-licensed under MIT) | Sean Barrett | [LICENSE (in README)](https://github.com/nothings/stb?tab=readme-ov-file#whats-the-license) |
| **stb_image_write** | 1.16 | Public Domain (dual-licensed under MIT) | Sean Barrett | [LICENSE (in README)](https://github.com/nothings/stb?tab=readme-ov-file#whats-the-license) |
| **stb_truetype** | 1.26 | Public Domain (dual-licensed under MIT) | Sean Barrett | [LICENSE (in README)](https://github.com/nothings/stb?tab=readme-ov-file#whats-the-license) |
| **stb_vorbis** | 1.22 | Public Domain (dual-licensed under MIT) | Sean Barrett | [LICENSE (in README)](https://github.com/nothings/stb?tab=readme-ov-file#whats-the-license) |
| **dr_mp3** | 0.6.39 | Public Domain or MIT-0 | David Reid | [LICENSE (in source)](https://github.com/mackron/dr_libs/blob/master/dr_mp3.h) |
| **dr_wav** | 0.13.16 | Public Domain or MIT-0 | David Reid | [LICENSE (in source)](https://github.com/mackron/dr_libs/blob/master/dr_wav.h) |
| **miniaudio** | 0.11.21 | Public Domain or MIT-0 | David Reid | [LICENSE (in README)](https://github.com/mackron/miniaudio?tab=readme-ov-file#license) |
| **nlohmann/json** | 3.11.3 | MIT | Niels Lohmann | [LICENSE.MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT) |
| **pugixml** | 1.15 | MIT | Arseny Kapoulkine | [LICENSE.md](https://github.com/zeux/pugixml/blob/master/LICENSE.md) |
| **cpp-httplib** | 0.18.3 | MIT | Yuji Hirose | [LICENSE](https://github.com/yhirose/cpp-httplib/blob/master/LICENSE) |

### Addon Dependencies (Optional)

These libraries are only included if you use the corresponding addon.

| Library | Version | License | Author/Organization | Addon | License URL |
|---------|---------|---------|---------------------|-------|-------------|
| **mbedTLS** | 3.6.2 | Apache-2.0 or GPL-2.0-or-later (dual-licensed) | Arm Limited | tcxTls | [LICENSE](https://github.com/Mbed-TLS/mbedtls/blob/development/LICENSE) |
| **Box2D** | 2.4.1 | MIT (v2.4.0以降) | Erin Catto | tcxBox2d | [LICENSE](https://github.com/erincatto/box2d/blob/main/LICENSE) |
| **bc7enc** | - | MIT or Public Domain (dual-licensed) | Richard Geldreich, Jr. | tcxTcv | [LICENSE (in source)](https://github.com/richgel999/bc7enc_rdo) |
| **bcdec** | 0.97 | MIT or Public Domain (dual-licensed) | Sergii "iOrange" Kudlai | tcxTcv, tcxHap | [LICENSE (in source)](https://github.com/iOrange/bcdec) |
| **LZ4** | 1.10.0 | BSD 2-Clause | Yann Collet | tcxTcv | [LICENSE](https://github.com/lz4/lz4/blob/dev/lib/LICENSE) |
| **Snappy** | 1.2.1 | BSD 3-Clause | Google Inc. | tcxHap | [COPYING](https://github.com/google/snappy/blob/main/COPYING) |
| **HAP** | - | BSD 2-Clause | Tom Butterworth, Vidvox LLC | tcxHap | [LICENSE](https://github.com/Vidvox/hap/blob/master/LICENSE) |
| **Lua** | 5.4.8 | MIT (v5.0 or later) | Lua.org, PUC-Rio | tcxLua | [LICENSE](https://www.lua.org/license.html) |
| **LuaJIT** | 2.1 | MIT | Mike Pall | tcxLua | [COPYRIGHT](https://github.com/LuaJIT/LuaJIT/blob/v2.1/COPYRIGHT) |
| **luajit-cmake** | [67637d0](https://github.com/zhaozg/luajit-cmake/commit/67637d05afadea249c2ef466ba1273db0f8bb772) (2026/3/18) | MIT | George Zhao (zhaozg) | tcxLua | [LICENSE](https://github.com/zhaozg/luajit-cmake/blob/master/LICENSE) |

> **Note**: Box2D v2.3.x以前はzlib Licenseでした。TrussCはv2.4.1を使用しています。

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

### MIT License (Dear ImGui, nlohmann/json, pugixml, cpp-httplib, Box2D, Lua, LuaJIT, luajit-cmake)

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

MITライセンスを選択する場合は、上記のMIT Licenseセクションを参照してください。

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

### MIT or Public Domain (bc7enc, bcdec)

bc7enc and bcdec are dual-licensed under MIT or Public Domain. You may choose either license.

**bc7enc:**
```
This software is available under 2 licenses -- choose whichever you prefer.
If you use this software in a product, attribution / credits is requested but not required.

ALTERNATIVE A - MIT License
Copyright(c) 2020-2021 Richard Geldreich, Jr.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
```

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

### BSD 2-Clause License (LZ4, HAP)

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
