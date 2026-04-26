# TrussC API Reference
            
            Complete API reference. This document is auto-generated from `api-definition.yaml`.
            
            For the latest interactive reference, visit [trussc.org/reference](https://trussc.org/reference/).
            
            ## Lifecycle

```cpp
void setup()                             // Called once at start
void update()                            // Called every frame before draw
void draw()                              // Called every frame after update
```

## Events

```cpp
void mousePressed(float x, float y, int button) // Mouse button pressed
void mouseReleased(float x, float y, int button) // Mouse button released
void mouseMoved(float x, float y)        // Mouse moved
void mouseDragged(float x, float y, int button) // Mouse dragged
void keyPressed(int key)                 // Key pressed. Use KEY_* constants for special keys, or uppercase char literals for printable keys (e.g. key == 'A', key == '1')
void keyReleased(int key)                // Key released
void windowResized(int width, int height) // Window resized
```

## Graphics - Color

```cpp
void clear()                             // Clear screen. No args = transparent black (0,0,0,0)
void clear(float gray)                   // Clear screen. No args = transparent black (0,0,0,0)
void clear(float r, float g, float b)    // Clear screen. No args = transparent black (0,0,0,0)
void setColor(float gray)                // Set drawing color (0.0-1.0)
void setColor(float r, float g, float b) // Set drawing color (0.0-1.0)
void setColor(float r, float g, float b, float a) // Set drawing color (0.0-1.0)
void setColorHSB(float h, float s, float b) // Set color from HSB (H: 0-1)
void setColorOKLCH(float L, float C, float H) // Set color from OKLCH
void setColorOKLab(float L, float a, float b) // Set color from OKLab
```

## Graphics - Shapes

```cpp
void drawRect(float x, float y, float w, float h) // Draw rectangle
void drawRect(Vec3 pos, float w, float h) // Draw rectangle
void drawRect(Vec3 pos, Vec2 size)       // Draw rectangle
void drawRectRounded(float x, float y, float w, float h, float radius) // Draw rounded rectangle (circular arc corners)
void drawRectRounded(Vec3 pos, Vec2 size, float radius) // Draw rounded rectangle (circular arc corners)
void drawRectSquircle(float x, float y, float w, float h, float radius) // Draw squircle rectangle (curvature-continuous corners, iOS-style)
void drawRectSquircle(Vec3 pos, Vec2 size, float radius) // Draw squircle rectangle (curvature-continuous corners, iOS-style)
void drawCircle(float x, float y, float radius) // Draw circle
void drawCircle(Vec3 center, float radius) // Draw circle
void drawEllipse(float x, float y, float w, float h) // Draw ellipse
void drawEllipse(Vec3 center, float rx, float ry) // Draw ellipse
void drawEllipse(Vec3 center, Vec2 radii) // Draw ellipse
void drawPoint(float x, float y)         // Draw a single point
void drawPoint(Vec3 pos)                 // Draw a single point
void drawLine(float x1, float y1, float x2, float y2) // Draw line (2D or 3D)
void drawLine(float x1, float y1, float z1, float x2, float y2, float z2) // Draw line (2D or 3D)
void drawLine(Vec3 p1, Vec3 p2)          // Draw line (2D or 3D)
void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) // Draw triangle
void drawTriangle(Vec3 p1, Vec3 p2, Vec3 p3) // Draw triangle
void drawBox(float size)                 // Draw 3D box (respects fill/noFill)
void drawBox(float w, float h, float d)  // Draw 3D box (respects fill/noFill)
void drawBox(float x, float y, float z, float size) // Draw 3D box (respects fill/noFill)
void drawBox(float x, float y, float z, float w, float h, float d) // Draw 3D box (respects fill/noFill)
void drawBox(Vec3 pos, float size)       // Draw 3D box (respects fill/noFill)
void drawBox(Vec3 pos, float w, float h, float d) // Draw 3D box (respects fill/noFill)
void drawSphere(float radius)            // Draw 3D sphere (respects fill/noFill)
void drawSphere(float x, float y, float z, float radius) // Draw 3D sphere (respects fill/noFill)
void drawSphere(Vec3 pos, float radius)  // Draw 3D sphere (respects fill/noFill)
void drawCone(float radius, float height) // Draw 3D cone (respects fill/noFill)
void drawCone(float x, float y, float z, float radius, float height) // Draw 3D cone (respects fill/noFill)
void drawCone(Vec3 pos, float radius, float height) // Draw 3D cone (respects fill/noFill)
void beginShape()                        // Begin drawing a shape
void vertex(float x, float y)            // Add a vertex
void vertex(float x, float y, float z)   // Add a vertex
void vertex(const Vec2& v)               // Add a vertex
void vertex(const Vec3& v)               // Add a vertex
void endShape(bool close = false)        // End drawing a shape
void beginStroke()                       // Begin drawing a stroke (uses StrokeMesh internally)
void endStroke(bool close = false)       // End drawing a stroke
void beginLines()                        // Begin batch line drawing. Add vertex pairs with vertex(), then call endLines(). Each pair of vertices draws one independent line segment. Use setColor() between vertices for per-line colors.
void endLines()                          // End batch line drawing and render all accumulated line segments
void drawStroke(float x1, float y1, float x2, float y2) // Draw a single stroke segment (thick line with cap/join)
void drawStroke(const Vec2& p1, const Vec2& p2) // Draw a single stroke segment (thick line with cap/join)
void drawBitmapString(const string& text, float x, float y) // Draw text
void setTextAlign(TextAlign horizontal)  // Set text alignment
void setTextAlign(TextAlign horizontal, TextAlign vertical) // Set text alignment
TextAlign getTextAlignH()                // Get horizontal text alignment
TextAlign getTextAlignV()                // Get vertical text alignment
float getBitmapFontHeight()              // Get bitmap font height
float getBitmapStringWidth(const string& text) // Get text width
float getBitmapStringHeight(const string& text) // Get text height
Rect getBitmapStringBBox(const string& text, float x, float y) // Get text bounding box
void setBitmapLineHeight(float height)   // Set line height for bitmap string newlines (default: 16)
float getBitmapLineHeight()              // Get line height for bitmap string newlines
void setFps(float fps)                   // Set target frame rate (VSYNC = -1.0)
```

## Graphics - Style

```cpp
void fill()                              // Enable fill mode (shapes are solid, no outline)
void noFill()                            // Enable stroke mode (shapes show outline only)
void setStrokeWeight(float weight)       // Set stroke width
float getStrokeWeight()                  // Get current stroke width
void setStrokeCap(StrokeCap cap)         // Set stroke cap style (Butt, Round, Square)
StrokeCap getStrokeCap()                 // Get current stroke cap style
void setStrokeJoin(StrokeJoin join)      // Set stroke join style (Miter, Round, Bevel)
StrokeJoin getStrokeJoin()               // Get current stroke join style
bool isFillEnabled()                     // Check if fill mode is enabled
bool isStrokeEnabled()                   // Check if stroke mode is enabled
void setCircleResolution(int resolution) // Set circle segment count
int getCircleResolution()                // Get circle segment count
void pushStyle()                         // Save current style state (color, stroke, fill)
void popStyle()                          // Restore previous style state
void resetStyle()                        // Reset style to default values (white color, fill enabled, stroke disabled)
Color getColor()                         // Get current fill color
```

## Transform

```cpp
void translate(float x, float y)         // Move origin
void translate(float x, float y, float z) // Move origin
void rotate(float radians)               // Rotate by radians (single axis, euler angles, or quaternion)
void rotate(float x, float y, float z)   // Rotate by radians (single axis, euler angles, or quaternion)
void rotate(Vec3 euler)                  // Rotate by radians (single axis, euler angles, or quaternion)
void rotate(Quaternion quat)             // Rotate by radians (single axis, euler angles, or quaternion)
void rotateDeg(float degrees)            // Rotate by degrees
void rotateDeg(float x, float y, float z) // Rotate by degrees
void rotateDeg(Vec3 euler)               // Rotate by degrees
void rotateX(float radians)              // Rotate around X axis
void rotateY(float radians)              // Rotate around Y axis
void rotateZ(float radians)              // Rotate around Z axis
void rotateXDeg(float degrees)           // Rotate around X axis (degrees)
void rotateYDeg(float degrees)           // Rotate around Y axis (degrees)
void rotateZDeg(float degrees)           // Rotate around Z axis (degrees)
void scale(float s)                      // Scale
void scale(float sx, float sy)           // Scale
void pushMatrix()                        // Save transform state
void popMatrix()                         // Restore transform state
Mat4 getCurrentMatrix()                  // Get current transformation matrix
void resetMatrix()                       // Reset transformation matrix to identity
void setMatrix(const Mat4& mat)          // Set transformation matrix directly
```

## Window & Input

```cpp
int getWindowWidth()                     // Get canvas width
int getWindowHeight()                    // Get canvas height
Vec2 getWindowSize()                     // Get canvas size as Vec2
float getMouseX()                        // Get mouse X position
float getMouseY()                        // Get mouse Y position
Vec2 getMousePos()                       // Get mouse position as Vec2
Vec2 getGlobalMousePos()                 // Get global mouse position as Vec2
bool isMousePressed()                    // Is mouse button pressed
bool isKeyPressed(int key)               // Is specific key currently pressed
void showCursor()                        // Show the mouse cursor (default)
void hideCursor()                        // Hide the mouse cursor
void setCursor(Cursor cursor)            // Set the mouse cursor shape
Cursor getCursor()                       // Get the current mouse cursor shape
void bindCursorImage(Cursor cursor, int width, int height, const unsigned char* pixels, int hotspotX = 0, int hotspotY = 0) // Bind a custom image to a cursor slot (RGBA pixels or Image)
void bindCursorImage(Cursor cursor, const Image& image, int hotspotX = 0, int hotspotY = 0) // Bind a custom image to a cursor slot (RGBA pixels or Image)
void unbindCursorImage(Cursor cursor)    // Unbind a custom cursor image, restoring the system default
```

## Time - Frame

```cpp
double getDeltaTime()                    // Seconds since last frame
double getFrameRate()                    // Current FPS
uint64_t getFrameCount()                 // Total frames rendered
```

## Memory

```cpp
int getSokolMemoryBytes()                // Total bytes allocated by sokol libraries
int getSokolMemoryAllocs()               // Number of active allocations in sokol libraries
void releaseSglBuffers()                 // Release sokol_gl vertex/command buffers (auto re-allocated on next draw)
```

## Platform

```cpp
bool Platform::isWeb()                   // True on Web (Emscripten / WASM)
bool Platform::isMacOS()                 // True on macOS
bool Platform::isIOS()                   // True on iOS
bool Platform::isWindows()               // True on Windows
bool Platform::isAndroid()               // True on Android
bool Platform::isLinux()                 // True on Linux (desktop, excludes Android)
bool Platform::isApple()                 // True on any Apple platform (macOS or iOS)
bool Platform::isMobile()                // True on mobile (iOS or Android)
bool Platform::isDesktop()               // True on desktop (macOS, Windows, or Linux)
const char* Platform::name()             // Short platform name: "web" / "macos" / "ios" / "windows" / "android" / "linux" / "unknown"
```

## Graphics Backend

```cpp
bool GraphicsBackend::isOpenGL()         // True when running on OpenGL (core or GLES3)
bool GraphicsBackend::isMetal()          // True when running on Apple Metal
bool GraphicsBackend::isD3D11()          // True when running on Direct3D 11
bool GraphicsBackend::isWebGPU()         // True when running on WebGPU
bool GraphicsBackend::isWebGL2()         // True when running on WebGL2 (GLES3 under Emscripten)
bool GraphicsBackend::isVulkan()         // True when running on Vulkan
const char* GraphicsBackend::name()      // Short backend name: "opengl" / "gles3" / "webgl2" / "d3d11" / "metal" / "webgpu" / "vulkan" / "dummy" / "unknown"
```

## Build Info

```cpp
const char* BuildInfo::date()            // Build date in "YYYY-MM-DD" form (local time, CMake configure time)
const char* BuildInfo::time()            // Build time in "HH:MM:SS" form (local time)
const char* BuildInfo::dateTime()        // Build date-time in "YYYY-MM-DD HH:MM:SS" form (local time)
int64_t BuildInfo::timestamp()           // Build timestamp as Unix seconds (UTC)
int BuildInfo::year()                    // Build year (e.g. 2026)
int BuildInfo::month()                   // Build month (1-12)
int BuildInfo::day()                     // Build day of month (1-31)
int BuildInfo::hour()                    // Build hour (0-23)
int BuildInfo::minute()                  // Build minute (0-59)
int BuildInfo::second()                  // Build second (0-59)
```

## Time - Elapsed

```cpp
float getElapsedTimef()                  // Elapsed seconds (float)
float getElapsedTime()                   // Elapsed seconds (alias for getElapsedTimef)
uint64_t getElapsedTimeMillis()          // Elapsed milliseconds (int64)
uint64_t getElapsedTimeMicros()          // Elapsed microseconds (int64)
void resetElapsedTimeCounter()           // Reset elapsed time
```

## Time - System

```cpp
uint64_t getSystemTimeMillis()           // Unix time in milliseconds
uint64_t getSystemTimeMicros()           // Unix time in microseconds
uint64_t getUnixTime()                   // Current Unix timestamp in seconds
string getTimestampString()              // Formatted timestamp
string getTimestampString(const string& format) // Formatted timestamp
```

## Time - Current

```cpp
int getSeconds()                         // Current seconds (0-59)
int getMinutes()                         // Current minutes (0-59)
int getHours()                           // Current hours (0-23)
int getYear()                            // Current year
int getMonth()                           // Current month (1-12)
int getDay()                             // Current day (1-31)
int getWeekday()                         // Weekday (0=Sun, 6=Sat)
```

## Math - Random & Noise

```cpp
float random()                           // Random number
float random(float max)                  // Random number
float random(float min, float max)       // Random number
int randomInt(int max)                   // Random integer
int randomInt(int min, int max)          // Random integer
void randomSeed(unsigned int seed)       // Set random seed
float noise(float x)                     // Perlin noise
float noise(float x, float y)            // Perlin noise
float noise(float x, float y, float z)   // Perlin noise
float signedNoise(float x)               // Perlin noise (-1.0 to 1.0)
float signedNoise(float x, float y)      // Perlin noise (-1.0 to 1.0)
float signedNoise(float x, float y, float z) // Perlin noise (-1.0 to 1.0)
float signedNoise(float x, float y, float z, float w) // Perlin noise (-1.0 to 1.0)
float fbm(float x, float y, int octaves = 4, float lacunarity = 2.0, float gain = 0.5) // Fractal Brownian Motion noise
float fbm(float x, float y, float z, int octaves = 4, float lacunarity = 2.0, float gain = 0.5) // Fractal Brownian Motion noise
```

## Math - Interpolation

```cpp
float clamp(float v, float min, float max) // Clamp value to range
float remap(float v, float inMin, float inMax, float outMin, float outMax) // Remap value from one range to another
```

## Math - Trigonometry

```cpp
float sin(float x)                       // Sine
float cos(float x)                       // Cosine
float tan(float x)                       // Tangent
float asin(float x)                      // Arc sine
float acos(float x)                      // Arc cosine
float atan(float x)                      // Arc tangent
float atan2(float y, float x)            // Arc tangent of y/x
float deg2rad(float degrees)             // Degrees to radians
float rad2deg(float radians)             // Radians to degrees
```

## Math - General

```cpp
float abs(float x)                       // Absolute value
float sqrt(float x)                      // Square root
float sq(float x)                        // Square (x*x)
float pow(float x, float y)              // Power (x^y)
float log(float x)                       // Natural logarithm
float exp(float x)                       // Exponential (e^x)
float min(float a, float b)              // Minimum
float max(float a, float b)              // Maximum
float floor(float x)                     // Round down
float ceil(float x)                      // Round up
float round(float x)                     // Round to nearest
float fmod(float x, float y)             // Floating-point modulo
float sign(float x)                      // Sign (-1, 0, 1)
float fract(float x)                     // Fractional part
float wrap(float value, float min, float max) // Wrap value within range [min, max)
float angleDifference(float angle1, float angle2) // Shortest angle difference in radians [-TAU/2, TAU/2]
float angleDifferenceDeg(float deg1, float deg2) // Shortest angle difference in degrees [-180, 180]
```

## Math - Geometry

```cpp
float dist(float x1, float y1, float x2, float y2) // Distance between points
float distSquared(float x1, float y1, float x2, float y2) // Squared distance
```

## Window & System

```cpp
void setWindowTitle(const string& title) // Set window title
void setWindowSize(int width, int height) // Set window size
void toggleFullscreen()                  // Toggle fullscreen mode
void setClipboardString(const string& text) // Copy text to clipboard
string getClipboardString()              // Get text from clipboard
bool isFullscreen()                      // Check if window is fullscreen
void setFullscreen(bool fullscreen)      // Set fullscreen mode
```

## Utility

```cpp
void logNotice(const string& message)    // Print to console
string toString(value)                   // Convert to string
void beep()                              // Play a beep sound
void beep(float frequency)               // Play a beep sound
int toInt(const string& str)             // Convert string to int
float toFloat(const string& str)         // Convert string to float
vector<string> splitString(const string& source, const string& delimiter) // Split string by delimiter
string joinString(const vector<string>& elements, const string& delimiter) // Join strings with delimiter
void stringReplace(string& input, const string& searchStr, const string& replaceStr) // Replace substring in place
string toLower(const string& src)        // Convert to lower case
string toUpper(const string& src)        // Convert to upper case
```

## File

```cpp
string getDataPath(const string& path)   // Get full path relative to data directory
string getAbsolutePath(const string& path) // Get absolute path
string getFileName(const string& path)   // Get filename from path
string getBaseName(const string& path)   // Get filename without extension
string getFileExtension(const string& path) // Get file extension without dot
string getParentDirectory(const string& path) // Get parent directory
string joinPath(const string& dir, const string& file) // Join directory and filename
bool fileExists(const string& path)      // Check if file exists
bool directoryExists(const string& path) // Check if directory exists
vector<string> listDirectory(const string& path) // List files in directory
FileWriter@ createFileWriter()           // Create a file writer (TrussSketch factory)
FileReader@ createFileReader()           // Create a file reader (TrussSketch factory)
```

## Sound

```cpp
Sound@ createSound()                     // Create a sound player (TrussSketch factory)
bool load(const string& path)            // Load sound file
void play()                              // Play sound
void stop()                              // Stop sound
void setVolume(float vol)                // Set volume (0.0-1.0)
void setLoop(bool loop)                  // Enable/disable looping
```

## ChipSound

```cpp
ChipSoundNote& wave(Wave type)           // Set wave type (Sin, Square, Triangle, Sawtooth, Noise, PinkNoise)
ChipSoundNote& hz(float frequency)       // Set frequency in Hz
ChipSoundNote& duration(float seconds)   // Set note duration in seconds
ChipSoundNote& volume(float vol)         // Set volume (0.0-1.0)
ChipSoundNote& attack(float seconds)     // Set attack time (ADSR envelope)
ChipSoundNote& decay(float seconds)      // Set decay time (ADSR envelope)
ChipSoundNote& sustain(float level)      // Set sustain level (0.0-1.0)
ChipSoundNote& release(float seconds)    // Set release time (ADSR envelope)
ChipSoundNote& adsr(float a, float d, float s, float r) // Set ADSR envelope (attack, decay, sustain, release)
Sound@ build()                           // Build and return Sound object from note
ChipSoundBundle@ createChipBundle()      // Create a chip sound bundle for sequencing multiple notes
ChipSoundBundle& add(const ChipSoundNote& note, float time) // Add a note at specified time (seconds)
ChipSoundBundle& clear()                 // Clear all notes from bundle
float getDuration()                      // Get the total duration of the bundle
```

## Font

```cpp
Font@ createFont()                       // Create a TrueType font
bool load(const string& path, int size)  // Load TTF font file
bool isLoaded()                          // Check if font is loaded
void drawString(const string& text, float x, float y) // Draw text at position
float getWidth(const string& text)       // Get text width in pixels
float getHeight(const string& text)      // Get text height in pixels
float getLineHeight()                    // Get line height
int getSize()                            // Get font size
```

## Animation

```cpp
float ease(float t, EaseType type, EaseMode mode) // Apply easing to value (0-1)
float easeIn(float t, EaseType type)     // Apply ease-in to value (0-1)
float easeOut(float t, EaseType type)    // Apply ease-out to value (0-1)
float easeInOut(float t, EaseType type)  // Apply ease-in-out to value (0-1)
Tween@ createTween()                     // Create a tween object
Tween@ from(float value)                 // Set start value
Tween@ to(float value)                   // Set end value
Tween@ duration(float seconds)           // Set animation duration
Tween@ ease(EaseType type)               // Set easing type
Tween@ start()                           // Start animation (chainable)
Tween@ pause()                           // Pause animation (chainable)
Tween@ resume()                          // Resume animation (chainable)
Tween@ reset()                           // Reset animation (chainable)
Tween@ finish()                          // Jump to end (chainable)
Tween@ loop(int count = -1)              // Set loop count (-1=infinite, 0=none, N=repeat N times)
Tween@ yoyo(bool enable = true)          // Enable yoyo (reverse direction each loop)
float getValue()                         // Get current tween value
float getProgress()                      // Get progress (0-1)
float getElapsed()                       // Get elapsed time
float getDuration()                      // Get duration
bool isPlaying()                         // Check if playing
bool isComplete()                        // Check if complete
float getStart()                         // Get start value
float getEnd()                           // Get end value
int getLoopCount()                       // Get number of completed loop iterations
```

## Types - Vec2

```cpp
Vec2 Vec2_fromAngle(float radians)       // Create Vec2 from angle
Vec2 Vec2_fromAngle(float radians, float length) // Create Vec2 from angle
```

## Types - Color

```cpp
ColorHSB toHSB()                         // Convert to HSB color space (H: 0-1, S: 0-1, B: 0-1)
ColorOKLab toOKLab()                     // Convert to OKLab color space (perceptually uniform)
ColorOKLCH toOKLCH()                     // Convert to OKLCH color space (L: 0-1, C: 0-0.4, H: 0-1)
Color Color_fromHSB(float h, float s, float b) // Create Color from HSB (H: 0-1, S: 0-1, B: 0-1)
Color Color_fromHSB(float h, float s, float b, float a) // Create Color from HSB (H: 0-1, S: 0-1, B: 0-1)
Color colorFromHSB(float h, float s, float b) // Create Color from HSB (alias for Color_fromHSB)
Color colorFromHSB(float h, float s, float b, float a) // Create Color from HSB (alias for Color_fromHSB)
Color Color_fromOKLCH(float L, float C, float H) // Create Color from OKLCH
Color Color_fromOKLCH(float L, float C, float H, float a) // Create Color from OKLCH
Color Color_fromOKLab(float L, float a, float b) // Create Color from OKLab
Color Color_fromOKLab(float L, float a, float b, float alpha) // Create Color from OKLab
```

## Types - ColorHSB

```cpp
ColorHSB(float h, float s, float b)      // HSB color type (H: 0-1, S: 0-1, B: 0-1). Use toRGB() to convert to Color
ColorHSB(float h, float s, float b, float a) // HSB color type (H: 0-1, S: 0-1, B: 0-1). Use toRGB() to convert to Color
Color toRGB()                            // Convert ColorHSB to Color (RGB)
ColorHSB lerp(ColorHSB target, float t)  // Interpolate in HSB space (shortest hue path)
```

## Types - ColorOKLCH

```cpp
ColorOKLCH(float L, float C, float H)    // OKLCH color type (L: 0-1, C: 0-0.4, H: 0-1). Perceptually uniform
ColorOKLCH(float L, float C, float H, float a) // OKLCH color type (L: 0-1, C: 0-0.4, H: 0-1). Perceptually uniform
Color toRGB()                            // Convert ColorOKLCH to Color (RGB)
ColorOKLCH lerp(ColorOKLCH target, float t) // Interpolate in OKLCH space (shortest hue path, perceptually uniform)
```

## 3D Setup

```cpp
void setupScreenPerspective()            // Set up perspective projection (oF-style default 3D)
void setupScreenPerspective(float fovDeg) // Set up perspective projection (oF-style default 3D)
void setupScreenPerspective(float fovDeg, float nearDist, float farDist) // Set up perspective projection (oF-style default 3D)
void setupScreenOrtho()                  // Set up orthographic projection (2D mode)
void setupScreenFov(float fovDeg)        // Set up screen projection with specified FOV (0 = ortho, >0 = perspective)
void setupScreenFov(float fovDeg, float nearDist, float farDist) // Set up screen projection with specified FOV (0 = ortho, >0 = perspective)
void setDefaultScreenFov(float fovDeg)   // Set default screen FOV (applied at frame start)
float getDefaultScreenFov()              // Get current default screen FOV
```

## 3D Camera

```cpp
EasyCam@ createEasyCam()                 // Create an EasyCam instance
void begin()                             // Apply camera transform (start 3D mode)
void end()                               // Restore previous transform (end 3D mode)
void reset()                             // Reset camera to default position
void setTarget(float x, float y, float z) // Set camera look-at target
void setTarget(const Vec3 &in target)    // Set camera look-at target
Vec3 getTarget()                         // Get camera look-at target
void setDistance(float distance)         // Set distance from target
float getDistance()                      // Get distance from target
void setFov(float radians)               // Set field of view in radians
float getFov()                           // Get field of view in radians
void setFovDeg(float degrees)            // Set field of view in degrees
void setNearClip(float nearClip)         // Set near clipping plane
void setFarClip(float farClip)           // Set far clipping plane
void enableMouseInput()                  // Enable mouse input for camera control
void disableMouseInput()                 // Disable mouse input for camera control
bool isMouseInputEnabled()               // Check if mouse input is enabled
void mousePressed(int x, int y, int button) // Handle mouse press event
void mouseReleased(int x, int y, int button) // Handle mouse release event
void mouseDragged(int x, int y, int button) // Handle mouse drag event
void mouseScrolled(float dx, float dy)   // Handle mouse scroll event (for zoom)
Vec3 getPosition()                       // Get camera position
void setSensitivity(float sensitivity)   // Set rotation sensitivity
void setZoomSensitivity(float sensitivity) // Set zoom sensitivity
void setPanSensitivity(float sensitivity) // Set pan sensitivity
```

## Lighting & PBR

```cpp
void addLight(Light& light)              // Add a light to the scene
void removeLight(Light& light)           // Remove a light from the scene
void clearLights()                       // Remove all lights from the scene
void setMaterial(Material& material)     // Set material for subsequent mesh draws (activates PBR)
void clearMaterial()                     // Clear material (return to default rendering)
void setCameraPosition(const Vec3& pos)  // Set camera position for specular calculation
void setCameraPosition(float x, float y, float z) // Set camera position for specular calculation
void setEnvironment(Environment& env)    // Set IBL environment for PBR ambient lighting
void clearEnvironment()                  // Clear IBL environment
void beginShadowPass(Light& light)       // Begin shadow depth pass from the light's point of view
void endShadowPass()                     // End shadow depth pass
void shadowDraw(const Mesh& mesh)        // Draw a mesh into the shadow depth pass (depth only)
```

## Math - 3D

```cpp
Mat4 Mat4_identity()                     // Create an identity matrix
Mat4 Mat4_translate(float x, float y, float z) // Create a translation matrix
Mat4 Mat4_translate(Vec3 v)              // Create a translation matrix
Mat4 Mat4_rotateX(float radians)         // Create X-axis rotation matrix
Mat4 Mat4_rotateY(float radians)         // Create Y-axis rotation matrix
Mat4 Mat4_rotateZ(float radians)         // Create Z-axis rotation matrix
Mat4 Mat4_scale(float s)                 // Create a scaling matrix
Mat4 Mat4_scale(float sx, float sy, float sz) // Create a scaling matrix
Mat4 Mat4_lookAt(Vec3 eye, Vec3 target, Vec3 up) // Create a view matrix
Mat4 Mat4_ortho(float left, float right, float bottom, float top, float nearPlane, float farPlane) // Create an orthographic projection matrix
Mat4 Mat4_perspective(float fovY, float aspect, float nearPlane, float farPlane) // Create a perspective projection matrix
Quaternion Quaternion_identity()         // Create an identity quaternion
Quaternion Quaternion_fromAxisAngle(Vec3 axis, float radians) // Create quaternion from axis-angle
Quaternion Quaternion_fromEuler(float pitch, float yaw, float roll) // Create quaternion from Euler angles
Quaternion Quaternion_fromEuler(Vec3 euler) // Create quaternion from Euler angles
Quaternion Quaternion_slerp(Quaternion a, Quaternion b, float t) // Spherical linear interpolation
```

## Graphics - Advanced

```cpp
void drawMesh(Mesh mesh)                 // Draw a mesh
void drawPolyline(Polyline polyline)     // Draw a polyline
Mesh createBox(float size)               // Create a box mesh
Mesh createBox(float w, float h, float d) // Create a box mesh
Mesh createSphere(float radius, int res = 20) // Create a sphere mesh
void drawTexture(const Texture& tex, float x, float y) // Draw a texture
void drawTexture(const Texture& tex, float x, float y, float w, float h) // Draw a texture
```

## Graphics - Texture & GPU

```cpp
Texture@ createTexture()                 // Create a texture (TrussSketch factory)
bool load(const string& path)            // Load image from file
void bind(int slot = 0)                  // Bind texture
void unbind(int slot = 0)                // Unbind texture
int getWidth()                           // Get width
int getHeight()                          // Get height
```

## Graphics - FBO

```cpp
Fbo@ createFbo()                         // Create an FBO (TrussSketch factory)
void allocate(int w, int h)              // Allocate buffer
void begin()                             // Begin drawing to FBO. No args = preserve previous content. With args = clear with specified color
void begin(float r, float g, float b, float a = 1.0) // Begin drawing to FBO. No args = preserve previous content. With args = clear with specified color
void end()                               // End drawing to FBO
Texture& getTexture()                    // Get internal texture
```

## Types - Pixels

```cpp
Pixels@ createPixels()                   // Create pixel buffer (TrussSketch factory)
void allocate(int w, int h, int channels) // Allocate memory
Color getColor(int x, int y)             // Get color at pixel
void setColor(int x, int y, const Color& c) // Set color at pixel
```

## Types - Mesh

```cpp
Mesh@ createMesh()                       // Create a new Mesh
void setMode(int mode)                   // Set primitive mode (MESH_TRIANGLES, etc.)
void addVertex(float x, float y, float z) // Add a vertex
void addVertex(Vec3 v)                   // Add a vertex
void addColor(float r, float g, float b, float a) // Add a color for the vertex
void addColor(Color c)                   // Add a color for the vertex
void addTexCoord(float u, float v)       // Add a texture coordinate
void addNormal(float x, float y, float z) // Add a normal vector
void addIndex(int index)                 // Add an index
void addTriangle(int i1, int i2, int i3) // Add a triangle (3 indices)
void clear()                             // Clear all data
void draw()                              // Draw the mesh
```

## Types - Path

```cpp
Path@ createPath()                       // Create a new Path
void addVertex(float x, float y)         // Add a vertex
void lineTo(float x, float y)            // Add a line segment to point
void bezierTo(float cx1, float cy1, float cx2, float cy2, float x, float y) // Add a cubic bezier curve
void quadBezierTo(float cx, float cy, float x, float y) // Add a quadratic bezier curve
void curveTo(float x, float y)           // Add a Catmull-Rom curve segment
void arc(float x, float y, float rX, float rY, float angleBegin, float angleEnd) // Add an arc
void close()                             // Close the shape
```

## Types - StrokeMesh

```cpp
StrokeMesh@ createStrokeMesh()           // Create a new StrokeMesh instance
StrokeMesh& setWidth(float width)        // Set stroke width (method chaining)
StrokeMesh& setColor(const Color &in color) // Set stroke color (method chaining)
StrokeMesh& setCapType(int type)         // Set cap type: Butt, Round, Square (method chaining)
StrokeMesh& setJoinType(int type)        // Set join type: Miter, Round, Bevel (method chaining)
StrokeMesh& setMiterLimit(float limit)   // Set miter limit for sharp corners (method chaining)
StrokeMesh& addVertex(float x, float y)  // Add a vertex (method chaining)
StrokeMesh& addVertex(float x, float y, float z) // Add a vertex (method chaining)
StrokeMesh& addVertex(const Vec2 &in v)  // Add a vertex (method chaining)
StrokeMesh& addVertex(const Vec3 &in v)  // Add a vertex (method chaining)
StrokeMesh& addVertexWithWidth(float x, float y, float width) // Add a vertex with variable width (method chaining)
StrokeMesh& setShape(Path@ path)         // Set shape from Path (method chaining)
StrokeMesh& setClosed(bool closed)       // Set whether the stroke is closed (method chaining)
StrokeMesh& clear()                      // Clear all vertices (method chaining)
void update()                            // Update the internal mesh (required before draw)
void draw()                              // Draw the stroke mesh
```

## Video

```cpp
VideoPlayer@ createVideoPlayer()         // Create a video player (TrussSketch factory)
bool load(const string& path)            // Load a video file
void close()                             // Close the video and release resources
bool isLoaded()                          // Check if a video is loaded
void play()                              // Start or resume playback
void stop()                              // Stop playback and reset to beginning
void setPaused(bool paused)              // Pause or resume playback
void togglePause()                       // Toggle pause state
void update()                            // Update the video frame. Call once per frame in update()
bool isPlaying()                         // Check if video is currently playing (not paused)
bool isPaused()                          // Check if video is paused
bool isFrameNew()                        // Check if a new frame is available since last update
bool isDone()                            // Check if playback has reached the end
float getWidth()                         // Get video width in pixels
float getHeight()                        // Get video height in pixels
float getDuration()                      // Get total duration in seconds
float getPosition()                      // Get current position (0.0 to 1.0)
void setPosition(float pct)              // Seek to position (0.0 to 1.0)
float getCurrentTime()                   // Get current playback time in seconds
void setCurrentTime(float seconds)       // Seek to a specific time in seconds
void setVolume(float vol)                // Set audio volume (0.0 to 1.0)
float getVolume()                        // Get current volume
void setSpeed(float speed)               // Set playback speed (1.0 = normal, 2.0 = double speed)
float getSpeed()                         // Get current playback speed
void setPan(float pan)                   // Set stereo pan (-1.0 left, 0.0 center, 1.0 right)
float getPan()                           // Get current stereo pan
void setLoop(bool loop)                  // Enable/disable looping
bool isLoop()                            // Check if looping is enabled
int getCurrentFrame()                    // Get current frame number
int getTotalFrames()                     // Get total number of frames
void setFrame(int frame)                 // Seek to a specific frame number
void nextFrame()                         // Advance to the next frame
void previousFrame()                     // Go back to the previous frame
void firstFrame()                        // Go to the first frame
void setGammaCorrection(float gamma)     // Set gamma correction (1.0 = none). Use ~0.45 to brighten on platforms with dark output (e.g. macOS AVFoundation)
float getGammaCorrection()               // Get current gamma correction value
void setUseHwAccel(bool enable)          // Enable/disable hardware decoding. Must be called before load(). Default: true. When enabled, the player probes available HW backends (VAAPI, V4L2M2M, CUDA, etc.) and falls back to software if none are available. Currently affects the Linux backend only.
bool getUseHwAccel()                     // Get HW accel preference (not the actual backend — use isUsingHwAccel() for that)
bool isUsingHwAccel()                    // Check if hardware decoding is currently active (after load)
string getHwAccelName()                  // Get the name of the active decode backend. Returns 'vaapi', 'v4l2m2m', 'cuda', 'videotoolbox', 'mediafoundation', 'software', or 'none'
void setResyncThreshold(float seconds)   // Set the maximum video/audio drift before hard re-sync. When drift exceeds this threshold, video seeks to match audio position instead of catching up frame-by-frame. Set to 0 to disable. Default: 0.5s. Primarily affects Linux (FFmpeg) backend.
float getResyncThreshold()               // Get the current resync threshold in seconds
bool hasAudio()                          // Check if the loaded video has an audio track
```

## Constants

```cpp
TAU                          // 6.283... (Full circle (2*PI))
HALF_TAU                     // 3.141... (Half circle (PI))
QUARTER_TAU                  // 1.570... (Quarter circle (PI/2))
PI                           // 3.141... (Pi (use TAU instead))
StrokeCap::Butt              // 0 (Flat line cap (no extension))
StrokeCap::Round             // 1 (Rounded line cap)
StrokeCap::Square            // 2 (Square line cap (extends by half stroke width))
StrokeJoin::Miter            // 0 (Sharp corner join)
StrokeJoin::Round            // 1 (Rounded corner join)
StrokeJoin::Bevel            // 2 (Beveled corner join)
FONT_SANS                    // string (System sans-serif font path (CDN URL on Web))
FONT_SERIF                   // string (System serif font path (CDN URL on Web))
FONT_MONO                    // string (System monospace font path (CDN URL on Web))
Wave::Sin                    // 0 (Sine wave (smooth, pure tone))
Wave::Square                 // 1 (Square wave (harsh, 8-bit style))
Wave::Triangle               // 2 (Triangle wave (softer than square))
Wave::Sawtooth               // 3 (Sawtooth wave (bright, buzzy))
Wave::Noise                  // 4 (White noise)
Wave::PinkNoise              // 5 (Pink noise (1/f noise, more natural))
Wave::Silent                 // 6 (Silent (no sound))
Cursor::Default              // 0 (System default cursor)
Cursor::Arrow                // 1 (Arrow cursor)
Cursor::IBeam                // 2 (Text input cursor)
Cursor::Crosshair            // 3 (Crosshair cursor)
Cursor::Hand                 // 4 (Pointing hand cursor)
Cursor::ResizeEW             // 5 (East-west resize cursor)
Cursor::ResizeNS             // 6 (North-south resize cursor)
Cursor::ResizeNWSE           // 7 (NW-SE diagonal resize cursor)
Cursor::ResizeNESW           // 8 (NE-SW diagonal resize cursor)
Cursor::ResizeAll            // 9 (Move/resize all directions cursor)
Cursor::NotAllowed           // 10 (Not allowed cursor)
Cursor::Custom0              // 11 (Custom cursor slot 0 (bind image first))
Cursor::Custom1              // 12 (Custom cursor slot 1)
Cursor::Custom2              // 13 (Custom cursor slot 2)
Cursor::Custom3              // 14 (Custom cursor slot 3)
KEY_SPACE                    // 32 (Space key)
KEY_ESCAPE                   // 256 (Escape key)
KEY_ENTER                    // 257 (Enter/Return key)
KEY_TAB                      // 258 (Tab key)
KEY_BACKSPACE                // 259 (Backspace key)
KEY_DELETE                   // 261 (Delete key)
KEY_RIGHT                    // 262 (Right arrow key)
KEY_LEFT                     // 263 (Left arrow key)
KEY_DOWN                     // 264 (Down arrow key)
KEY_UP                       // 265 (Up arrow key)
KEY_LEFT_SHIFT               // 340 (Left Shift key)
KEY_RIGHT_SHIFT              // 344 (Right Shift key)
KEY_LEFT_CONTROL             // 341 (Left Control key)
KEY_RIGHT_CONTROL            // 345 (Right Control key)
KEY_LEFT_ALT                 // 342 (Left Alt/Option key)
KEY_RIGHT_ALT                // 346 (Right Alt/Option key)
KEY_LEFT_SUPER               // 343 (Left Super/Command key)
KEY_RIGHT_SUPER              // 347 (Right Super/Command key)
MOUSE_BUTTON_LEFT            // 0 (Left mouse button)
MOUSE_BUTTON_RIGHT           // 1 (Right mouse button)
MOUSE_BUTTON_MIDDLE          // 2 (Middle mouse button)
```

## Variables

```cpp
float myVar = 0.0;       // Global variable (persists across frames)
```

## Example

```cpp
float angle = 0.0;

void setup() {
    logNotice("Starting!");
}

void update() {
    angle = angle + getDeltaTime();
}

void draw() {
    clear(0.1);

    pushMatrix();
    translate(getWindowWidth() / 2.0, getWindowHeight() / 2.0);
    rotate(angle);

    setColor(1.0, 0.5, 0.2);
    drawRect(-50.0, -50.0, 100.0, 100.0);

    popMatrix();
}

void keyPressed(int key) {
    logNotice("Key: " + toString(key));
}
```
