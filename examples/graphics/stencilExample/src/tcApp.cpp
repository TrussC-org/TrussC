#include "tcApp.h"

void tcApp::draw() {
    clear(0.12f);
    clearStencil();
    setStencilDesc(StencilDesc::shape());
    drawCircle(480, 300, 180);
    setStencilDesc(StencilDesc::inside());
    setColor(0.2f, 0.7f, 1.0f);
    drawRect(250, 230, 460, 140);
    resetStencilDesc();
}
