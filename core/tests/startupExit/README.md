Regression coverage for #394. Uses the allCoreTests layout; no GPU is needed
for the default test. A test-local replacement for `sapp_run` returns without
initialization and sends an error through the real descriptor's logger.

Run `allCoreTests startupExit`. On Linux, `allCoreTests startupExit --window`
also launches a real window and checks successful setup/exit returns 0, then
simulates another failed launch to check that the previous success is cleared.
Use Xvfb when running without a display. The same arguments work when building
this test separately with trusscli.

On Linux, `--glx-failure` calls the real platform launcher and checks that a
missing GLX extension returns 1 without calling `App::setup()` or `App::exit()`.
Run it in a separate process with GLX disabled on Xvfb (no source injection):

```sh
env __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s '-screen 0 640x480x24 -extension GLX' \
  core/tests/startupExit/bin/startupExit --glx-failure
```

The X display must still be available; `XOpenDisplay` failure deliberately aborts.

When this test translation unit is compiled with `TC_HOT_RELOAD_BUILD`, it also
checks the `TC_RUN_APP` hot-reload failure path, including resetting a previous
setup status. The test platform entry point fails before loading a Guest.

The optional window test is intended for Linux: macOS's normal termination
exits inside NSApplication rather than returning to the test. Native failure
injection still needs a Windows/macOS build. In a Windows debug build, force
all `D3D11CreateDevice` attempts to return a failing HRESULT (including the
feature-level and debug-layer retries), set `TRUSSC_LOG_FILE`, and launch an
app that uses `TC_RUN_APP`. Check that setup is not called, one final device
error with that HRESULT reaches the file, and the process exits with code 1.
Also check normal startup/exit remains successful, and that a successful retry
does not emit a device error. Remove the temporary injection after checking.
