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

### Addon Dependencies (Optional)

These libraries are only included if you use the corresponding addon.

| Library | Version | License | Author/Organization | Addon | License URL |
|---------|---------|---------|---------------------|-------|-------------|
| **mbedTLS** | 3.6.2 | Apache-2.0 or GPL-2.0-or-later (dual-licensed) | Arm Limited | tcxTls | [LICENSE](https://github.com/Mbed-TLS/mbedtls/blob/development/LICENSE) |
| **Box2D** | 2.4.1 | MIT (v2.4.0以降) | Erin Catto | tcxBox2d | [LICENSE](https://github.com/erincatto/box2d/blob/main/LICENSE) |

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

### MIT License (Dear ImGui, nlohmann/json, pugixml, Box2D)

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
