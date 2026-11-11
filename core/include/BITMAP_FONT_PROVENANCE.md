# Bitmap glyph provenance

Source URLs: https://github.com/freeglut/freeglut (8x13 bitmap font),
https://gitlab.freedesktop.org/xorg/font/misc-misc (X11 fixed 8x13 font).
The original TrussC import `51c74486` identifies the data as freeglut/X11.
Exact source file, version and upstream commit: **not recorded**.
Source archive SHA-256: **not recorded**. No matching upstream source copy
was available offline; the import label is not independent authentication.

`tcBitmapFont.h` contains 95 ASCII glyphs (U+0020 through U+007E), 13 bytes
each. The 1,235 glyph bytes are unchanged from that import. Later table layout,
comments and registration/UTF-8 APIs are owned TrussC changes.

| Data/file | SHA-256 |
| --- | --- |
| 1,235 ASCII bytes in glyph/row order (hex literals decoded to bytes) | `7917cc741fbb69e34d0491b39d5c5d01098c5ebe77935a5f75951380b49eb627` |
| `tcBitmapFont.h` (complete working-tree file) | `5494b1ab33915f0b9b52e619dd130d07c5b09db01ce618f42a3a1658b57eef8a` |

X11 `font-misc-misc/COPYING` says: "Public domain font. Share and enjoy."
This notice is also recorded in the installed `xfonts-base` copyright file.
Freeglut is distributed under MIT/X11; its notice is retained in
[docs/LICENSE.md](../../docs/LICENSE.md#bitmap-font-data).
The exact source revision and applicable import-time font notice still need
recovery; this record does not certify a byte match to those upstream projects.
Retain the glyph payload and record verified source identity on the next update.
