// =============================================================================
// stencilExample - Draw only inside a shape, like a mask in Photoshop.
//
// The stencil buffer is an invisible image the same size as the screen.
// While drawing in shape mode, shapes go into that invisible image instead of
// the screen, so no color appears. Switch to inside mode, and later drawing
// shows up only where the stencil shape was drawn.
// Like setColor() and setBlendMode(), setStencilDesc() switches the mode for
// everything drawn after it.
// =============================================================================

#include "tcApp.h"

void tcApp::draw() {
    clear(0.12f);

    // start each frame with an empty stencil
    clearStencil();

    // shape mode: draw into the stencil buffer only (nothing appears on screen)
    setStencilDesc(StencilDesc::shape());

    // draw a circle into the stencil: from now on, drawing stays inside it
    drawCircle(480, 300, 180);

    // inside mode: draw on screen, but only inside the stencil shape
    // (StencilDesc::outside() would draw only outside it instead)
    setStencilDesc(StencilDesc::inside());

    // a rectangle wider than the circle: its left and right ends are cut
    // off by the circle, so those edges come out curved
    setColor(0.2f, 0.7f, 1.0f);
    drawRect(250, 230, 460, 140);

    // back to normal drawing
    resetStencilDesc();
}
