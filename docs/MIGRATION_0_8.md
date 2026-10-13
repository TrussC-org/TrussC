# Migrating to v0.8.0

## Stdin commands need `console::start()`

Console input is now opt-in (breaking change, #689 and #756). Neither `runApp()` nor `runHeadlessApp()` starts a stdin reader automatically. If your app subscribes to `events().console`, add `console::start()` in `setup()`:

```cpp
void MyApp::setup() {
    consoleListener_ = events().console.listen([this](ConsoleEventArgs& e) {
        handleCommand(e);
    });
    console::start();
}
```

Keep `consoleListener_` as an `EventListener` member. Both runners pump commands on the main thread and stop delivery before `exit()` / `cleanup()`; no manual pump is needed.

`console::stop()` disables delivery and drains pending commands. `console::start()` drains pending commands before enabling delivery again. The first start creates the process's only detached stdin reader, which continues consuming and discarding lines while stopped and ends at EOF. Restarting delivery does not create a new reader or replay discarded lines. Apps that never opt in leave stdin untouched. Web support remains unavailable.
