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
void keyPressed(int key)                 // Key pressed
void keyReleased(int key)                // Key released
void windowResized(int width, int height) // Window resized
```

## Graphics - Color

```cpp
void clear(float gray)                   // Clear screen
void clear(float r, float g, float b)    // Clear screen
void setColor(float gray)                // Set drawing color (0.0-1.0)
void setColor(float r, float g, float b) // Set drawing color (0.0-1.0)
void setColor(float r, float g, float b, float a) // Set drawing color (0.0-1.0)
void setColorHSB(float h, float s, float b) // Set color from HSB (H: 0-TAU)
void setColorOKLCH(float L, float C, float H) // Set color from OKLCH
void setColorOKLab(float L, float a, float b) // Set color from OKLab
```

## Graphics - Shapes

```cpp
void drawRect(float x, float y, float w, float h) // Draw rectangle
void drawRect(Vec3 pos, float w, float h) // Draw rectangle
void drawRect(Vec3 pos, Vec2 size)       // Draw rectangle
void drawCircle(float x, float y, float radius) // Draw circle
void drawCircle(Vec3 center, float radius) // Draw circle
void drawEllipse(float x, float y, float w, float h) // Draw ellipse
void drawEllipse(Vec3 center, float rx, float ry) // Draw ellipse
void drawEllipse(Vec3 center, Vec2 radii) // Draw ellipse
void drawLine(float x1, float y1, float x2, float y2) // Draw line
void drawLine(Vec3 p1, Vec3 p2)          // Draw line
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
void drawStroke(float x1, float y1, float x2, float y2) // Draw a single stroke segment (thick line with cap/join)
void drawStroke(const Vec2& p1, const Vec2& p2) // Draw a single stroke segment (thick line with cap/join)
void drawBitmapString(const string& text, float x, float y) // Draw text
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
void scale(float s)                      // Scale
void scale(float sx, float sy)           // Scale
void pushMatrix()                        // Save transform state
void popMatrix()                         // Restore transform state
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
```

## Time - Frame

```cpp
double getDeltaTime()                    // Seconds since last frame
double getFrameRate()                    // Current FPS
uint64_t getFrameCount()                 // Total frames rendered
```

## Time - Elapsed

```cpp
float getElapsedTimef()                  // Elapsed seconds (float)
uint64_t getElapsedTimeMillis()          // Elapsed milliseconds (int64)
uint64_t getElapsedTimeMicros()          // Elapsed microseconds (int64)
void resetElapsedTimeCounter()           // Reset elapsed time
```

## Time - System

```cpp
uint64_t getSystemTimeMillis()           // Unix time in milliseconds
uint64_t getSystemTimeMicros()           // Unix time in microseconds
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
float lerp(float a, float b, float t)    // Linear interpolation
float clamp(float v, float min, float max) // Clamp value to range
float map(float v, float inMin, float inMax, float outMin, float outMax) // Map value between ranges
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
```

## Math - Geometry

```cpp
float dist(float x1, float y1, float x2, float y2) // Distance between points
float distSquared(float x1, float y1, float x2, float y2) // Squared distance
```

## Window & System

```cpp
void toggleFullscreen()                  // Toggle fullscreen mode
void setClipboardString(const string& text) // Copy text to clipboard
string getClipboardString()              // Get text from clipboard
```

## Utility

```cpp
void logNotice(const string& message)    // Print to console
string to_string(value)                  // Convert to string
void beep()                              // Play a beep sound
void beep(float frequency)               // Play a beep sound
```

## Sound

```cpp
Sound()                                  // Create a sound player
void play()                              // Play sound
void stop()                              // Stop sound
void setVolume(float vol)                // Set volume (0.0-1.0)
void setLoop(bool loop)                  // Enable/disable looping
```

## Animation

```cpp
Tween()                                  // Create a tween
void setDuration(float seconds)          // Set animation duration
void start()                             // Start animation
void update(float dt)                    // Update animation
float getValue()                         // Get current tween value
```

## Types - Vec2

```cpp
Vec2()                                   // Create 2D vector
Vec2(float x, float y)                   // Create 2D vector
Vec2(float v)                            // Create 2D vector
Vec2& set(float x, float y)              // Set vector components
Vec2& set(Vec2 v)                        // Set vector components
Vec2 Vec2_fromAngle(float radians)       // Create Vec2 from angle
Vec2 Vec2_fromAngle(float radians, float length) // Create Vec2 from angle
```

## Types - Vec3

```cpp
Vec3()                                   // Create 3D vector
Vec3(float x, float y, float z)          // Create 3D vector
Vec3(float v)                            // Create 3D vector
Vec3& set(float x, float y, float z)     // Set vector components
Vec3& set(Vec3 v)                        // Set vector components
```

## Types - Color

```cpp
Color()                                  // Create color (0.0-1.0)
Color(float r, float g, float b)         // Create color (0.0-1.0)
Color(float r, float g, float b, float a) // Create color (0.0-1.0)
Color& set(float r, float g, float b)    // Set color components
Color& set(float r, float g, float b, float a) // Set color components
Color& set(float gray)                   // Set color components
Color& set(Color c)                      // Set color components
Color Color_fromHSB(float h, float s, float b) // Create Color from HSB
Color Color_fromHSB(float h, float s, float b, float a) // Create Color from HSB
Color Color_fromOKLCH(float L, float C, float H) // Create Color from OKLCH
Color Color_fromOKLCH(float L, float C, float H, float a) // Create Color from OKLCH
Color Color_fromOKLab(float L, float a, float b) // Create Color from OKLab
Color Color_fromOKLab(float L, float a, float b, float alpha) // Create Color from OKLab
```

## Types - Rect

```cpp
Rect()                                   // Create a rectangle
Rect(float x, float y, float w, float h) // Create a rectangle
Rect& set(float x, float y, float w, float h) // Set rectangle properties
Rect& set(Vec2 pos, float w, float h)    // Set rectangle properties
bool contains(float x, float y)          // Check if point is inside
bool intersects(Rect other)              // Check intersection
```

## Scene Graph

```cpp
Node()                                   // Create a base scene node
void addChild(shared_ptr<Node> child)    // Add a child node
void setPosition(float x, float y)       // Set position
void setPosition(Vec3 pos)               // Set position
RectNode()                               // Create a 2D rectangle node
void setSize(float w, float h)           // Set size
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
EasyCam()                                // Create an easy-to-use 3D camera
void begin()                             // Apply camera transform
void end()                               // Restore previous transform
```

## Math - 3D

```cpp
Mat4()                                   // Create a 4x4 matrix
Quaternion()                             // Create a quaternion
```

## Graphics - Advanced

```cpp
void drawMesh(Mesh mesh)                 // Draw a mesh
void drawPolyline(Polyline polyline)     // Draw a polyline
Mesh createBox(float size)               // Create a box mesh
Mesh createBox(float w, float h, float d) // Create a box mesh
Mesh createSphere(float radius, int res = 20) // Create a sphere mesh
```

## Types - Mesh

```cpp
Mesh()                                   // Create a new Mesh
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

## Types - Polyline

```cpp
Polyline()                               // Create a new Polyline (Path)
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
StrokeMesh()                             // Create a new StrokeMesh
void setWidth(float width)               // Set stroke width
void setColor(Color color)               // Set stroke color
void setCapType(int type)                // Set cap type (CAP_BUTT, CAP_ROUND, CAP_SQUARE)
void setJoinType(int type)               // Set join type (JOIN_MITER, JOIN_ROUND, JOIN_BEVEL)
void addVertex(float x, float y)         // Add a vertex
void update()                            // Update the internal mesh
```

## Constants

```cpp
TAU                          // 6.283... (Full circle (2*PI))
HALF_TAU                     // 3.141... (Half circle (PI))
QUARTER_TAU                  // 1.570... (Quarter circle (PI/2))
PI                           // 3.141... (Pi (use TAU instead))
```

## Variables

```cpp
global myVar = 0         // Global variable (persists across frames)
var localVar = 0         // Local variable (scope-limited)
```

## Example

```cpp
global angle = 0.0

def setup() {
    logNotice("Starting!")
}

def update() {
    angle = angle + getDeltaTime()
}

def draw() {
    clear(0.1)

    pushMatrix()
    translate(getWindowWidth() / 2.0, getWindowHeight() / 2.0)
    rotate(angle)

    setColor(1.0, 0.5, 0.2)
    drawRect(-50.0, -50.0, 100.0, 100.0)

    popMatrix()
}

def keyPressed(key) {
    logNotice("Key: " + to_string(key))
}
```
