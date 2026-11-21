#include "tcApp.h"

void tcApp::setup() {
    inputListener = getWindow()->events().mousePressed.listen([](MouseEventArgs& e) {
        logNotice("input") << e.injected << " " << e.sourceTag;
    });
    getWindow()->injectInput(InputEvent::mousePressed({20, 30}, MouseButton::Left), "tuio");
    getWindow()->injectInput(InputEvent::mouseMoved({80, 30}), "tuio");
    getWindow()->injectInput(InputEvent::mouseReleased({80, 30}, MouseButton::Left), "tuio");
}
