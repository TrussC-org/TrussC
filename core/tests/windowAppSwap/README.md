# Window request regression checks

The default `windowAppSwap` test is headless and runs in `allCoreTests`. Its
probes outlive each window's teardown. It checks request timing, last-request
wins, close precedence, nested dispatch, request/apply validation, pending
ownership and reentrant App teardown.

`native/` is an optional Linux/X11 harness (requires X11 and Xtst development
libraries). It exercises the actual backend, including KEY_DOWN followed by
CHAR, a borderless window, immediate Window destruction and shutdown callbacks
that destroy another window or request its close. It is separate from the
headless runner and uses a plain `main()`.

From the repository root, configure and build through the approved build wrapper:

```sh
$TCBUILD cmake -S core/tests/windowAppSwap/native -B core/tests/windowAppSwap/build-native \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
$TCBUILD cmake --build core/tests/windowAppSwap/build-native
```

Run with `DISPLAY` pointing to an Xvfb server started with Mesa:
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`
and `LIBGL_ALWAYS_SOFTWARE=1`. Use the same environment for the harness. Stop
only the Xvfb PID you started. Each invocation runs one case:

```sh
ASAN_OPTIONS=detect_leaks=0 core/tests/windowAppSwap/build-native/windowRequestNative update-close
```

Cases: `update-close`, `draw-close`, `key-close`, `update-swap`, `draw-swap`,
`key-swap`, `borderless-close`, `destructor-close`, `shutdown-destroy`,
`shutdown-chain`. Success is exit code 0; an external timeout may bound a stuck
run, but every assertion checks state and callback counts, not elapsed time.
Leak detection is disabled because the framework deliberately keeps process
registries alive and the graphics driver has process-lifetime allocations;
address checking remains enabled. macOS and Windows backend paths need separate
verification.
