# Phone control panel

phonePanelExample lets a phone choose a mode and adjust a level while a desktop app runs fullscreen. The page contains radio buttons, one fader, and a scrolling log. Use a trusted LAN: this example serves plain HTTP, so anyone on the network path can see the startup token. There is no TLS configuration in this example.

From this directory, build and run with an installed `trusscli`:

```sh
trusscli build
trusscli run
```

Connect the phone to the same LAN and open the complete URL shown on the app screen, including `?token=...`. Escape closes the desktop app and stops the server. The page and its JavaScript are inline in [src/tcApp.cpp](src/tcApp.cpp); no external assets are needed.

The app picks one up, private IPv4 interface. If it picks the wrong LAN, select an address belonging to this computer (port 8088):

```sh
PHONE_PANEL_BIND=192.168.1.20 ./bin/phonePanelExample
```

For a local test, use `PHONE_PANEL_BIND=127.0.0.1` instead. On Windows, set the environment variable before running `bin/phonePanelExample.exe`; on macOS the executable is `bin/phonePanelExample.app/Contents/MacOS/phonePanelExample`.

HTTP handlers run on worker threads. Scene changes go to the main thread through `runOnMainThread()`. `getLogger().onLog` feeds a mutex-protected log ring, and the phone polls it once per second. Shutdown calls `stop()` before joining the server thread.

Security points implemented beside the handling code:

- Bind one local interface rather than exposing every interface.
- Generate a fresh random token each launch, show it only on screen, and check every route without exiting on the first mismatched byte. Treat the full URL as a secret.
- Limit bodies to 64 bytes; validate radio indices and finite numbers; clamp the fader to 0–1.
- Bound workers, queued connections, timeouts, and authenticated requests (30/second).
- Retain at most 100 log lines of 512 bytes; render them with `textContent`.

This is a desktop HTTP server example; it cannot host the panel from a browser build.
