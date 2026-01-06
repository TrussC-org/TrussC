# TrussC API Reference
            
            Complete API reference. This document is auto-generated from `api-definition.yaml`.
            
            For the latest interactive reference, visit [trussc.org/reference](https://trussc.org/reference/).
            
            ## Lifecycle

```javascript
setup()                                  // Called once at start
update()                                 // Called every frame before draw
draw()                                   // Called every frame after update
```

## Events

```javascript
mousePressed(float x, float y, int button) // Mouse button pressed
mouseReleased(float x, float y, int button) // Mouse button released
mouseMoved(float x, float y)             // Mouse moved
mouseDragged(float x, float y, int button) // Mouse dragged
keyPressed(int key)                      // Key pressed
keyReleased(int key)                     // Key released
windowResized(int width, int height)     // Window resized
```

## Graphics - Color

```javascript
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

```javascript
void drawRect(float x, float y, float w, float h) // Draw rectangle
void drawCircle(float x, float y, float radius) // Draw circle
void drawEllipse(float x, float y, float w, float h) // Draw ellipse
void drawLine(float x1, float y1, float x2, float y2) // Draw line
void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) // Draw triangle
void beginShape()                        // Begin drawing a shape
void vertex(float x, float y)            // Add a vertex
void vertex(float x, float y, float z)   // Add a vertex
void vertex(const Vec2& v)               // Add a vertex
void vertex(const Vec3& v)               // Add a vertex
void endShape(bool close = false)        // End drawing a shape
void drawBitmapString(const string& text, float x, float y) // Draw text
```

## Graphics - Style

```javascript
void fill()                              // Enable fill
void noFill()                            // Disable fill
void stroke()                            // Enable stroke
void noStroke()                          // Disable stroke
void setStrokeWeight(float weight)       // Set stroke width
```

## Transform

```javascript
void translate(float x, float y)         // Move origin
void translate(float x, float y, float z) // Move origin
void rotate(float radians)               // Rotate by radians
void rotateDeg(float degrees)            // Rotate by degrees
void scale(float s)                      // Scale
void scale(float sx, float sy)           // Scale
void pushMatrix()                        // Save transform state
void popMatrix()                         // Restore transform state
```

## Window & Input

```javascript
int getWindowWidth()                     // Get canvas width
int getWindowHeight()                    // Get canvas height
float getMouseX()                        // Get mouse X position
float getMouseY()                        // Get mouse Y position
bool isMousePressed()                    // Is mouse button pressed
```

## Time - Frame

```javascript
double getDeltaTime()                    // Seconds since last frame
double getFrameRate()                    // Current FPS
uint64_t getFrameCount()                 // Total frames rendered
```

## Time - Elapsed

```javascript
float getElapsedTimef()                  // Elapsed seconds (float)
uint64_t getElapsedTimeMillis()          // Elapsed milliseconds (int64)
uint64_t getElapsedTimeMicros()          // Elapsed microseconds (int64)
void resetElapsedTimeCounter()           // Reset elapsed time
```

## Time - System

```javascript
uint64_t getSystemTimeMillis()           // Unix time in milliseconds
uint64_t getSystemTimeMicros()           // Unix time in microseconds
string getTimestampString()              // Formatted timestamp
string getTimestampString(const string& format) // Formatted timestamp
```

## Time - Current

```javascript
int getSeconds()                         // Current seconds (0-59)
int getMinutes()                         // Current minutes (0-59)
int getHours()                           // Current hours (0-23)
int getYear()                            // Current year
int getMonth()                           // Current month (1-12)
int getDay()                             // Current day (1-31)
int getWeekday()                         // Weekday (0=Sun, 6=Sat)
```

## Math - Random & Noise

```javascript
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

```javascript
float lerp(float a, float b, float t)    // Linear interpolation
float clamp(float v, float min, float max) // Clamp value to range
float map(float v, float inMin, float inMax, float outMin, float outMax) // Map value between ranges
```

## Math - Trigonometry

```javascript
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

```javascript
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

```javascript
float dist(float x1, float y1, float x2, float y2) // Distance between points
float distSquared(float x1, float y1, float x2, float y2) // Squared distance
```

## Window & System

```javascript
toggleFullscreen()                       // Toggle fullscreen mode
setClipboardString(const string& text)   // Copy text to clipboard
getClipboardString()                     // Get text from clipboard
```

## Utility

```javascript
logNotice(const string& message)         // Print to console
to_string(value)                         // Convert to string
beep()                                   // Play a beep sound
beep(float frequency)                    // Play a beep sound
```

## Sound

```javascript
Sound()                                  // Create a sound player
play()                                   // Play sound
stop()                                   // Stop sound
setVolume(float vol)                     // Set volume (0.0-1.0)
setLoop(bool loop)                       // Enable/disable looping
```

## Animation

```javascript
Tween()                                  // Create a tween
setDuration(float seconds)               // Set animation duration
start()                                  // Start animation
update(float dt)                         // Update animation
getValue()                               // Get current tween value
```

## Types - Vec2

```javascript
Vec2()                                   // Create 2D vector
Vec2(float x, float y)                   // Create 2D vector
Vec2(float v)                            // Create 2D vector
Vec2& set(float x, float y)              // Set vector components
Vec2& set(Vec2 v)                        // Set vector components
Vec2 Vec2_fromAngle(float radians)       // Create Vec2 from angle
Vec2 Vec2_fromAngle(float radians, float length) // Create Vec2 from angle
```

## Types - Vec3

```javascript
Vec3()                                   // Create 3D vector
Vec3(float x, float y, float z)          // Create 3D vector
Vec3(float v)                            // Create 3D vector
Vec3& set(float x, float y, float z)     // Set vector components
Vec3& set(Vec3 v)                        // Set vector components
```

## Types - Color

```javascript
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

```javascript
Rect()                                   // Create a rectangle
Rect(float x, float y, float w, float h) // Create a rectangle
Rect& set(float x, float y, float w, float h) // Set rectangle properties
Rect& set(Vec2 pos, float w, float h)    // Set rectangle properties
contains(float x, float y)               // Check if point is inside
intersects(Rect other)                   // Check intersection
```

## Scene Graph

```javascript
Node()                                   // Create a base scene node
addChild(shared_ptr<Node> child)         // Add a child node
setPosition(float x, float y)            // Set position
setPosition(Vec3 pos)                    // Set position
RectNode()                               // Create a 2D rectangle node
setSize(float w, float h)                // Set size
```

## 3D Camera

```javascript
EasyCam()                                // Create an easy-to-use 3D camera
begin()                                  // Apply camera transform
end()                                    // Restore previous transform
```

## Math - 3D

```javascript
Mat4()                                   // Create a 4x4 matrix
Quaternion()                             // Create a quaternion
```

## Graphics - Advanced

```javascript
drawMesh(Mesh mesh)                      // Draw a mesh
drawPolyline(Polyline polyline)          // Draw a polyline
createBox(float size)                    // Create a box mesh
createBox(float w, float h, float d)     // Create a box mesh
createSphere(float radius, int res = 20) // Create a sphere mesh
```

## Types - Mesh

```javascript
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

```javascript
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

```javascript
StrokeMesh()                             // Create a new StrokeMesh
void setWidth(float width)               // Set stroke width
void setColor(Color color)               // Set stroke color
void setCapType(int type)                // Set cap type (CAP_BUTT, CAP_ROUND, CAP_SQUARE)
void setJoinType(int type)               // Set join type (JOIN_MITER, JOIN_ROUND, JOIN_BEVEL)
void addVertex(float x, float y)         // Add a vertex
void update()                            // Update the internal mesh
```

## Constants

```javascript
TAU                          // 6.283... (Full circle (2*PI))
HALF_TAU                     // 3.141... (Half circle (PI))
QUARTER_TAU                  // 1.570... (Quarter circle (PI/2))
PI                           // 3.141... (Pi (use TAU instead))
```

## Variables

```javascript
global myVar = 0         // Global variable (persists across frames)
var localVar = 0         // Local variable (scope-limited)
```

## Example

```javascript
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
