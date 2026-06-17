#include "tcApp.h"

// Draw a sprite purely from its reflected data — proof the values are real.
static void drawSprite(const Sprite& s) {
    pushMatrix();
    translate(s.transform.position.x, s.transform.position.y);
    rotateZ(s.transform.rotation);
    scale(s.transform.scale);
    setColor(s.color);
    const float r = s.getRadius();
    switch (s.shape) {
        case Shape::Circle:   drawCircle(0, 0, r); break;
        case Shape::Square:   drawRect(-r, -r, r * 2, r * 2); break;
        case Shape::Triangle: drawTriangle(0, -r, r, r, -r, r); break;
    }
    popMatrix();
}

void tcApp::setup() {
    runSelfTest();
}

// Exercise the whole reflection round-trip and check every feature.
void tcApp::runSelfTest() {
    auto check = [&](bool ok, const string& what) {
        report_.push_back((ok ? "[OK]   " : "[FAIL] ") + what);
        if (!ok) allPassed_ = false;
    };
    allPassed_ = true;

    // --- SAVE: one call turns the sprite into JSON ---
    Json saved = reflectToJson(sprite_);
    savedJson_ = saved.dump(2);
    logNotice("Saved sprite:\n" + savedJson_);

    check(saved["shape"] == "Circle",        "enum saves as its label");
    check(saved["transform"].is_object(),    "composite nests into its own object");
    check(saved.contains("diameter"),        "read-only value is still dumped");

    // --- EDIT the JSON, then LOAD it onto a fresh sprite ---
    Json edit = saved;
    edit["shape"] = "Triangle";        // enum read back from a label
    edit["radius"] = 45;              // written through the setter
    edit["diameter"] = 999;           // read-only -> reader must refuse it
    edit["transform"]["scale"] = 1.4;  // write into the nested composite
    edit["transform"]["position"] = Json::array({640, 250});

    JsonReadReflector r(edit);
    loaded_.reflectMembers(r);
    loadedJson_ = reflectToJson(loaded_).dump(2);

    check(loaded_.shape == Shape::Triangle,    "enum loads from a label");
    check(loaded_.getRadius() == 45.0f,        "3-arg setter wrote the value");
    check(loaded_.transform.scale == 1.4f,     "nested composite loaded");
    // The reader reports the read-only key it skipped, instead of writing it:
    bool roReported = std::find(r.readOnly.begin(), r.readOnly.end(),
                                "diameter") != r.readOnly.end();
    check(roReported && loaded_.diameter() != 999.0f,
          "read-only key reported + not applied");

    // The 3-arg setter doesn't just store — it runs. Loading 0.1 clamps to 1:
    Sprite clampDemo;
    JsonReadReflector cr(Json{{"radius", 0.1}});
    clampDemo.reflectMembers(cr);
    check(clampDemo.getRadius() == 1.0f, "setter logic runs on load (clamp)");

    logNotice(allPassed_ ? "reflectExample: ALL CHECKS PASSED"
                         : "reflectExample: SOME CHECKS FAILED");
}

void tcApp::draw() {
    clear(0.12f);

    // The two sprites, drawn from reflected data (original / loaded-from-JSON).
    drawSprite(sprite_);
    drawSprite(loaded_);

    setColor(colors::white);
    drawBitmapString("ORIGINAL (sprite_)", 160, 120);
    drawBitmapString("LOADED FROM EDITED JSON (loaded_)", 520, 120);

    // Pass/fail report.
    float y = 380;
    setColor(allPassed_ ? colors::limeGreen : colors::tomato);
    drawBitmapString(allPassed_ ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", 20, y);
    setColor(colors::white);
    for (const string& line : report_) {
        y += 16;
        drawBitmapString(line, 20, y);
    }

    // The JSON the sprite produced, line by line (drawBitmapString is single-line).
    auto drawJson = [](const string& title, const string& json, float x, float y) {
        setColor(colors::gold);
        drawBitmapString(title, x, y);
        setColor(colors::lightGray);
        size_t start = 0;
        float ly = y + 16;
        while (start <= json.size()) {
            size_t nl = json.find('\n', start);
            string line = json.substr(start, nl == string::npos ? string::npos : nl - start);
            drawBitmapString(line, x, ly);
            ly += 14;
            if (nl == string::npos) break;
            start = nl + 1;
        }
    };
    drawJson("reflectToJson(sprite_):", savedJson_, 480, 380);
    drawJson("reflectToJson(loaded_):", loadedJson_, 740, 380);
}
