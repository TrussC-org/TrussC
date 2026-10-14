#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcTcpServer.h"
#include <future>

namespace {
void startStopHandoff() {
    TcpServer server;
    promise<void> entered, release;
    auto proceed = release.get_future().share();
    auto ready = entered.get_future();
    internal::setTcpServerStartHookForTests([&] {
        entered.set_value();
        proceed.wait();
    });
    atomic<bool> started{false}, stopping{false};
    thread starter([&] { started = server.start(0); });
    check("start reaches accept handoff hook", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    thread stopper([&] { stopping = true; server.stop(); });
    check("stop enters during start handoff", until([&] { return stopping.load(); }));
    release.set_value();
    starter.join();
    stopper.join();
    internal::setTcpServerStartHookForTests({});
    check("start/stop handoff leaves stopped server", started && !server.isRunning() && server.getClientCount() == 0);
    check("server restarts after concurrent stop", server.start(0));
    server.stop();
}
void clientStopsAndDestroys() {
    for (bool destroy : {false, true}) {
        auto server = make_unique<TcpServer>();
        atomic<bool> done{false};
        auto receive = server->onReceive.listen([&](TcpServerReceiveEventArgs& e) {
            if (destroy) server.reset();
            else { server->disconnectClient(e.clientId); server->stop(); }
            done = true;
        });
        check("client stop fixture starts", server->start(0));
        TcpClient client;
        check("client stop fixture connects", client.connect("127.0.0.1", server->getPort()));
        check("client stop trigger sent", client.send("x"));
        check("client thread stop/destruction returns", until([&] { return done.load(); }));
        client.disconnect();
        if (server) server->stop();
    }
}
} // namespace
TC_CORE_TEST_MAIN() {
    Watchdog watchdog;
    startStopHandoff();
    clientStopsAndDestroys();
    return failures ? 1 : 0;
}
