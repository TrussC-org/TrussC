# Color tool

colorToolExample converts a six-digit hex color to TrussC's normalized RGB values, prints the result, and exits. With no arguments it opens a small GUI; Escape closes it.

From this directory, build with an installed `trusscli`:

```sh
trusscli build
./bin/colorToolExample ff8000
# 1 0.501961 0
./bin/colorToolExample
```

The executable is `bin/colorToolExample.exe` on Windows and `bin/colorToolExample.app/Contents/MacOS/colorToolExample` on macOS. Pass arguments to that executable directly. A desktop build is required for the command-line path.

`main.cpp` only forwards `argc`/`argv` to `tcApp::run()` in [src/tcApp.cpp](src/tcApp.cpp). TrussC has no app argument accessor. The CLI branch uses `Color::fromHex()` without starting a window or runtime. Exit code 0 means success; 1 means invalid arguments or an output error. Input must be exactly six hex digits, without `#` or `0x`:

```sh
./bin/colorToolExample 00ff80
```

The no-arguments GUI pattern comes from [tools/src/main.cpp](../../../tools/src/main.cpp), in `trusscli`'s `main()`. The GUI shows a color swatch and command usage.
