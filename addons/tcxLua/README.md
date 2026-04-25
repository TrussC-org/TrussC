# tcxLua

- Using Lua 5.4.8 sources now (NOTE: Lua 5.5 is currently not supported by Sol2)
- Using [sol2](https://github.com/ThePhD/sol2) (v3.5.0).
- LuaJIT v2.1 can be enabled with `-DUSE_LUAJIT=ON` in cmake (using [luajit-cmake](https://github.com/zhaozg/luajit-cmake), disabled by default).

## Binding coverage

### Dones

- trussc (TrussC.h directly defined functions)
- cmath (common use ones only), tcMath
- tcPrimitives.h, tcLog.h
- Vec2, Vec3, Vec4, Mat4, Quaternion, Rect
- Color, colors (constants)
- Mesh, Shader
- Fbo, Texture, Image, Pixels
- EasyCam, Light, Material
- Font, Path
- Json, Xml
- `Tween<T>` (as TweenFloat, TweenVec2, TweenVec3, TweenColor)

## Known Issues

- Default value is not treated perfectly now ([Issue#1](https://github.com/funatsufumiya/tcxLua/issues/1)). So you may need additional values when using methods for example:
  - `clear(0.08, 1.0)` instead of `clear(0.08)`
  - `setColor(1.0, 0.0, 0.0, 1.0)` instead of `setColor(1.0, 0.0, 0.0)`
- `end()` methods are replaced to `end_fbo()`, `end_shader()`, `end_cam()` etc ([Issue#11](https://github.com/funatsufumiya/tcxLua/issues/11)).

## Development

### Bindgen

Please read [tools/bindgen/README.md](tools/bindgen/README.md) in detail.

```bash
$ cd tools/bingen
# $ pip install uv # only at first time
# $ uv sync # only at first time
$ uv run main.py ../../../../core/include/TrussC.h
$ cp trussc_generated.cpp ../../src/generated
```

## License

[LICENSE](./LICENSE) is combined result. Please also check [docs/LICENSE_NOTE.md](docs/LICENSE_NOTE.md/) in detail.
