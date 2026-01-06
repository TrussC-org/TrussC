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
clear(float gray)                        // Clear screen
clear(float r, float g, float b)         // Clear screen
setColor(float gray)                     // Set drawing color (0.0-1.0)
setColor(float r, float g, float b)      // Set drawing color (0.0-1.0)
setColor(float r, float g, float b, float a) // Set drawing color (0.0-1.0)
setColorHSB(float h, float s, float b)   // Set color from HSB (H: 0-TAU)
setColorOKLCH(float L, float C, float H) // Set color from OKLCH
setColorOKLab(float L, float a, float b) // Set color from OKLab
```

## Graphics - Shapes

```javascript
drawRect(float x, float y, float w, float h) // Draw rectangle
drawCircle(float x, float y, float radius) // Draw circle
drawEllipse(float x, float y, float w, float h) // Draw ellipse
drawLine(float x1, float y1, float x2, float y2) // Draw line
drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) // Draw triangle
beginShape()                             // Begin drawing a shape
vertex(float x, float y)                 // Add a vertex
vertex(float x, float y, float z)        // Add a vertex
vertex(const Vec2& v)                    // Add a vertex
vertex(const Vec3& v)                    // Add a vertex
endShape(bool close = false)             // End drawing a shape
drawBitmapString(const string& text, float x, float y) // Draw text
```

## Graphics - Style

```javascript
fill()                                   // Enable fill
noFill()                                 // Disable fill
stroke()                                 // Enable stroke
noStroke()                               // Disable stroke
setStrokeWeight(float weight)            // Set stroke width
```

## Transform

```javascript
translate(float x, float y)              // Move origin
translate(float x, float y, float z)     // Move origin
rotate(float radians)                    // Rotate by radians
rotateDeg(float degrees)                 // Rotate by degrees
scale(float s)                           // Scale
scale(float sx, float sy)                // Scale
pushMatrix()                             // Save transform state
popMatrix()                              // Restore transform state
```

## Window & Input

```javascript
getWindowWidth()                         // Get canvas width
getWindowHeight()                        // Get canvas height
getMouseX()                              // Get mouse X position
getMouseY()                              // Get mouse Y position
isMousePressed()                         // Is mouse button pressed
```

## Time - Frame

```javascript
getDeltaTime()                           // Seconds since last frame
getFrameRate()                           // Current FPS
getFrameCount()                          // Total frames rendered
```

## Time - Elapsed

```javascript
getElapsedTimef()                        // Elapsed seconds (float)
getElapsedTimeMillis()                   // Elapsed milliseconds (int64)
getElapsedTimeMicros()                   // Elapsed microseconds (int64)
resetElapsedTimeCounter()                // Reset elapsed time
```

## Time - System

```javascript
getSystemTimeMillis()                    // Unix time in milliseconds
getSystemTimeMicros()                    // Unix time in microseconds
getTimestampString()                     // Formatted timestamp
getTimestampString(const string& format) // Formatted timestamp
```

## Time - Current

```javascript
getSeconds()                             // Current seconds (0-59)
getMinutes()                             // Current minutes (0-59)
getHours()                               // Current hours (0-23)
getYear()                                // Current year
getMonth()                               // Current month (1-12)
getDay()                                 // Current day (1-31)
getWeekday()                             // Weekday (0=Sun, 6=Sat)
```

## Math - Random & Noise

```javascript
random()                                 // Random number
random(float max)                        // Random number
random(float min, float max)             // Random number
noise(float x)                           // Perlin noise
noise(float x, float y)                  // Perlin noise
noise(float x, float y, float z)         // Perlin noise
signedNoise(float x)                     // Perlin noise (-1.0 to 1.0)
signedNoise(float x, float y)            // Perlin noise (-1.0 to 1.0)
signedNoise(float x, float y, float z)   // Perlin noise (-1.0 to 1.0)
signedNoise(float x, float y, float z, float w) // Perlin noise (-1.0 to 1.0)
fbm(float x, float y, int octaves = 4, float lacunarity = 2.0, float gain = 0.5) // Fractal Brownian Motion noise
fbm(float x, float y, float z, int octaves = 4, float lacunarity = 2.0, float gain = 0.5) // Fractal Brownian Motion noise
```

## Math - Interpolation

```javascript
lerp(float a, float b, float t)          // Linear interpolation
clamp(float v, float min, float max)     // Clamp value to range
map(float v, float inMin, float inMax, float outMin, float outMax) // Map value between ranges
```

## Math - Trigonometry

```javascript
sin(float x)                             // Sine
cos(float x)                             // Cosine
tan(float x)                             // Tangent
asin(float x)                            // Arc sine
acos(float x)                            // Arc cosine
atan(float x)                            // Arc tangent
atan2(float y, float x)                  // Arc tangent of y/x
deg2rad(float degrees)                   // Degrees to radians
rad2deg(float radians)                   // Radians to degrees
```

## Math - General

```javascript
abs(float x)                             // Absolute value
sqrt(float x)                            // Square root
sq(float x)                              // Square (x*x)
pow(float x, float y)                    // Power (x^y)
log(float x)                             // Natural logarithm
exp(float x)                             // Exponential (e^x)
min(float a, float b)                    // Minimum
max(float a, float b)                    // Maximum
floor(float x)                           // Round down
ceil(float x)                            // Round up
round(float x)                           // Round to nearest
fmod(float x, float y)                   // Floating-point modulo
sign(float x)                            // Sign (-1, 0, 1)
fract(float x)                           // Fractional part
```

## Math - Geometry

```javascript
dist(float x1, float y1, float x2, float y2) // Distance between points
distSquared(float x1, float y1, float x2, float y2) // Squared distance
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
setMode(int mode)                        // Set primitive mode (MESH_TRIANGLES, etc.)
addVertex(float x, float y, float z)     // Add a vertex
addVertex(Vec3 v)                        // Add a vertex
addColor(float r, float g, float b, float a) // Add a color for the vertex
addColor(Color c)                        // Add a color for the vertex
addTexCoord(float u, float v)            // Add a texture coordinate
addNormal(float x, float y, float z)     // Add a normal vector
addIndex(int index)                      // Add an index
addTriangle(int i1, int i2, int i3)      // Add a triangle (3 indices)
clear()                                  // Clear all data
draw()                                   // Draw the mesh
```

## Types - Polyline

```javascript
Polyline()                               // Create a new Polyline (Path)
addVertex(float x, float y)              // Add a vertex
lineTo(float x, float y)                 // Add a line segment to point
bezierTo(float cx1, float cy1, float cx2, float cy2, float x, float y) // Add a cubic bezier curve
quadBezierTo(float cx, float cy, float x, float y) // Add a quadratic bezier curve
curveTo(float x, float y)                // Add a Catmull-Rom curve segment
arc(float x, float y, float rX, float rY, float angleBegin, float angleEnd) // Add an arc
close()                                  // Close the shape
```

## Types - StrokeMesh

```javascript
StrokeMesh()                             // Create a new StrokeMesh
setWidth(float width)                    // Set stroke width
setColor(Color color)                    // Set stroke color
setCapType(int type)                     // Set cap type (CAP_BUTT, CAP_ROUND, CAP_SQUARE)
setJoinType(int type)                    // Set join type (JOIN_MITER, JOIN_ROUND, JOIN_BEVEL)
addVertex(float x, float y)              // Add a vertex
update()                                 // Update the internal mesh
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
