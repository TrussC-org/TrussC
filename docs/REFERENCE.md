# TrussC API Reference

Complete API reference. This document is auto-generated from `api-definition.yaml`.

For the latest interactive reference, visit [trussc.org/reference](https://trussc.org/reference/).

## Lifecycle

```javascript
setup()                          // Called once at start
update()                         // Called every frame before draw
draw()                           // Called every frame after update
```

## Events

```javascript
mousePressed(x, y, button)       // Mouse button pressed
mouseReleased(x, y, button)      // Mouse button released
mouseMoved(x, y)                 // Mouse moved
mouseDragged(x, y, button)       // Mouse dragged
keyPressed(key)                  // Key pressed
keyReleased(key)                 // Key released
windowResized(width, height)     // Window resized
```

## Graphics - Color

```javascript
clear(gray)                      // Clear screen
clear(r, g, b)                   // Clear screen
setColor(gray)                   // Set drawing color (0.0-1.0)
setColor(r, g, b)                // Set drawing color (0.0-1.0)
setColor(r, g, b, a)             // Set drawing color (0.0-1.0)
setColorHSB(h, s, b)             // Set color from HSB (H: 0-TAU)
setColorOKLCH(L, C, H)           // Set color from OKLCH
setColorOKLab(L, a, b)           // Set color from OKLab
```

## Graphics - Shapes

```javascript
drawRect(x, y, w, h)             // Draw rectangle
drawCircle(x, y, radius)         // Draw circle
drawEllipse(x, y, w, h)          // Draw ellipse
drawLine(x1, y1, x2, y2)         // Draw line
drawTriangle(x1, y1, x2, y2, x3, y3) // Draw triangle
beginShape()                     // Begin drawing a shape
vertex(x, y)                     // Add a vertex
vertex(x, y, z)                  // Add a vertex
vertex(v)                        // Add a vertex
endShape(close)                  // End drawing a shape
drawBitmapString(text, x, y)     // Draw text
```

## Graphics - Style

```javascript
fill()                           // Enable fill
noFill()                         // Disable fill
stroke()                         // Enable stroke
noStroke()                       // Disable stroke
setStrokeWeight(weight)          // Set stroke width
```

## Transform

```javascript
translate(x, y)                  // Move origin
translate(x, y, z)               // Move origin
rotate(radians)                  // Rotate by radians
rotateDeg(degrees)               // Rotate by degrees
scale(s)                         // Scale
scale(sx, sy)                    // Scale
pushMatrix()                     // Save transform state
popMatrix()                      // Restore transform state
```

## Window & Input

```javascript
getWindowWidth()                 // Get canvas width
getWindowHeight()                // Get canvas height
getMouseX()                      // Get mouse X position
getMouseY()                      // Get mouse Y position
isMousePressed()                 // Is mouse button pressed
```

## Time - Frame

```javascript
getDeltaTime()                   // Seconds since last frame
getFrameRate()                   // Current FPS
getFrameCount()                  // Total frames rendered
```

## Time - Elapsed

```javascript
getElapsedTimef()                // Elapsed seconds (float)
getElapsedTimeMillis()           // Elapsed milliseconds (int64)
getElapsedTimeMicros()           // Elapsed microseconds (int64)
resetElapsedTimeCounter()        // Reset elapsed time
```

## Time - System

```javascript
getSystemTimeMillis()            // Unix time in milliseconds
getSystemTimeMicros()            // Unix time in microseconds
getTimestampString()             // Formatted timestamp
getTimestampString(format)       // Formatted timestamp
```

## Time - Current

```javascript
getSeconds()                     // Current seconds (0-59)
getMinutes()                     // Current minutes (0-59)
getHours()                       // Current hours (0-23)
getYear()                        // Current year
getMonth()                       // Current month (1-12)
getDay()                         // Current day (1-31)
getWeekday()                     // Weekday (0=Sun, 6=Sat)
```

## Math - Random & Noise

```javascript
random()                         // Random number
random(max)                      // Random number
random(min, max)                 // Random number
noise(x)                         // Perlin noise
noise(x, y)                      // Perlin noise
noise(x, y, z)                   // Perlin noise
signedNoise(x)                   // Perlin noise (-1.0 to 1.0)
signedNoise(x, y)                // Perlin noise (-1.0 to 1.0)
signedNoise(x, y, z)             // Perlin noise (-1.0 to 1.0)
signedNoise(x, y, z, w)          // Perlin noise (-1.0 to 1.0)
fbm(x, y, octaves, lacunarity, gain) // Fractal Brownian Motion noise
fbm(x, y, z, octaves, lacunarity, gain) // Fractal Brownian Motion noise
```

## Math - Interpolation

```javascript
lerp(a, b, t)                    // Linear interpolation
clamp(v, min, max)               // Clamp value to range
map(v, inMin, inMax, outMin, outMax) // Map value between ranges
```

## Math - Trigonometry

```javascript
sin(x)                           // Sine
cos(x)                           // Cosine
tan(x)                           // Tangent
asin(x)                          // Arc sine
acos(x)                          // Arc cosine
atan(x)                          // Arc tangent
atan2(y, x)                      // Arc tangent of y/x
deg2rad(degrees)                 // Degrees to radians
rad2deg(radians)                 // Radians to degrees
```

## Math - General

```javascript
abs(x)                           // Absolute value
sqrt(x)                          // Square root
sq(x)                            // Square (x*x)
pow(x, y)                        // Power (x^y)
log(x)                           // Natural logarithm
exp(x)                           // Exponential (e^x)
min(a, b)                        // Minimum
max(a, b)                        // Maximum
floor(x)                         // Round down
ceil(x)                          // Round up
round(x)                         // Round to nearest
fmod(x, y)                       // Floating-point modulo
sign(x)                          // Sign (-1, 0, 1)
fract(x)                         // Fractional part
```

## Math - Geometry

```javascript
dist(x1, y1, x2, y2)             // Distance between points
distSquared(x1, y1, x2, y2)      // Squared distance
```

## Window & System

```javascript
toggleFullscreen()               // Toggle fullscreen mode
setClipboardString(text)         // Copy text to clipboard
getClipboardString()             // Get text from clipboard
```

## Utility

```javascript
logNotice(message)               // Print to console
to_string(value)                 // Convert to string
beep()                           // Play a beep sound
beep(frequency)                  // Play a beep sound
```

## Sound

```javascript
Sound()                          // Create a sound player
play()                           // Play sound
stop()                           // Stop sound
setVolume(vol)                   // Set volume (0.0-1.0)
setLoop(loop)                    // Enable/disable looping
```

## Animation

```javascript
Tween()                          // Create a tween
setDuration(seconds)             // Set animation duration
start()                          // Start animation
update(dt)                       // Update animation
getValue()                       // Get current tween value
```

## Types - Vec2

```javascript
Vec2()                           // Create 2D vector
Vec2(x, y)                       // Create 2D vector
Vec2(v)                          // Create 2D vector
set(x, y)                        // Set vector components
set(v)                           // Set vector components
Vec2_fromAngle(radians)          // Create Vec2 from angle
Vec2_fromAngle(radians, length)  // Create Vec2 from angle
```

## Types - Vec3

```javascript
Vec3()                           // Create 3D vector
Vec3(x, y, z)                    // Create 3D vector
Vec3(v)                          // Create 3D vector
set(x, y, z)                     // Set vector components
set(v)                           // Set vector components
```

## Types - Color

```javascript
Color()                          // Create color (0.0-1.0)
Color(r, g, b)                   // Create color (0.0-1.0)
Color(r, g, b, a)                // Create color (0.0-1.0)
set(r, g, b)                     // Set color components
set(r, g, b, a)                  // Set color components
set(gray)                        // Set color components
set(c)                           // Set color components
Color_fromHSB(h, s, b)           // Create Color from HSB
Color_fromHSB(h, s, b, a)        // Create Color from HSB
Color_fromOKLCH(L, C, H)         // Create Color from OKLCH
Color_fromOKLCH(L, C, H, a)      // Create Color from OKLCH
Color_fromOKLab(L, a, b)         // Create Color from OKLab
Color_fromOKLab(L, a, b, alpha)  // Create Color from OKLab
```

## Types - Rect

```javascript
Rect()                           // Create a rectangle
Rect(x, y, w, h)                 // Create a rectangle
set(x, y, w, h)                  // Set rectangle properties
set(pos, w, h)                   // Set rectangle properties
contains(x, y)                   // Check if point is inside
intersects(other)                // Check intersection
```

## Scene Graph

```javascript
Node()                           // Create a base scene node
addChild(child)                  // Add a child node
setPosition(x, y)                // Set position
setPosition(pos)                 // Set position
RectNode()                       // Create a 2D rectangle node
setSize(w, h)                    // Set size
```

## 3D Camera

```javascript
EasyCam()                        // Create an easy-to-use 3D camera
begin()                          // Apply camera transform
end()                            // Restore previous transform
```

## Math - 3D

```javascript
Mat4()                           // Create a 4x4 matrix
Quaternion()                     // Create a quaternion
```

## Graphics - Advanced

```javascript
drawMesh(mesh)                   // Draw a mesh
drawPolyline(polyline)           // Draw a polyline
createBox(size)                  // Create a box mesh
createBox(w, h, d)               // Create a box mesh
createSphere(radius, res)        // Create a sphere mesh
```

## Types - Mesh

```javascript
Mesh()                           // Create a new Mesh
setMode(mode)                    // Set primitive mode (MESH_TRIANGLES, etc.)
addVertex(x, y, z)               // Add a vertex
addVertex(v)                     // Add a vertex
addColor(r, g, b, a)             // Add a color for the vertex
addColor(c)                      // Add a color for the vertex
addTexCoord(u, v)                // Add a texture coordinate
addNormal(x, y, z)               // Add a normal vector
addIndex(index)                  // Add an index
addTriangle(i1, i2, i3)          // Add a triangle (3 indices)
clear()                          // Clear all data
draw()                           // Draw the mesh
```

## Types - Polyline

```javascript
Polyline()                       // Create a new Polyline (Path)
addVertex(x, y)                  // Add a vertex
lineTo(x, y)                     // Add a line segment to point
bezierTo(cx1, cy1, cx2, cy2, x, y) // Add a cubic bezier curve
quadBezierTo(cx, cy, x, y)       // Add a quadratic bezier curve
curveTo(x, y)                    // Add a Catmull-Rom curve segment
arc(x, y, rX, rY, start, end)    // Add an arc
close()                          // Close the shape
```

## Types - StrokeMesh

```javascript
StrokeMesh()                     // Create a new StrokeMesh
setWidth(width)                  // Set stroke width
setColor(color)                  // Set stroke color
setCapType(type)                 // Set cap type (CAP_BUTT, CAP_ROUND, CAP_SQUARE)
setJoinType(type)                // Set join type (JOIN_MITER, JOIN_ROUND, JOIN_BEVEL)
addVertex(x, y)                  // Add a vertex
update()                         // Update the internal mesh
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
