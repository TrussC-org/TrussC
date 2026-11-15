# Debugging

## 1. Read and save TrussC logs

When a TrussC app misbehaves, start with its log. Check the terminal, VS Code Terminal/Debug Console, or Xcode debug console. On Android, use Android Studio's Logcat window or `adb logcat -s TrussC`.

Windows Release builds hide the console; to keep it, put this before the TrussC include in `tcApp.h`:

```cpp
#define TRUSSC_SHOW_CONSOLE
#include <TrussC.h>
```

Desktop console and file lines have this format (produced by `logError("tcApp") << "Could not load image";`):

```text
[14:23:05.042] [ERROR] [tcApp] Could not load image
```

Fields: local time, severity, optional module, message. `[ERROR]` means an operation failed; `[WARNING]` means something unexpected happened but the app recovered. Android logcat supplies its own time and priority around the message.

Set `TRUSSC_LOG_FILE` in the launch environment to capture native startup failures before `setup()`. For example, in a Linux/macOS terminal:

```sh
TRUSSC_LOG_FILE=/tmp/trussc-debug.log ./myApp
```

In app code, open a file in `tcApp::setup()`:

```cpp
if (!setLogFile("logs/app.log")) return;
```

Files append; relative paths use `getDataPath()`, creating missing parent folders. `setLogFile()` logs failures and returns a boolean-testable `SaveResult`. On Web, use `setLogFile()` as shown instead of the environment variable; files live in the browser's virtual filesystem.

## 2. Search the log message

An error message can help you find the cause of a TrussC app's failure. Search for the message, or paste it into the chat on [trussc.org](https://trussc.org/) with what you were doing and your OS.

## 3. Match the symptom to the cause

The table pairs common TrussC app symptoms with likely causes and fixes. Snippets belong in `tcApp : public tc::App`, with `using namespace tc;` and `using namespace std;`.

| Symptom → likely cause | Fix in tcApp |
| --- | --- |
| Nothing updates in `EVENT_DRIVEN` mode → no `redraw()` request | Request a frame after each visible change, for example in an input handler: `void keyPressed(int) override { redraw(); }` |
| File not found → looking outside the app's data folder | Put `image.png` in the project's `bin/data/` folder; inspect the resolved path in `setup()`: `logNotice() << getDataPath("image.png");` |
| Crash when calling `addChild()` in a Node constructor → shared ownership is not ready | Build the tree in `setup()` (also in your own Node subclasses): `void setup() override { addChild(make_shared<Node>()); }` |
| Drawing from a worker callback → Node/GPU work needs the main thread | Deliver the event to the main thread, update state, then draw in `draw()`. For a log callback with member `EventListener listener`: `listener = getLogger().onLog.listen([](LogEventArgs&) { redraw(); }, Deliver::Main);` |
| macOS “Problem Report” after a crash → macOS is reporting an abnormal termination | Read the report's crashed-thread backtrace and the last app logs; rerun under the debugger. There is no TrussC API for this OS dialog. Preserve future app logs in `setup()`: `if (!setLogFile("logs/app.log")) return;` |

## 4. Find the crash site with a debugger

A debugger can stop a crashing TrussC app at the failing code. Open the generated project in VS Code and press **F5**. Use CodeLLDB on Linux/macOS and the C/C++ extension's `cppvsdbg` on Windows VS Code, as described in [GET_STARTED.md › Editor Setup](../GET_STARTED.md#editor-setup). Inspect the call stack and the first frame in your code. For Windows editor forks, use the CodeLLDB configuration from that section.

In Xcode, run the app with **Product → Run** and inspect the stopped thread's call stack in the Debug navigator.

## 5. Choose log levels and show logs in the app

TrussC log levels control how much detail each output shows, and a log listener lets you display messages in the app. Write messages with `logVerbose()`, `logNotice()`, `logWarning()`, `logError()`, and `logFatal()` using stream insertion (`<<`).

| Level (`LogLevel::…`) | Meaning |
| --- | --- |
| `Verbose` | Debugging detail; hidden by default |
| `Notice` | Normal information; default threshold for each output |
| `Warning` | Unexpected but recoverable |
| `Error` | An operation failed |
| `Fatal` | Cannot continue (logging itself does not terminate the app) |
| `Silent` | Turns an output off |

Three outputs have independent thresholds; an event also carries logs:

- **Console:** desktop/Web use stdout for Verbose/Notice and stderr for Warning and higher; iOS uses `os_log`; Android uses logcat, tag `TrussC`.
- **File:** opened with `setLogFile()` or `TRUSSC_LOG_FILE`.
- **System:** macOS uses `os_log` (Console.app, subsystem `org.trussc`, category `TrussC`); Windows uses `OutputDebugStringW` (Visual Studio Output or DebugView). Other platforms have no separate system output. macOS skips duplicates when Xcode mirrors system logs.
- **`getLogger().onLog`:** receives every line regardless of output thresholds.

A threshold admits that level and higher. In `tcApp::setup()`, keep the console at Notice while saving Verbose detail:

```cpp
setLogLevel(LogLevel::Notice);
setFileLogLevel(LogLevel::Verbose);
if (!setLogFile("logs/app.log")) return;
```

`setLogLevel()` resets all three thresholds; subsequent `setConsoleLogLevel()`, `setFileLogLevel()`, or `setSystemLogLevel()` calls override one. Runtime exit protocol records still reach an open file even at Silent.

Keep an in-app listener as a member; destroying its `EventListener` disconnects it. Callbacks normally run on the emitting thread. This `tcApp` uses `Deliver::Main` for safe state updates, then draws the latest message in `draw()`:

```cpp
#include <TrussC.h>
using namespace tc;
class tcApp : public App {
public:
    std::string latestLog;
    EventListener logListener;
    void setup() override {
        logListener = getLogger().onLog.listen([this](LogEventArgs& e) {
            latestLog = e.timestamp + " [" + logLevelToString(e.level) + "] " + e.message;
            redraw();
        }, Deliver::Main);
    }
    void draw() override {
        drawBitmapString(latestLog, 10, 20);
    }
};
```

## 6. Look up TrussC pitfalls and API details

For more TrussC symptoms and API details, consult [FOR_AI_ASSISTANT.md › Pitfalls & troubleshooting](FOR_AI_ASSISTANT.md#pitfalls--troubleshooting).
