#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    EventListener exitListener_, closeListener_;
    bool quitConfirmed_ = false, confirmationPending_ = false;
    void confirmExit();
    void onExitRequested(ExitRequestEventArgs& args);
};
