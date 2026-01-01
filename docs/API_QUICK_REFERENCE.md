# TrussC Quick Reference for AI

Compact API reference for RAG/prompts. Method signatures omitted for brevity.

## Classes

Font, Vec2, Vec3, Vec4, Color, ColorHSB, ColorLinear, ColorOKLab, ColorOKLCH,
Rect, Mat3, Mat4, Quaternion, Mesh, Polyline, Texture, Fbo, Shader, Pixels,
Sound, Tween, EasyCam, Node, RectNode, App, WindowSettings

## Drawing

drawRect, drawCircle, drawEllipse, drawLine, drawTriangle, drawPoint,
drawBitmapString, drawTexture, drawMesh, drawPolyline

## Mesh Primitives

createBox, createSphere, createPlane, createCylinder, createCone, createIcosphere

## Mesh Methods

Mesh::addVertex, addColor, addTexCoord, addNormal, addIndex, addTriangle,
draw, clear, getVertices, getColors, getTexCoords, getNormals, getIndices,
setMode, getMode

## Style

setColor, setColorHSB, setColorOKLab, setColorOKLCH, getColor,
fill, noFill, stroke, noStroke, setStrokeWeight, getStrokeWeight,
pushStyle, popStyle, setBlendMode, getBlendMode, resetBlendMode,
setCircleResolution, getCircleResolution

## Transform

translate, rotate, rotateX, rotateY, rotateZ, rotateDeg,
scale, pushMatrix, popMatrix, resetMatrix, getCurrentMatrix, setMatrix

## Window

getWindowWidth, getWindowHeight, setWindowTitle, setWindowSize,
setFullscreen, toggleFullscreen, isFullscreen,
getFramebufferWidth, getFramebufferHeight, getDpiScale, getAspectRatio

## Frame

clear, beginFrame, present, setup, cleanup,
suspendSwapchainPass, resumeSwapchainPass, isInSwapchainPass

## Time

getDeltaTime, getElapsedTime, getElapsedTimeMillis,
getFps, setFps, setIndependentFps, getFrameCount, getUpdateCount, getDrawCount,
redraw, exitApp

## Input - Mouse

getMouseX, getMouseY, getGlobalMouseX, getGlobalMouseY,
getGlobalPMouseX, getGlobalPMouseY, isMousePressed, getMouseButton

## Input - Keyboard Constants

KEY_SPACE, KEY_ENTER, KEY_ESCAPE, KEY_TAB, KEY_BACKSPACE, KEY_DELETE,
KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT, KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL,
KEY_LEFT_ALT, KEY_RIGHT_ALT, KEY_LEFT_SUPER, KEY_RIGHT_SUPER,
KEY_F1 ~ KEY_F12

## Math Functions

lerp, map, clamp, random, randomInt, randomSeed,
deg2rad, rad2deg, sign, fract, abs, min, max

## Math Constants

TAU, HALF_TAU, QUARTER_TAU, PI (deprecated, use HALF_TAU)

## Noise

noise, snoise, noiseDetail, noiseSeed

## Vec2

Vec2::length, lengthSquared, normalized, normalize, dot, cross, distance,
lerp, rotated, rotate, perpendicular, reflected, angle, limit

## Vec3

Vec3::length, lengthSquared, normalized, normalize, dot, cross, distance,
lerp, reflected, limit

## Vec4

Vec4::length, lengthSquared, normalized, normalize, dot, lerp

## Color

Color, colorFromHSB, Color::fromBytes, Color::fromHex,
toLinear, toHSB, toOKLab, toOKLCH, toHex, clamped,
lerp, lerpRGB, lerpLinear, lerpHSB, lerpOKLab, lerpOKLCH

## Predefined Colors

colors::white, black, red, green, blue, yellow, cyan, magenta, orange,
gray, darkGray, lightGray, transparent, cornflowerBlue, ...

## Rect

Rect, Rect::contains, intersects, getRight, getBottom, getCenterX, getCenterY,
getCenter, getTopLeft, getBottomRight, expanded, translated

## Mat4

Mat4::identity, translate, rotateX, rotateY, rotateZ, rotate, scale,
lookAt, perspective, ortho, inverted, transposed, determinant

## Quaternion

Quaternion::fromAxisAngle, fromEuler, toEuler, toMatrix,
normalized, conjugate, inverse, slerp, rotate

## Texture

Texture::load, loadFromPixels, draw, bind, unbind,
getWidth, getHeight, isAllocated, clear

## Fbo

Fbo::allocate, begin, end, draw, getTexture, getWidth, getHeight,
isAllocated, clear, readToPixels

## Shader

Shader::load, loadFromSource, begin, end, setUniform, isLoaded

## Font

Font::load, draw, drawString, getStringWidth, getStringHeight,
getStringBBox, getLineHeight, isLoaded

## Sound

Sound::load, play, stop, pause, resume, isPlaying, isLoaded,
setVolume, getVolume, setPan, getPan, setSpeed, getSpeed, setLoop, isLooping,
getPosition, setPosition, getDuration

## Animation - Easing

easeIn, easeOut, easeInOut, ease

## Animation - EaseType

EaseType::Linear, Quad, Cubic, Quart, Quint, Sine, Expo, Circ, Back, Elastic, Bounce

## Animation - EaseMode

EaseMode::In, Out, InOut

## Animation - Tween

Tween::from, to, duration, ease, start, pause, resume, reset, finish,
update, getValue, getProgress, isComplete, isPlaying, complete (event)

## Node

Node::addChild, removeChild, getChildren, getParent, getRoot,
setPosition, setRotation, setScale, getPosition, getRotation, getScale,
getGlobalPosition, getGlobalRotation, getGlobalScale, getGlobalMatrix,
show, hide, isVisible, setEnabled, isEnabled,
setup, update, draw, cleanup

## RectNode

RectNode::setSize, getSize, setColor, getColor, setBorderRadius,
contains, mousePressed, mouseReleased, mouseDragged, mouseScrolled (events)

## App Callbacks

setup, update, draw, cleanup, exit,
keyPressed, keyReleased,
mousePressed, mouseReleased, mouseMoved, mouseDragged, mouseScrolled,
windowResized, filesDropped

## App Methods

requestExit, isExitRequested

## WindowSettings

WindowSettings::setSize, setTitle, setHighDpi, setPixelPerfect,
setSampleCount, setFullscreen, setClipboardSize, setEnableDebugInput

## 3D

enable3D, enable3DPerspective, disable3D

## EasyCam

EasyCam::begin, end, setPosition, setTarget, setDistance,
getPosition, getTarget, getDistance, enableMouseInput, disableMouseInput

## Scissor

setScissor, resetScissor, pushScissor, popScissor

## Clipboard

setClipboardString, getClipboardString

## Screenshot

saveScreenshot, grabScreen

## Mouse Button Constants

MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE

## FPS Constants

VSYNC, EVENT_DRIVEN

## Logging

logNotice, logVerbose, logWarning, logError
