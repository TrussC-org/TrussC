# Additional clean-import byte records

These SHA-256 values identify the vendored working-tree bytes. Version/source
labels come from the source or existing import records; no new independent
upstream authentication was possible offline. Archive hashes are **not recorded**.
Retain each file's embedded license. The separate ImGui I2 edits and all other
existing patch ledgers remain in force. Fetched dependencies keep their URL/pin
and archive hashes in the owning CMake; no downloads were performed here.

## Dear ImGui

Source: https://github.com/ocornut/imgui
Version: v1.92.9b, recorded commit f1cc2ae.

| File | SHA-256 |
| --- | --- |
| `addons/tcxImGui/src/imgui/imgui.cpp` | `5a1e5128c0305f50b6556a77f71fde35836505883b74c1d6ae44723243f1e8de` |
| `addons/tcxImGui/src/imgui/imgui.h` | `0d8db1045db01d908853adfd26ae07c5bc5ab4789d4515f6ea34234a69ade0ca` |
| `addons/tcxImGui/src/imgui/imgui_demo.cpp` | `8930ec6bb26844a3d24aa519a07f674ec666b8c22c4d8a74fa6cc128033ed494` |
| `addons/tcxImGui/src/imgui/imgui_draw.cpp` | `83d30419a8e06a5f0a8692ee6de186f7ecfddecdc33769e76244837e2975b400` |
| `addons/tcxImGui/src/imgui/imgui_internal.h` | `efba9bccc971cc49ec3da3168968ba412c7ea89745c104a7f5c004c8349bc71f` |
| `addons/tcxImGui/src/imgui/imgui_stdlib.cpp` | `f97af53aa2a75e21229df70756ac18837a37dc9ae621a6b0bb53aace18baf035` |
| `addons/tcxImGui/src/imgui/imgui_stdlib.h` | `7ab7fd2f9fb9d4feae10584a6100642e16464e47f3f3cb223218074de5a24bf6` |
| `addons/tcxImGui/src/imgui/imgui_tables.cpp` | `b5deabe5b569ab712c11b6556562a646bf88327320fc60a451071b2a36498d72` |
| `addons/tcxImGui/src/imgui/imstb_rectpack.h` | `889b396795202d1457560a797a7242e96f6f132d4b88ca2d69be58bf05e1771f` |
| `addons/tcxImGui/src/imgui/imstb_textedit.h` | `a985f5fa0ed97353d493b497961e9eef52082edcd045cf6954b69990ec9d0741` |
| `addons/tcxImGui/src/imgui/imstb_truetype.h` | `c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528` |

## bcdec

Source: https://github.com/iOrange/bcdec
Version: 0.985; exact commit not recorded.

| File | SHA-256 |
| --- | --- |
| `addons/tcxHap/src/impl/bcdec.h` | `134520764d96f70a27db814173616c89ad85db95fbd3328417c7a8c77e7ca189` |

## LZ4

Source: https://github.com/lz4/lz4
Version: 1.10.0; exact commit not recorded.

| File | SHA-256 |
| --- | --- |
| `core/include/lz4/lz4.c` | `9396f7de527bc8435de9c7569fb7998e56545a84b4f3c2d808c0235c01774539` |
| `core/include/lz4/lz4.h` | `26b82efc53d1570f3b54eef02e9c4764c1ad374ff03cac04e2ced5ea4d4c552f` |

## nlohmann/json

Source: https://github.com/nlohmann/json
Version: 3.12.0; exact commit not recorded.

| File | SHA-256 |
| --- | --- |
| `core/include/nlohmann/json.hpp` | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |

## pugixml

Source: https://github.com/zeux/pugixml
Version: 1.16; TrussC pristine import 5c7f8499; exact upstream commit not recorded.

| File | SHA-256 |
| --- | --- |
| `core/include/pugixml/pugiconfig.hpp` | `e6c51fbc9a765608bd214ea47d229d3bbd6b650036cdffc94b96b498c202495c` |
| `core/include/pugixml/pugixml.cpp` | `bd94cd9ec6ac67c0723422640bb243eab4a7c2d9fd5584dab77305a50fa31ef4` |
| `core/include/pugixml/pugixml.hpp` | `0b7c94701df3d50981ffafe57f177c77b940e347c6e5d01073e4161f61299212` |

## stb_perlin

Source: https://github.com/nothings/stb
Version: 0.5; commit 2bb4a0accd4003c1db4c24533981e01b1adfd656.

| File | SHA-256 |
| --- | --- |
| `core/include/stb/stb_perlin.h` | `17e688447a02776fe667e0b6a5d1155bf85c827a83f787ae2d44f57699274960` |

