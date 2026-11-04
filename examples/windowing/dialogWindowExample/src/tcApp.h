#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// The DIALOG window's content: a question and an OK button. The answer goes
// back to the main App through the callback given at construction:
//   OK button     -> onAnswer(true),  then Window::close()
//   OS close (x)  -> onAnswer(false)  (closeRequested, treated as cancel)
class ConfirmApp : public App {
public:
    ConfirmApp(string question, function<void(bool)> onAnswer)
        : question_(std::move(question)), onAnswer_(std::move(onAnswer)) {}

    void setup() override {
        // OS close button: answer "cancel" and let the window close.
        closeListener_ = events().closeRequested.listen([this](CloseRequestEventArgs&) {
            answer(false);
        });
    }

    void draw() override {
        clear(0.20f, 0.22f, 0.28f);
        setColor(1.0f);
        drawBitmapString(question_, 20, 34);
        setColor(0.7f);
        drawBitmapString("close (x) = cancel", 20, 54);

        // OK button
        const Rect ok = okRect();
        const bool hover = ok.contains(getMouseX(), getMouseY());
        setColor(hover ? Color(0.35f, 0.65f, 1.0f) : Color(0.25f, 0.50f, 0.85f));
        drawRect(ok.x, ok.y, ok.width, ok.height);
        setColor(1.0f);
        drawBitmapString("OK", ok.getCenterX(), ok.getCenterY(), Direction::Center, Direction::Center);
    }

    void mousePressed(Vec2 pos, int button) override {
        if (button != MOUSE_BUTTON_LEFT || !okRect().contains(pos.x, pos.y)) return;
        answer(true);
        // Code close skips closeRequested, so the listener above does not run.
        getWindow()->close();
    }

private:
    Rect okRect() const { return Rect(getWidth() - 110, getHeight() - 50, 90, 32); }

    // Deliver the answer once, whichever way the dialog ends.
    void answer(bool ok) {
        if (answered_) return;
        answered_ = true;
        if (onAnswer_) onAnswer_(ok);
    }

    string question_;
    function<void(bool)> onAnswer_;
    EventListener closeListener_;
    bool answered_ = false;
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(Vec2 pos, int button) override;

private:
    void openDialog();
    bool dialogOpen() const { return dialog_ && dialog_->isOpen(); }
    void blockedInput();

    shared_ptr<Window> dialog_;
    vector<Vec2> circles_;
    string lastAnswer_ = "-";

    // Background flash while the dialog blocks input
    float bgBrightness_ = 0.0f;
    float bgTarget_ = 0.0f;
};
