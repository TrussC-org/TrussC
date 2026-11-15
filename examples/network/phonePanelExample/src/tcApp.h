#pragma once
#include <TrussC.h>
#include <deque>
#include <mutex>
#include <thread>

using namespace std;
using namespace tc;
namespace httplib { class Server; }

class tcApp : public App {
public:
    tcApp();
    ~tcApp() override;
    void setup() override;
    void draw() override;
    void exit() override;
    void keyPressed(int key) override { if (key == KEY_ESCAPE) exitApp(); }

private:
    struct LogLines {
        mutex guard;
        deque<string> lines;
    };
    shared_ptr<LogLines> logs = make_shared<LogLines>();
    unique_ptr<httplib::Server> server;
    thread serverThread;
    EventListener logListener;
    string url;
    int radio = 0;
    float fader = 0.5f;
};
