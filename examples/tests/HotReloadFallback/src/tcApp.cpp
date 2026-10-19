#include "tcApp.h"

// Desktop: host/guest split. Web / Android / iOS: a no-op, normal app.
TC_HOT_RELOAD(tcApp)

void tcApp::setup() {
    setWindowTitle("HotReloadFallback");
}

void tcApp::draw() {
    clear(0.12f);

    float t = getElapsedTimef();
    float cx = getWindowWidth() * 0.5f;
    float cy = getWindowHeight() * 0.5f;

    setColor(0.3f, 0.7f, 1.0f);
    drawCircle(cx + cos(t * 0.25f * TAU) * 120.0f, cy + sin(t * 0.25f * TAU) * 120.0f, 30.0f);

    setColor(1.0f);
#ifdef TC_HOT_RELOAD_BUILD
    drawBitmapString("TC_HOT_RELOAD: host/guest build", 20, 30);
#else
    drawBitmapString("TC_HOT_RELOAD: normal build (no hot reload on this platform)", 20, 30);
#endif
}
