#include "tcApp.h"
#include <iostream>

using namespace std;

// Shader effects (Metal MSL fragment shaders)

// Effect 0: Gradient
const char* effectGradient = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float time;
    float _pad0[3];
    float4 resolution;
    float4 mouse;
    float4 custom;
};

struct FragmentIn {
    float4 position [[position]];
    float2 texcoord;
};

fragment float4 fragmentMain(FragmentIn in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]]) {
    float2 uv = in.texcoord;

    // Rainbow gradient changing over time
    float3 col = 0.5 + 0.5 * cos(u.time + uv.xyx + float3(0, 2, 4));

    return float4(col, 1.0);
}
)";

// Effect 1: Ripple
const char* effectRipple = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float time;
    float _pad0[3];
    float4 resolution;
    float4 mouse;
    float4 custom;
};

struct FragmentIn {
    float4 position [[position]];
    float2 texcoord;
};

fragment float4 fragmentMain(FragmentIn in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]]) {
    float2 uv = in.texcoord - 0.5;
    float aspect = u.resolution.x / u.resolution.y;
    uv.x *= aspect;

    float dist = length(uv);
    float wave = sin(dist * 20.0 - u.time * 3.0) * 0.5 + 0.5;
    wave *= 1.0 - smoothstep(0.0, 0.5, dist);

    float3 col = mix(float3(0.1, 0.1, 0.3), float3(0.3, 0.6, 1.0), wave);

    return float4(col, 1.0);
}
)";

// Effect 2: Plasma
const char* effectPlasma = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float time;
    float _pad0[3];
    float4 resolution;
    float4 mouse;
    float4 custom;
};

struct FragmentIn {
    float4 position [[position]];
    float2 texcoord;
};

fragment float4 fragmentMain(FragmentIn in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]]) {
    float2 uv = in.texcoord * 4.0;
    float t = u.time * 0.5;

    float v1 = sin(uv.x + t);
    float v2 = sin(uv.y + t);
    float v3 = sin(uv.x + uv.y + t);
    float v4 = sin(length(uv - float2(2.0)) + t);

    float v = v1 + v2 + v3 + v4;

    float3 col;
    col.r = sin(v * 3.14159) * 0.5 + 0.5;
    col.g = sin(v * 3.14159 + 2.094) * 0.5 + 0.5;
    col.b = sin(v * 3.14159 + 4.188) * 0.5 + 0.5;

    return float4(col, 1.0);
}
)";

// Effect 3: Mouse follow
const char* effectMouse = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float time;
    float _pad0[3];
    float4 resolution;
    float4 mouse;
    float4 custom;
};

struct FragmentIn {
    float4 position [[position]];
    float2 texcoord;
};

fragment float4 fragmentMain(FragmentIn in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]]) {
    float2 uv = in.texcoord;
    float2 mouseUV = u.mouse.xy / u.resolution.xy;

    float dist = distance(uv, mouseUV);
    float glow = 0.05 / (dist + 0.01);
    glow = clamp(glow, 0.0, 1.0);

    float3 col = float3(glow * 0.5, glow * 0.8, glow);

    // Background grid
    float2 grid = fract(uv * 20.0);
    float gridLine = step(0.95, grid.x) + step(0.95, grid.y);
    col += gridLine * 0.1;

    return float4(col, 1.0);
}
)";

void tcApp::setup() {
    cout << "shaderExample: Custom Shader Demo" << endl;
    cout << "  - Press 1-4 to switch effects" << endl;
    cout << "  - Press SPACE to cycle effects" << endl;

    loadEffect(currentEffect);
}

void tcApp::update() {
    // Nothing
}

void tcApp::draw() {
    clear(0);

    // Apply shader and draw fullscreen
    shader.begin();
    shader.setUniformTime(getElapsedTime());
    shader.setUniformResolution(getWindowWidth(), getWindowHeight());
    shader.setUniformMouse(getGlobalMouseX(), getGlobalMouseY());
    shader.draw();
    shader.end();

    // Display effect name
    string effectNames[] = {"Gradient", "Ripple", "Plasma", "Mouse Follow"};
    string info = "Effect " + to_string(currentEffect + 1) + ": " + effectNames[currentEffect];
    drawBitmapStringHighlight(info, 10, 20,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
    drawBitmapStringHighlight("Press 1-4 or SPACE to change", 10, 40,
        Color(0, 0, 0, 0.7f), Color(0.7f, 0.7f, 0.7f));
}

void tcApp::keyPressed(int key) {
    if (key == '1') loadEffect(0);
    else if (key == '2') loadEffect(1);
    else if (key == '3') loadEffect(2);
    else if (key == '4') loadEffect(3);
    else if (key == ' ') {
        currentEffect = (currentEffect + 1) % NUM_EFFECTS;
        loadEffect(currentEffect);
    }
}

void tcApp::loadEffect(int index) {
    const char* sources[] = {effectGradient, effectRipple, effectPlasma, effectMouse};

    currentEffect = index;
    if (shader.loadFromSource(sources[index])) {
        cout << "Loaded effect " << (index + 1) << endl;
    } else {
        cout << "Failed to load effect " << (index + 1) << endl;
    }
}
