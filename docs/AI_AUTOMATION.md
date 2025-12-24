# AI Automation & Stdin Interface

## What is this?

TrussC applications can receive commands via stdin and output events to stdout.
This enables powerful automation scenarios:

- **AI agents can control your app** - Claude, GPT, etc. can simulate mouse/keyboard input
- **Automated UI testing** - Write shell scripts to test interactive behavior
- **Operation replay** - Capture user actions and replay them later
- **External tool integration** - Connect with other processes via pipes

## Quick Example

```bash
# Get app status
echo 'tcdebug info' | ./myapp

# Simulate a mouse click at (100, 200)
echo 'tcdebug {"type":"mouse_click","x":100,"y":200}' | ./myapp

# Capture all user input to a file
echo 'tcdebug stream normal' | ./myapp > captured.txt
```

## Security: Opt-in Required

**Input simulation is disabled by default** for security reasons.

To enable it, set `enableDebugInput = true` in your WindowSettings:

```cpp
int main() {
    WindowSettings settings;
    settings.enableDebugInput = true;  // Enable input simulation
    return runApp<MyApp>(settings);
}
```

Without this flag:
- `tcdebug info`, `tcdebug help`, `tcdebug screenshot` still work (read-only)
- Input simulation commands (`mouse_*`, `key_*`, `drop`, `stream`, `playback`) return an error

## Command Reference

All commands use the `tcdebug` prefix. Two formats are supported:

### JSON Format (Recommended for AI)
```
tcdebug {"type":"command_name", "param1": value, ...}
```

### Space-separated Format (Convenient for humans)
```
tcdebug command_name param1 param2 ...
```

### Always Available Commands

| Command | Description | Example |
|---------|-------------|---------|
| `info` | Get comprehensive app status | `tcdebug info` |
| `help` | List available commands | `tcdebug help` |
| `screenshot` | Save screenshot to file | `tcdebug {"type":"screenshot","path":"/tmp/shot.png"}` |

#### Info Command Response Fields

The `info` command returns detailed status information:

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | string | ISO 8601 UTC timestamp |
| `fps` | number | Current frame rate |
| `width`, `height` | number | Window size (logical pixels) |
| `dpiScale` | number | DPI scale factor (e.g., 2.0 for Retina) |
| `fullscreen` | boolean | Fullscreen mode status |
| `mouseX`, `mouseY` | number | Current mouse position |
| `updateCount`, `drawCount` | number | Frame counters |
| `elapsedTime` | number | Seconds since app start |
| `backend` | string | Graphics backend (e.g., "Metal (macOS)") |
| `memoryBytes` | number | Process memory usage (RSS) |
| `nodeCount` | number | Active Node objects (for leak detection) |
| `textureCount` | number | Active Texture objects |
| `fboCount` | number | Active FBO objects |
| `debugInputEnabled` | boolean | Whether input simulation is enabled |

### Input Simulation Commands (requires enableDebugInput)

#### Mouse Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `mouse_move` | `x`, `y` | Move mouse to position |
| `mouse_press` | `x`, `y`, `button` | Press mouse button |
| `mouse_release` | `x`, `y`, `button` | Release mouse button |
| `mouse_click` | `x`, `y`, `button` | Press + release (convenience) |
| `mouse_scroll` | `dx`, `dy` | Scroll wheel |

Button values: `"left"` (default), `"right"`, `"middle"`, or `0`, `1`, `2`

Examples:
```bash
# JSON format
tcdebug {"type":"mouse_click","x":100,"y":200,"button":"left"}
tcdebug {"type":"mouse_scroll","dx":0,"dy":-1}

# Space-separated format
tcdebug mouse_click 100 200 left
tcdebug mouse_scroll 0 -1
```

#### Keyboard Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `key_press` | `key` | Press a key (keycode) |
| `key_release` | `key` | Release a key |
| `key_send` | `key` | Press + release (convenience) |

Key values are sokol keycodes (integers). Common keys:
- Space: 32, Enter: 257, Escape: 256
- A-Z: 65-90, 0-9: 48-57
- Arrow keys: 262-265 (right, left, down, up)

Examples:
```bash
# Press space (keycode 32)
tcdebug {"type":"key_send","key":32}
tcdebug key_send 32
```

#### File Drop Command

| Command | Parameters | Description |
|---------|------------|-------------|
| `drop` | `files` (array) | Simulate file drop |

```bash
tcdebug {"type":"drop","files":["/path/to/file1.png","/path/to/file2.jpg"]}
tcdebug drop /path/to/file1.png /path/to/file2.jpg
```

### Stream Capture Commands

Capture real user input and output to stdout:

| Command | Parameters | Description |
|---------|------------|-------------|
| `stream` | `mode` | Start/stop capturing user input |

Modes:
- `off` / `disable` - Stop capturing
- `normal` - Capture clicks, key presses (skip mouse movement during drag)
- `detail` - Capture everything including all mouse movements

```bash
# Start capturing
tcdebug stream normal

# Output example (user clicks and types):
tcdebug {"type":"mouse_press","x":150,"y":300,"button":"left","time":1.234}
tcdebug {"type":"mouse_release","x":150,"y":300,"button":"left","time":1.456}
tcdebug {"type":"key_press","key":65,"time":2.100}
tcdebug {"type":"key_release","key":65,"time":2.200}
```

The `time` field indicates elapsed time since app start, useful for replay timing.

### Playback Mode

| Command | Parameters | Description |
|---------|------------|-------------|
| `playback` | `mode` | Set playback timing mode |

Modes:
- `immediate` (default) - Execute commands instantly
- `realtime` - Respect `time` field for realistic timing (not yet implemented)

## Output Format

All responses use JSON format with `tcdebug` prefix:

```
tcdebug {"type":"info","fps":60,"width":1280,"height":720,...}
tcdebug {"status":"ok","type":"mouse_click"}
tcdebug {"status":"error","message":"debug input disabled"}
```

This format allows:
- Easy parsing by AI/scripts
- Copy-paste replay (output can be fed back as input)
- Filtering with `grep tcdebug`

## Use Cases

### AI Agent Integration

An AI agent can:
1. Query app state with `tcdebug info`
2. Send commands based on reasoning
3. Capture output to verify results
4. Take screenshots for visual feedback

### Automated Testing

```bash
#!/bin/bash
# test_button_click.sh

# Start app in background
./myapp &
APP_PID=$!
sleep 1

# Click the "Submit" button at known position
echo 'tcdebug {"type":"mouse_click","x":400,"y":300}' > /proc/$APP_PID/fd/0

# Check result...
kill $APP_PID
```

### Operation Recording & Replay

```bash
# Record session
echo 'tcdebug stream normal' | ./myapp | tee session.txt

# Replay later (filter only tcdebug lines, pipe back)
grep '^tcdebug' session.txt | ./myapp
```

## Comments

Lines containing `#` are treated as comments (stripped before parsing):

```bash
tcdebug mouse_click 100 200  # Click the button
tcdebug info  # Check status
```

## Tips for AI Integration

1. **Always check `debugInputEnabled`** in the `info` response before sending input commands
2. **Use JSON format** for reliable parsing
3. **Use `mouse_click`** instead of separate press/release for simple clicks
4. **Check `status` field** in responses to detect errors
5. **Use `screenshot`** to get visual feedback when needed

## Related

- [ARCHITECTURE.md](ARCHITECTURE.md) - Console system overview
- [tcDebugInput.h](../trussc/include/tc/utils/tcDebugInput.h) - Implementation
