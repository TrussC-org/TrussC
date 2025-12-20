# TrussC API Reference (For AI Agents)

This document is a comprehensive API reference for AI agents (Cursor, Claude Code, etc.) to write TrussC code.

## Overview

TrussC is a lightweight creative coding framework based on sokol. It aims for an API similar to openFrameworks.

```cpp
#include "TrussC.h"
using namespace trussc;
// or
using namespace tc;
```

---

## App Basic Structure

```cpp
class tcApp : public tc::App {
public:
    void setup() override;      // Initialization (called once)
    void update() override;     // Per-frame update
    void draw() override;       // Per-frame rendering
    void exit() override;       // On exit (before cleanup, all objects alive)
    void cleanup() override;    // Exit cleanup

    // Mouse events
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float x, float y) override;

    // Keyboard events
    void keyPressed(int key) override;
    void keyReleased(int key) override;

    // Window events
    void windowResized(int w, int h) override;
};

// Launch app
int main() {
    WindowSettings settings;
    settings.title = "My App";
    settings.width = 1280;
    settings.height = 720;
    settings.highDpi = true;
    settings.msaaSamples = 4;  // Anti-aliasing
    runApp<tcApp>(settings);
    return 0;
}
```

---

## Drawing Functions

### Basic Shapes

```cpp
// Rectangle
drawRect(x, y, width, height);

// Circle
drawCircle(cx, cy, radius);

// Ellipse
drawEllipse(cx, cy, radiusX, radiusY);

// Line
drawLine(x1, y1, x2, y2);

// Triangle
drawTriangle(x1, y1, x2, y2, x3, y3);

// Point
drawPoint(x, y);

// Clear background
clear(r, g, b);           // 0-255
clear(gray);              // Grayscale
clear(Color c);           // Color struct
```

### Fill & Stroke

```cpp
fill();                   // Enable fill
noFill();                 // Disable fill
stroke();                 // Enable outline
noStroke();               // Disable outline
setStrokeWeight(weight);  // Set line width
```

### Custom Shapes (beginShape/endShape)

```cpp
// Polygon
beginShape();
vertex(x1, y1);
vertex(x2, y2);
vertex(x3, y3);
// ... more vertices
endShape(true);  // true = close shape

// Lines
beginShape(ShapeMode::Lines);
vertex(x1, y1);
vertex(x2, y2);
vertex(x3, y3);
vertex(x4, y4);
endShape();

// Available modes:
// ShapeMode::Polygon (default), ShapeMode::Lines,
// ShapeMode::LineStrip, ShapeMode::Points
```

### Text Drawing

```cpp
// Bitmap font (fixed width, fast) - Y coordinate is baseline
drawBitmapString("Hello", x, y);
drawBitmapString("Hello", x, y, scale);  // With scale

// TrueType font
Font font;
font.load("font.ttf", 24);  // Load with size
font.drawString("Hello", x, y);
font.getStringWidth("Hello");
font.getLineHeight();
```

---

## Color Settings

### setColor

```cpp
// RGB (0-255)
setColor(255, 0, 0);           // Red
setColor(255, 0, 0, 128);      // Semi-transparent red

// RGB (0.0-1.0)
setColor(1.0f, 0.0f, 0.0f);

// Grayscale
setColor(128);                 // Gray
setColor(0.5f);

// Color struct
setColor(Color c);
setColor(colors::cornflowerBlue);
```

### Color Spaces

```cpp
// HSB (H: 0-TAU, S: 0-1, B: 0-1)
setColorHSB(h, s, b);
setColorHSB(h, s, b, alpha);

// OKLab
setColorOKLab(L, a, b);
setColorOKLab(L, a, b, alpha);

// OKLCH (chroma and hue)
setColorOKLCH(L, C, H);
setColorOKLCH(L, C, H, alpha);
```

### Color Struct

```cpp
Color c(r, g, b);
Color c(r, g, b, a);
Color c(gray);
Color c(gray, a);

// From HSB
Color c = ColorHSB(h, s, b).toRGB();
Color c = ColorHSB(h, s, b, a).toRGB();

// From OKLab / OKLCH
Color c = ColorOKLab(L, a, b).toRGB();
Color c = ColorOKLCH(L, C, H).toRGB();

// Color interpolation
Color c = Color::lerp(c1, c2, t);          // RGB linear
Color c = Color::lerpHSB(c1, c2, t);       // HSB
Color c = Color::lerpOKLab(c1, c2, t);     // OKLab (recommended)
Color c = Color::lerpOKLCH(c1, c2, t);     // OKLCH
```

### Predefined Colors

```cpp
colors::white, colors::black, colors::red, colors::green, colors::blue
colors::yellow, colors::cyan, colors::magenta, colors::orange
colors::cornflowerBlue  // oF default background
```

---

## Transform

```cpp
pushMatrix();             // Save matrix to stack
popMatrix();              // Restore matrix from stack

translate(x, y);          // 2D translation
translate(x, y, z);       // 3D translation

rotate(radians);          // Z-axis rotation (2D)
rotateX(radians);         // X-axis rotation
rotateY(radians);         // Y-axis rotation
rotateZ(radians);         // Z-axis rotation

rotateDeg(degrees);       // Degree versions
rotateXDeg(degrees);
rotateYDeg(degrees);
rotateZDeg(degrees);

scale(s);                 // Uniform scale
scale(sx, sy);            // 2D scale
scale(sx, sy, sz);        // 3D scale

resetMatrix();            // Reset matrix
```

---

## Math Functions

### Basic Math

```cpp
// Constants
PI                        // 3.14159...
TAU                       // 6.28318... (2*PI)
HALF_PI                   // PI/2
DEG_TO_RAD                // PI/180
RAD_TO_DEG                // 180/PI

// Clamping & Interpolation
clamp(value, min, max);   // Limit to range
lerp(a, b, t);            // Linear interpolation (t: 0-1)
map(value, inMin, inMax, outMin, outMax);  // Range mapping

// Angle conversion
radians(degrees);
degrees(radians);

// Other
abs(x), floor(x), ceil(x), round(x)
min(a, b), max(a, b)
pow(x, y), sqrt(x)
sin(x), cos(x), tan(x)
asin(x), acos(x), atan(x), atan2(y, x)
```

### Vectors

```cpp
// Vec2
Vec2 v(x, y);
Vec2 v = Vec2::zero();
v.x, v.y
v.length();               // Length
v.lengthSquared();        // Squared length
v.normalized();           // Normalize
v.dot(other);             // Dot product
v.cross(other);           // Cross product (scalar)
v.distance(other);        // Distance
v.lerp(other, t);         // Interpolation
v.angle();                // Angle from X-axis
Vec2::fromAngle(radians); // Create from angle

// Operators
v1 + v2, v1 - v2, v1 * scalar, v1 / scalar

// Vec3
Vec3 v(x, y, z);
Vec3 v = Vec3::zero();
Vec3 v = Vec3::up();      // (0, 1, 0)
Vec3 v = Vec3::right();   // (1, 0, 0)
Vec3 v = Vec3::forward(); // (0, 0, 1)
v.x, v.y, v.z
v.length(), v.normalized(), v.dot(other), v.cross(other)
```

### Matrix

```cpp
Mat4 m = Mat4::identity();
Mat4 m = Mat4::translate(x, y, z);
Mat4 m = Mat4::rotateX(radians);
Mat4 m = Mat4::rotateY(radians);
Mat4 m = Mat4::rotateZ(radians);
Mat4 m = Mat4::scale(sx, sy, sz);
Mat4 m = m1 * m2;         // Matrix multiplication
Mat4 inv = m.inverted();  // Inverse matrix
```

### Noise

```cpp
// Perlin noise (0.0 - 1.0)
noise(x);
noise(x, y);
noise(x, y, z);

// Signed noise (-1.0 - 1.0)
signedNoise(x);
signedNoise(x, y);
signedNoise(x, y, z);

// Fractal noise (fbm)
fbm(x, y);
fbm(x, y, octaves, lacunarity, gain);
fbm(x, y, z, octaves, lacunarity, gain);
```

### Random

```cpp
random();                 // 0.0 - 1.0
random(max);              // 0.0 - max
random(min, max);         // min - max
randomInt(max);           // 0 - max-1
randomInt(min, max);      // min - max
randomSeed(seed);         // Set seed
```

---

## Input

### Mouse

```cpp
getMouseX();              // Mouse X coordinate
getMouseY();              // Mouse Y coordinate
getPMouseX();             // Previous frame mouse X
getPMouseY();             // Previous frame mouse Y
isMousePressed();         // Any mouse button pressed
isMousePressed(button);   // Specific button (MOUSE_BUTTON_LEFT, RIGHT, MIDDLE)

// Global coordinates (from inside Node)
getGlobalMouseX();
getGlobalMouseY();
```

### Keyboard

```cpp
isKeyPressed();           // Any key pressed
isKeyPressed(key);        // Specific key pressed
getLastKey();             // Last pressed key

// Special key constants
KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN
KEY_ENTER, KEY_ESCAPE, KEY_SPACE, KEY_TAB
KEY_BACKSPACE, KEY_DELETE
KEY_SHIFT, KEY_CTRL, KEY_ALT
KEY_F1 ~ KEY_F12
```

---

## Time & Frame

```cpp
// Elapsed time
getElapsedTime();         // Seconds since start (float)
getElapsedTimef();        // Same
getElapsedTimeMillis();   // Milliseconds
getElapsedTimeMicros();   // Microseconds
resetElapsedTimeCounter(); // Reset elapsed time counter
getDeltaTime();           // Time since last frame

// Frame count
getFrameCount();          // Update call count (= getUpdateCount)
getUpdateCount();         // Update call count
getDrawCount();           // Draw frame count
getFrameRate();           // Current FPS

// FPS settings
setFps(fps);              // Target FPS (Update + Draw sync)
setDrawFps(fps);          // Draw rate separate setting
setUpdateFps(fps);        // Update rate separate (Decoupled mode)
setVsync(true);           // VSync mode

// Timestamp
getTimestampString();     // "2024-01-15-18-29-35-299"
getTimestampString("%Y/%m/%d %H:%M:%S");  // Custom format (%i for millis)

// Current time
getSeconds();             // 0-59
getMinutes();             // 0-59
getHours();               // 0-23
getYear();                // e.g. 2024
getMonth();               // 1-12
getDay();                 // 1-31
getWeekday();             // 0=Sun, 1=Mon, ... 6=Sat

// Sleep
sleepMillis(ms);          // Millisecond sleep
sleepMicros(us);          // Microsecond sleep

// Exit app
exitApp();                // Request exit -> exit() -> cleanup() -> destructors
```

---

## Window

```cpp
getWindowWidth();         // Window width
getWindowHeight();        // Window height
getWidth();               // Drawing area width (= getWindowWidth)
getHeight();              // Drawing area height
isFullscreen();           // Is fullscreen
setFullscreen(bool);      // Set fullscreen
toggleFullscreen();       // Toggle fullscreen
setWindowTitle("title");  // Set title
```

---

## Image

```cpp
Image img;

// Load
img.load("image.png");
img.loadFromMemory(buffer, length);

// Allocate (for dynamic updates)
img.allocate(width, height, channels);

// State
img.isAllocated();
img.getWidth();
img.getHeight();
img.getChannels();

// Pixel operations
Color c = img.getColor(x, y);
img.setColor(x, y, color);
img.update();             // Upload changes to GPU

// Draw
img.draw(x, y);
img.draw(x, y, width, height);

// Save
img.save("output.png");

// Pixel data access
Pixels& pixels = img.getPixels();
unsigned char* data = img.getPixelsData();
```

---

## FBO (Framebuffer)

```cpp
Fbo fbo;

// Allocate
fbo.allocate(width, height);
fbo.allocate(width, height, msaaSamples);  // MSAA support

// Offscreen drawing
fbo.begin();              // Start drawing (clear transparent)
fbo.begin(r, g, b, a);    // With background color
// ... drawing ...
fbo.end();                // End drawing

// Draw
fbo.draw(x, y);
fbo.draw(x, y, width, height);

// Save
fbo.save("output.png");

// Copy to Image
Image img;
fbo.copyTo(img);

// State
fbo.getWidth();
fbo.getHeight();
fbo.isAllocated();
```

---

## Screenshot

```cpp
// Save entire screen to file
saveScreenshot("screenshot.png");
saveScreenshot(getDataPath("output/frame_" + toString(getFrameNum()) + ".png"));
```

**For AI Agents**: `saveScreenshot()` is very useful for autonomous debugging. Save rendering results as images and read them to visually verify code output. For example, when investigating "circle not displaying", taking a screenshot helps identify causes (coordinates off-screen, color same as background, etc.).

---

## Console Input (Commands from stdin)

TrussC apps can receive commands from stdin and respond with JSON on stdout. This enables AI agents to interact with running apps.

### Built-in Commands

```bash
# Get app info
echo "tcdebug info" | ./myApp
# Output: {"fps":60.0,"width":800,"height":600,"updateCount":123,"drawCount":123,"elapsedTime":2.05}

# Save screenshot
echo "tcdebug screenshot /tmp/shot.png" | ./myApp
# Output: {"status":"ok","path":"/tmp/shot.png"}
```

### Usage from AI Agents

Use FIFO (named pipe) to send multiple commands while app is running:

```bash
# Create FIFO
mkfifo /tmp/app_fifo

# Launch app in background (reading from FIFO)
./myApp < /tmp/app_fifo &
APP_PID=$!

# Open FIFO for writing
exec 3>/tmp/app_fifo

# Send commands
echo "tcdebug info" >&3
sleep 1
echo "tcdebug screenshot /tmp/debug.png" >&3
sleep 1

# Cleanup
exec 3>&-
kill $APP_PID
rm /tmp/app_fifo
```

### Custom Command Implementation

Handle custom commands in app:

```cpp
class tcApp : public App {
public:
    void setup() override {
        // Keep listener as member variable (IMPORTANT!)
        consoleListener_ = events().console.listen([this](ConsoleEventArgs& e) {
            if (e.args.empty()) return;

            if (e.args[0] == "spawn" && e.args.size() >= 3) {
                float x = stof(e.args[1]);
                float y = stof(e.args[2]);
                spawnObject(x, y);
                cout << "{\"status\":\"ok\",\"command\":\"spawn\"}" << endl;
            }
            else if (e.args[0] == "getState") {
                cout << "{\"objectCount\":" << objects.size()
                     << ",\"score\":" << score << "}" << endl;
            }
        });
    }

private:
    EventListener consoleListener_;  // Listener unregisters when destroyed
};
```

**IMPORTANT**: The return value of `events().console.listen()` (`EventListener`) MUST be stored as a member variable. If not stored, the listener is automatically unregistered when scope ends (RAII pattern).

### Disabling Console

If using stdin for other purposes, disable in setup():

```cpp
void setup() override {
    console::stop();  // Stop stdin reading thread
}
```

**For AI Agents**: This feature allows AI to send real-time commands to built-in TrussC apps, check app state, and capture screenshots. Very useful for debugging, testing, and automation.

---

## 3D Drawing

### EasyCam

```cpp
EasyCam cam;

// Initialize in setup()
cam.setDistance(500);
cam.setTarget(0, 0, 0);

// Use in draw()
cam.begin();
// ... 3D drawing ...
cam.end();

// Enable/disable mouse input
cam.enableMouseInput();
cam.disableMouseInput();

// Forward mouse events (call from mousePressed, etc.)
cam.mousePressed(x, y, button);
cam.mouseReleased(x, y, button);
cam.mouseDragged(x, y, button);
cam.mouseScrolled(dx, dy);

// Parameters
cam.setFovDeg(45);
cam.setNearClip(0.1f);
cam.setFarClip(10000);
cam.setSensitivity(1.0f);
cam.setZoomSensitivity(10.0f);
cam.reset();
```

### 3D Primitives

```cpp
// Generate Mesh
Mesh box = createBox(width, height, depth);
Mesh box = createBox(size);                    // Cube
Mesh sphere = createSphere(radius, resolution);
Mesh cylinder = createCylinder(radius, height, resolution);
Mesh cone = createCone(radius, height, resolution);
Mesh plane = createPlane(width, height, cols, rows);
Mesh icoSphere = createIcoSphere(radius, subdivisions);

// Draw
box.draw();
box.drawWireframe();
```

### Mesh

```cpp
Mesh mesh;

// Add vertices
mesh.addVertex(x, y, z);
mesh.addVertex(Vec3 v);
mesh.addNormal(nx, ny, nz);
mesh.addTexCoord(u, v);
mesh.addColor(Color c);
mesh.addIndex(index);
mesh.addTriangle(i0, i1, i2);

// Primitive mode
mesh.setMode(PrimitiveMode::Triangles);
mesh.setMode(PrimitiveMode::Lines);
mesh.setMode(PrimitiveMode::Points);
mesh.setMode(PrimitiveMode::TriangleStrip);

// Get data
vector<Vec3>& verts = mesh.getVertices();
int numVerts = mesh.getNumVertices();
int numIndices = mesh.getNumIndices();

// Draw
mesh.draw();
mesh.drawWireframe();

// Clear
mesh.clear();
```

### Lighting

```cpp
Light light;

// Light type
light.setDirectional(0, -1, 0);  // Direction (sunlight)
light.setPoint(x, y, z);         // Position (point light)

// Colors
light.setAmbient(0.2f, 0.2f, 0.2f);
light.setDiffuse(1.0f, 1.0f, 1.0f);
light.setSpecular(1.0f, 1.0f, 1.0f);
light.setIntensity(1.0f);

// Attenuation (for point lights)
light.setAttenuation(constant, linear, quadratic);

// Enable/disable
light.enable();
light.disable();

// Material
Material mat;
mat.setAmbient(0.2f, 0.2f, 0.2f);
mat.setDiffuse(0.8f, 0.2f, 0.2f);  // Object color
mat.setSpecular(1.0f, 1.0f, 1.0f);
mat.setShininess(32.0f);
mat.setEmission(0, 0, 0);  // Self-illumination

// Use in draw()
LightingState lighting;
lighting.enableLighting();
lighting.addLight(&light);
lighting.setMaterial(&mat);
lighting.begin();
mesh.draw();  // Drawn with lighting
lighting.end();
```

---

## Sound

```cpp
Sound sound;

// Load (.ogg, .wav, .mp3 supported)
sound.load("music.ogg");
sound.loadTestTone(440.0f, 1.0f);  // Test sine wave

// Playback control
sound.play();
sound.stop();
sound.pause();
sound.resume();

// Settings
sound.setVolume(0.8f);      // 0.0 - 1.0
sound.setPan(-0.5f);        // -1.0 (left) to 1.0 (right)
sound.setSpeed(1.5f);       // Playback speed (0.1 - 4.0)
sound.setLoop(true);

// State
sound.isPlaying();
sound.isPaused();
sound.isLoaded();
sound.getPosition();        // Position (seconds)
sound.getDuration();        // Duration (seconds)
```

### Microphone Input

```cpp
MicInput& mic = getMicInput();

mic.start();
mic.stop();
mic.isRunning();

// Get waveform data
float buffer[1024];
size_t count = mic.getBuffer(buffer, 1024);
```

---

## Network

### TCP Client

```cpp
TcpClient client;

// Event-driven pattern (IMPORTANT: store listeners!)
connectListener_ = client.onConnect.listen([this]() {
    tcLogNotice("TCP") << "Connected!";
});
disconnectListener_ = client.onDisconnect.listen([this]() {
    tcLogNotice("TCP") << "Disconnected";
});
receiveListener_ = client.onReceive.listen([this](TcpReceiveEventArgs& e) {
    string data(e.data.begin(), e.data.end());
    tcLogNotice("TCP") << "Received: " << data;
});

// Connect
client.connect("example.com", 80);

// Send
client.send("GET / HTTP/1.1\r\n\r\n");
client.sendBytes(data, length);

// State
client.isConnected();

// Must call in update()
client.update();
```

### TCP Server

```cpp
TcpServer server;

// Event handlers (store as members!)
clientConnectListener_ = server.onClientConnect.listen([](TcpClientEventArgs& e) {
    tcLogNotice("Server") << "Client connected: " << e.clientId;
});
clientReceiveListener_ = server.onClientReceive.listen([](TcpReceiveEventArgs& e) {
    // e.clientId, e.data
});

// Start server
server.start(8080);

// Send to client
server.send(clientId, data, length);
server.broadcast(data, length);  // Send to all clients

// Must call in update()
server.update();
```

### UDP Socket

```cpp
UdpSocket udp;

// Receive callback
receiveListener_ = udp.onReceive.listen([](UdpReceiveEventArgs& e) {
    // e.data, e.senderAddress, e.senderPort
});

// Bind to port
udp.bind(9000);

// Send
udp.sendTo("192.168.1.100", 9000, data, length);

// Must call in update()
udp.update();
```

---

## Threading

```cpp
// Thread (runs separate from main thread)
Thread thread;

// Start with lambda
thread.start([]() {
    // Background work
    while (running) {
        // ...
    }
});

// Check state
thread.isRunning();

// Wait for completion
thread.join();

// Thread Channel (thread-safe queue)
ThreadChannel<string> channel;

// Producer thread
thread.start([&channel]() {
    channel.send("message");
});

// Consumer (main thread, in update())
string msg;
if (channel.tryReceive(msg)) {
    // Process message
}

// Blocking receive
channel.receive(msg);  // Waits until data available
```

---

## Node (Scene Graph)

```cpp
class MyNode : public Node {
public:
    void setup() override { }
    void update() override { }
    void draw() override {
        setColor(255, 0, 0);
        drawRect(-50, -50, 100, 100);
    }

protected:
    // Events (overridable)
    bool hitTest(float localX, float localY) override {
        return localX >= -50 && localX <= 50 && localY >= -50 && localY <= 50;
    }
    bool onMousePress(float localX, float localY, int button) override {
        return true;  // Consume event
    }
};

// Usage
auto node = make_shared<MyNode>();
node->x = 100;
node->y = 100;
node->rotation = PI / 4;
node->scaleX = 2.0f;
node->scaleY = 2.0f;

// Tree operations
parent->addChild(node);
parent->removeChild(node);
parent->getChildren();
node->getParent();

// State
node->isActive = true;   // false skips update/draw
node->isVisible = true;  // false skips draw only

// Coordinate conversion
node->globalToLocal(gx, gy, lx, ly);
node->localToGlobal(lx, ly, gx, gy);
float mx = node->getMouseX();  // Mouse in local coords

// Timers (execute in update(), thread-safe)
uint64_t id = node->callAfter(2.0, []{ /* Run after 2 sec */ });
uint64_t id = node->callEvery(1.0, []{ /* Run every 1 sec */ });
node->cancelTimer(id);
node->cancelAllTimers();
```

**Timer Safety**: `callAfter()` / `callEvery()` callbacks execute during `update()` cycle (main thread). This means callbacks can safely modify node state, change drawing settings, and operate on other nodes. No mutex or locks needed since callbacks don't run from separate threads.

---

## ImGui

```cpp
// Initialize in setup()
imguiSetup();

// Use in draw()
imguiBegin();

ImGui::Begin("Window Title");
ImGui::Text("Hello, world!");
if (ImGui::Button("Click Me")) {
    // Button pressed
}
ImGui::SliderFloat("Value", &value, 0.0f, 1.0f);
ImGui::End();

imguiEnd();

// Check if ImGui using mouse/keyboard
if (!imguiWantsMouse()) {
    // Custom mouse handling
}
if (!imguiWantsKeyboard()) {
    // Custom keyboard handling
}
```

---

## Utilities

### Data Path

```cpp
// Default is data/ folder in same directory as executable
string path = getDataPath("image.png");

// Change root
setDataPathRoot("/path/to/data/");

// For macOS bundle (references Resources/data/)
setDataPathToResources();
```

### String Conversion

```cpp
// Number -> String
string s = toString(42);
string s = toString(3.14159, 2);         // "3.14" (2 decimal places)
string s = toString(42, 5, '0');         // "00042" (5 digits, zero-padded)
string s = toString(3.14, 2, 6, '0');    // "003.14" (precision+width+fill)

// Hex
string s = toHex(255);                   // "FF"
string s = toHex(255, 4);                // "00FF"

// Binary
string s = toBinary(255);                // "00000000...11111111"
string s = toBinary((char)65);           // "01000001" (= 'A')

// String -> Number
int i = toInt("42");
float f = toFloat("3.14");
double d = toDouble("3.14159");
bool b = toBool("true");                 // "true", "1", "yes" -> true
int i = hexToInt("FF");                  // 255
unsigned int u = hexToUInt("FFFFFFFF");
```

### String Operations

```cpp
// Search
bool found = isStringInString("hello world", "world");  // true
size_t count = stringTimesInString("abcabc", "abc");    // 2

// Split & Join
vector<string> parts = splitString("a,b,c", ",");       // {"a", "b", "c"}
vector<string> parts = splitString("a, b, c", ",", true, true);  // ignoreEmpty, trim
string joined = joinString(parts, "-");                 // "a-b-c"

// Replace (in-place)
string s = "hello world";
stringReplace(s, "world", "TrussC");                    // "hello TrussC"

// Trim
string s = trim("  hello  ");                           // "hello"
string s = trimFront("  hello");                        // "hello"
string s = trimBack("hello  ");                         // "hello"

// Case
string s = toLower("HELLO");                            // "hello"
string s = toUpper("hello");                            // "HELLO"
```

### JSON

```cpp
// Load
Json j = loadJson("config.json");

// Save
saveJson(j, "config.json");
saveJson(j, "config.json", 4);  // Indent 4

// Parse
Json j = parseJson("{\"key\": \"value\"}");

// Stringify
string s = toJsonString(j);

// Get/Set values
j["key"] = "value";
string val = j["key"];
int num = j["number"];
bool flag = j.value("flag", false);  // With default
```

### Logging

```cpp
// Stream format (with module name recommended)
tcLogNotice("tcApp") << "Info: " << value;
tcLogWarning("tcApp") << "Warning: " << message;
tcLogError("tcApp") << "Error: " << error;
tcLogVerbose("tcApp") << "Debug: " << debug;

// Without module name
tcLogNotice() << "Simple message";

// Level settings
tcSetConsoleLogLevel(LogLevel::Warning);  // Show Warning+ only
tcSetFileLogLevel(LogLevel::Verbose);

// File output
tcSetLogFile("app.log");
tcCloseLogFile();
```

### Events

```cpp
// Event definition
Event<MouseEventArgs> onMousePress;

// Register listener (RAII - auto-unregister on scope end)
EventListener listener = onMousePress.listen([](MouseEventArgs& e) {
    // Event handling
});

// Register member function
EventListener listener = onMousePress.listen(this, &MyClass::onPress);

// Manual disconnect
listener.disconnect();

// Fire event
MouseEventArgs args(x, y, button);
onMousePress.notify(args);
```

---

## Common Patterns & Gotchas

### EventListener RAII Pattern (CRITICAL)

```cpp
// WRONG - Listener immediately unregistered!
void setup() override {
    events().console.listen([](ConsoleEventArgs& e) { });  // Lost immediately
}

// CORRECT - Store as member variable
class tcApp : public App {
    EventListener consoleListener_;
    EventListener keyListener_;

    void setup() override {
        consoleListener_ = events().console.listen([](ConsoleEventArgs& e) { });
        keyListener_ = events().keyPressed.listen([](KeyEventArgs& e) { });
    }
};
```

### Node hitTest Types

```cpp
// Rectangle hit test
bool hitTest(float lx, float ly) override {
    return lx >= 0 && lx <= width && ly >= 0 && ly <= height;
}

// Circle hit test
bool hitTest(float lx, float ly) override {
    float dx = lx - centerX;
    float dy = ly - centerY;
    return dx*dx + dy*dy <= radius*radius;
}
```

### Decoupled Update/Draw Mode

```cpp
// Use when update needs higher rate than draw (e.g., physics)
setUpdateFps(120);  // Update at 120fps
setDrawFps(60);     // Draw at 60fps

// Check which cycle in update()
void update() override {
    // This runs at 120fps
    physics.step();
}
void draw() override {
    // This runs at 60fps
}
```

### Pixel-Perfect Coordinates

```cpp
// TrussC uses pixel coordinates (not normalized)
// (0,0) is top-left, Y increases downward
drawRect(0, 0, 100, 100);  // Top-left 100x100 rectangle

// High-DPI: logical pixels, not physical
// getWidth() returns logical width regardless of DPI scaling
```

### Platform Notes

**macOS Bundle Path**:
```cpp
// In bundle, use setDataPathToResources() to reference
// xxx.app/Contents/Resources/data/
void setup() override {
    setDataPathToResources();  // No-op on non-macOS
    img.load(getDataPath("image.png"));
}
```

**Windows Console**:
```cpp
// For GUI apps, console output may not show by default
// Use subsystem:console in CMake or allocate console:
#ifdef _WIN32
AllocConsole();
#endif
```

**DPI Scaling**:
```cpp
// getWidth()/getHeight() return logical (DPI-independent) sizes
// Drawing is automatic - no manual scaling needed
// Use sapp_dpi_scale() if you need the actual scale factor
float scale = sapp_dpi_scale();
```

---

## File Structure Example

```
myProject/
├── CMakeLists.txt
├── addons.make           # Used addons
├── src/
│   ├── main.cpp
│   └── tcApp.cpp
├── data/                 # Assets
│   ├── image.png
│   └── sound.ogg
└── build/                # Build output
```

---

## addons.make

List used addons, one per line:

```
tcxBox2d
tcxOsc
tcxTls
```

---

## Common Patterns

### Basic Drawing

```cpp
void tcApp::draw() {
    clear(30);  // Dark gray background

    setColor(255, 100, 100);
    fill();
    drawCircle(getMouseX(), getMouseY(), 50);

    setColor(255);
    noFill();
    stroke();
    drawRect(100, 100, 200, 150);
}
```

### Animation

```cpp
void tcApp::draw() {
    clear(0);

    float t = getElapsedTime();
    float x = getWidth() / 2 + sin(t) * 200;
    float y = getHeight() / 2 + cos(t * 0.7f) * 150;

    setColor(ColorHSB(t, 1.0f, 1.0f).toRGB());
    drawCircle(x, y, 30);
}
```

### Transform

```cpp
void tcApp::draw() {
    clear(0);

    pushMatrix();
    translate(getWidth() / 2, getHeight() / 2);
    rotate(getElapsedTime());

    setColor(255);
    drawRect(-50, -50, 100, 100);

    popMatrix();
}
```

### Mouse Interaction

```cpp
void tcApp::mousePressed(int x, int y, int button) {
    if (button == MOUSE_BUTTON_LEFT) {
        particles.push_back({(float)x, (float)y});
    }
}

void tcApp::mouseDragged(int x, int y, int button) {
    particles.push_back({(float)x, (float)y});
}
```

### Keyboard Input

```cpp
void tcApp::keyPressed(int key) {
    if (key == ' ') {
        paused = !paused;
    } else if (key == KEY_ESCAPE) {
        exit(0);
    } else if (key == 'f') {
        toggleFullscreen();
    }
}
```
