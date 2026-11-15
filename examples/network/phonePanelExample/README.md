# Phone control panel

phonePanelExample lets a phone control a fullscreen app on the same trusted LAN. Radio buttons select red, green, or blue; a fader sets the brightness of a large rectangle. The page also shows a scrolling log. Use a trusted LAN: plain HTTP exposes the startup token to anyone on the network path. This example has no TLS API configuration.

From this directory, build and run with an installed `trusscli`:

```sh
trusscli build
trusscli run
```

Connect the phone to the same LAN and open the complete URL shown on screen, including `?token=...`. Escape closes the app and stops the server. [src/tcApp.cpp](src/tcApp.cpp) contains the server and app; [src/panelPage.h](src/panelPage.h) contains the page and JavaScript, with no external assets.

The short handling code shows these essentials:

- Bind one interface on port 8088 using `getLocalIp()`: prefer the default-route IPv4 interface, then private IPv4, any IPv4, and finally IPv6. Fall back to `127.0.0.1` if no address is available.
- Generate a random token per launch, display it only on screen, and check it on every route.
- Cap bodies at 64 bytes, parse and validate radio indices and finite levels, and clamp brightness to 0–1.
- Send scene changes through `runOnMainThread()` with a weak app owner.
- Feed a mutex-protected ring from `getLogger().onLog`: at most 100 lines of 512 bytes, displayed with `textContent`.
- Stop the server and join its thread in the framework's `exit()` callback, before member destruction.

This example requires a desktop HTTP server; there is no browser-build API for hosting the panel.
