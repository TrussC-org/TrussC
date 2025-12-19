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

TrussC includes or depends on the following third-party libraries. All are permissive open-source licenses compatible with commercial use.

### Core Dependencies

| Library | Version | License | Author/Organization | URL |
|---------|---------|---------|---------------------|-----|
| **sokol** | - | zlib/libpng | Andre Weissflog | https://github.com/floooh/sokol |
| **Dear ImGui** | 1.92.6 | MIT | Omar Cornut | https://github.com/ocornut/imgui |
| **stb_image** | 2.30 | MIT or Public Domain | Sean Barrett | https://github.com/nothings/stb |
| **stb_image_write** | 1.16 | MIT or Public Domain | Sean Barrett | https://github.com/nothings/stb |
| **stb_truetype** | 1.26 | MIT or Public Domain | Sean Barrett | https://github.com/nothings/stb |
| **stb_vorbis** | 1.22 | MIT or Public Domain | Sean Barrett | https://github.com/nothings/stb |
| **dr_mp3** | 0.6.39 | Public Domain or MIT-0 | David Reid | https://github.com/mackron/dr_libs |
| **dr_wav** | 0.13.16 | Public Domain or MIT-0 | David Reid | https://github.com/mackron/dr_libs |
| **miniaudio** | 0.11.21 | Public Domain or MIT-0 | David Reid | https://github.com/mackron/miniaudio |
| **nlohmann/json** | 3.11.3 | MIT | Niels Lohmann | https://github.com/nlohmann/json |
| **pugixml** | 1.15 | MIT | Arseny Kapoulkine | https://pugixml.org/ |

### Addon Dependencies (Optional)

These libraries are only included if you use the corresponding addon.

| Library | Version | License | Author/Organization | Addon | URL |
|---------|---------|---------|---------------------|-------|-----|
| **mbedTLS** | 3.6.2 | Apache-2.0 or GPL-2.0-or-later | Arm Limited | tcxTls | https://github.com/Mbed-TLS/mbedtls |
| **Box2D** | 2.4.1 | MIT | Erin Catto | tcxBox2d | https://github.com/erincatto/box2d |

---

## License Details

### zlib/libpng License (sokol)

```
zlib/libpng license

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

### Public Domain / MIT-0 (stb, dr_libs, miniaudio)

These libraries offer a choice of Public Domain (Unlicense) or MIT-0 license. You may choose either.

**Public Domain (Unlicense):**
```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

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
