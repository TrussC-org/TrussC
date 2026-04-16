#include "tcApp.h"

void tcApp::setup() {
    setImmersiveMode(true);
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.08f);

    float x = 20;
    float y = 30;
    float w = getWindowWidth() - 40;
    float barH = 18;
    float lineH = 24;

    // --- Battery ---
    y = drawSection(x, y, "Battery");
    float battery = getBatteryLevel();
    bool charging = isBatteryCharging();
    string batteryStr = (battery < 0) ? "N/A" : to_string((int)(battery * 100)) + "%" + (charging ? " [charging]" : "");
    drawBar(x, y, w, barH, max(battery, 0.f), batteryStr);
    y += lineH + 8;

    // --- Volume ---
    y = drawSection(x, y, "Volume");
    float vol = getSystemVolume();
    if (vol < 0) {
        drawBar(x, y, w, barH, 0, "N/A");
    } else {
        drawBar(x, y, w, barH, vol, "Volume: " + to_string((int)(vol * 100)) + "%");
    }
    y += lineH + 8;

    // --- Brightness ---
    // Note: on Android, the value is gamma-corrected (perceptual), not linear.
    // Slider at max may report ~0.64 rather than 1.0. This is normal OS behavior.
    // On iOS, the value matches the slider position linearly (0.0 - 1.0).
    y = drawSection(x, y, "Brightness");
    float bright = getSystemBrightness();
    if (bright < 0) {
        drawBar(x, y, w, barH, 0, "N/A");
    } else {
        drawBar(x, y, w, barH, bright, "Brightness: " + to_string((int)(bright * 100)) + "%");
    }
    y += lineH + 8;

    // --- Thermal ---
    y = drawSection(x, y, "Thermal");
    auto state = getThermalState();
    float temp = getThermalTemperature();
    string stateNames[] = {"Nominal", "Fair", "Serious", "Critical"};
    Color stateColors[] = {colors::green, colors::yellow, colors::orange, colors::red};
    int si = (int)state;
    setColor(stateColors[si]);
    string tempStr = (temp < 0) ? "" : " (" + to_string((int)temp) + "C)";
    drawBitmapString(stateNames[si] + tempStr, x, y);
    setColor(colors::white);
    y += lineH + 8;

    // --- Accelerometer ---
    y = drawSection(x, y, "Accelerometer");
    Vec3 accel = getAccelerometer();
    drawBitmapString("x:" + to_string(accel.x).substr(0,5)
                  + " y:" + to_string(accel.y).substr(0,5)
                  + " z:" + to_string(accel.z).substr(0,5), x, y);
    y += lineH + 8;

    // --- Gyroscope ---
    y = drawSection(x, y, "Gyroscope");
    Vec3 gyro = getGyroscope();
    drawBitmapString("x:" + to_string(gyro.x).substr(0,5)
                  + " y:" + to_string(gyro.y).substr(0,5)
                  + " z:" + to_string(gyro.z).substr(0,5), x, y);
    y += lineH;
    Quaternion att = getDeviceOrientation();
    drawBitmapString("q: " + to_string(att.w).substr(0,5)
                  + " " + to_string(att.x).substr(0,5)
                  + " " + to_string(att.y).substr(0,5)
                  + " " + to_string(att.z).substr(0,5), x, y);
    y += lineH + 8;

    // --- Compass ---
    y = drawSection(x, y, "Compass");
    float heading = getCompassHeading();
    drawBitmapString("Heading: " + to_string(heading / TAU * 360).substr(0,5) + " deg", x, y);
    // Draw a simple needle
    float cx = x + w - 30;
    float cy = y - 4;
    noFill();
    setColor(0.3f);
    drawCircle(cx, cy, 20);
    setColor(colors::red);
    drawLine(cx, cy, cx - sin(heading) * 18, cy - cos(heading) * 18);
    setColor(colors::white);
    y += lineH + 8;

    // --- Proximity ---
    y = drawSection(x, y, "Proximity");
    bool prox = isProximityClose();
    setColor(prox ? colors::red : colors::green);
    drawBitmapString(prox ? "CLOSE" : "far", x, y);
    setColor(colors::white);
    y += lineH + 8;

    // --- Location ---
    y = drawSection(x, y, "Location");
    auto loc = getLocation();
    if (loc.accuracy >= 0) {
        drawBitmapString("Lat: " + to_string(loc.latitude), x, y);
        y += lineH;
        drawBitmapString("Lon: " + to_string(loc.longitude), x, y);
        y += lineH;
        drawBitmapString("Alt: " + to_string((int)loc.altitude) + "m  Acc: " + to_string((int)loc.accuracy) + "m", x, y);
    } else {
        drawBitmapString("Requesting...", x, y);
    }
}

void tcApp::touchPressed(const TouchEventArgs& touch) {
    // Toggle immersive mode on tap
    setImmersiveMode(!getImmersiveMode());
}

// --- Helpers ---

void tcApp::drawBar(float x, float y, float w, float h, float value, const string& label) {
    // Background
    setColor(0.2f);
    drawRect(x, y, w, h);
    // Fill
    setColor(0.15f, 0.5f, 0.8f);
    drawRect(x, y, w * clamp(value, 0.f, 1.f), h);
    // Label
    setColor(colors::white);
    drawBitmapString(label, x + 4, y + 2);
}

float tcApp::drawSection(float x, float y, const string& title) {
    setColor(0.5f);
    drawBitmapString(title, x, y);
    setColor(colors::white);
    return y + 20;
}
