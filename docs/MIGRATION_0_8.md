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

## The MCP port line is only on the Logger

The raw stderr copy of the MCP port line is gone (breaking change, #414). After the port is bound, the server logs `[MCP] HTTP server listening on http://HOST:PORT/mcp` as a Logger Notice only: on stdout, in the file set with `TRUSSC_LOG_FILE`, and to `onLog` listeners. Logger lines carry a timestamp and a `[NOTICE]` prefix.

If a tool reads the port from stderr, change it to one of these:

- Read the Logger line from stdout or the log file, and match `HTTP server listening on http://` anywhere in the line. A console level of Warning or higher hides the line on stdout; the log file still gets it as long as its own level allows Notice.
- Set a fixed port with `TRUSSC_MCP_PORT=8080` and connect to `http://127.0.0.1:8080/mcp`. If that port is already in use, the server fails to start.
