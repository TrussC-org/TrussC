// =============================================================================
// tcxTls tests - headless behavioral test for TlsClient (no window).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
//
// The TLS peer is this process's own: an mbedTLS server on 127.0.0.1 with a
// key and a self-signed certificate made at startup, so nothing leaves the
// machine and no key is committed. The client uses setVerifyNone(). The
// server speaks TLS 1.2 only and uses its own random generator, so it does
// not share mbedTLS's global PSA state with the client's receive thread
// (this mbedTLS build has no MBEDTLS_THREADING_C).
//
// Guards the invariants (#254):
//   - connect() after the peer ended the connection reconnects, whether the
//     peer closed it cleanly (close_notify) or a TLS error ended it, and 10
//     such reconnects leak no descriptors (counted on Linux). Both paths used
//     to leave the old socket open, and connect() created the new one over it.
//   - With an auto-reconnect onDisconnect listener attached (reconnect unless
//     the reason is "Disconnected by client"), disconnect() from another
//     thread reports exactly one onDisconnect, "Disconnected by client", and
//     leaves no connection. The receive thread used to report the EOF of
//     disconnect()'s own shutdown() as a remote close, and the listener
//     reconnected from it while disconnect() was joining that thread.
//   - A listener that reconnects on "Disconnected by client" ends up
//     connected: disconnect() resets the SSL context before it notifies.
//   - connect() to another peer while connected, with a listener that
//     reconnects on every onDisconnect: the listener's reconnect (to the old
//     peer, from inside connect()'s own disconnect) is closed again without
//     another notification or error, connect() reaches the new peer, and
//     nothing leaks (counted on Linux).
//   - The destructor fires no onDisconnect: destroying a client whose
//     listener reconnects on every onDisconnect finishes, reports nothing and
//     does not reconnect.
//   - A plain onError listener that reconnects after a failed handshake ends
//     up connected. The failed connection used to be torn down after the
//     listener returned, taking the new connection with it.
//
// The scenario runs on a worker with a deadline, so a hang reports FAIL
// instead of eating the CI job timeout.
// =============================================================================

#include <TrussC.h>
#include "tcTlsClient.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define TC_CLOSE closesocket
    using rawsocket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
    #include <errno.h>
    #include <sys/time.h>
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

#ifdef __linux__
    #include <dirent.h>
#endif

#if defined(MSG_NOSIGNAL)
    #define PEER_SEND_FLAGS MSG_NOSIGNAL
#else
    #define PEER_SEND_FLAGS 0
#endif

using namespace std;
using namespace tc;
using tcx::tls::TlsClient;

static const rawsocket_t kNoSocket = static_cast<rawsocket_t>(-1);

static atomic<int> g_fail{0};
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later abort
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time. The
// thread is detached so a hang still ends in a FAIL line and a non-zero exit.
template <typename F>
static bool completesWithin(int ms, F fn) {
    auto done = make_shared<atomic<bool>>(false);
    thread([done, fn = move(fn)]() mutable { fn(); done->store(true); }).detach();
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < deadline) {
        if (done->load()) return true;
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    return done->load();
}

// Poll pred until it holds or ms pass
template <typename P>
static bool waitFor(int ms, P pred) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    return pred();
}

// Leave without running destructors: a client left in a broken state may
// hang in its destructor, and the result is already decided.
[[noreturn]] static void bail() {
    printf("\nFAILED\n");
    fflush(stdout);
    _Exit(1);
}

// -----------------------------------------------------------------------------
// Raw loopback sockets
// -----------------------------------------------------------------------------

// A listening TCP socket on 127.0.0.1 with a port the OS picks
static rawsocket_t listenLoopback(int& port) {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket) return kNoSocket;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    socklen_t len = sizeof(addr);
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        ::getsockname(s, (sockaddr*)&addr, &len) != 0 ||
        ::listen(s, 8) != 0) {
        TC_CLOSE(s);
        return kNoSocket;
    }
    port = ntohs(addr.sin_port);
    return s;
}

static rawsocket_t acceptWithin(rawsocket_t listener, int ms) {
#ifdef _WIN32
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(listener, &fds);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    if (::select(0, &fds, nullptr, nullptr, &tv) <= 0) return kNoSocket;
#else
    pollfd pfd{listener, POLLIN, 0};
    if (::poll(&pfd, 1, ms) <= 0) return kNoSocket;
#endif
    return ::accept(listener, nullptr, nullptr);
}

static void setRecvTimeout(rawsocket_t s, int ms) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// Writing to a client that already closed must not raise SIGPIPE
static void setNoSigpipe(rawsocket_t s) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)s;
#endif
}

#ifdef __linux__
// Entries in a /proc directory: open descriptors of this process
static int countEntries(const char* path) {
    DIR* d = opendir(path);
    if (!d) return -1;
    int n = 0;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] != '.') ++n;
    }
    closedir(d);
    return n;
}
#endif

// -----------------------------------------------------------------------------
// The TLS peer (server side)
// -----------------------------------------------------------------------------

// One key, one self-signed certificate, one server config for every peer
struct TlsServer {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context key;
    mbedtls_x509_crt cert;
    mbedtls_ssl_config conf;

    TlsServer() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_pk_init(&key);
        mbedtls_x509_crt_init(&cert);
        mbedtls_ssl_config_init(&conf);
    }
    ~TlsServer() {
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
    TlsServer(const TlsServer&) = delete;
    TlsServer& operator=(const TlsServer&) = delete;

    bool setup() {
        const char* pers = "tcxTls-tests";
        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                  reinterpret_cast<const unsigned char*>(pers),
                                  strlen(pers)) != 0) return false;

        // EC P-256 key: quick to generate
        if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
            mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                                mbedtls_ctr_drbg_random, &drbg) != 0) return false;

        // Self-signed certificate for it
        mbedtls_x509write_cert crt;
        mbedtls_x509write_crt_init(&crt);
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);
        unsigned char serial[] = {1};
        unsigned char der[2048];
        int len = -1;
        if (mbedtls_x509write_crt_set_subject_name(&crt, "CN=localhost") == 0 &&
            mbedtls_x509write_crt_set_issuer_name(&crt, "CN=localhost") == 0 &&
            mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) == 0 &&
            mbedtls_x509write_crt_set_validity(&crt, "20240101000000", "20991231235959") == 0) {
            // Written at the end of the buffer
            len = mbedtls_x509write_crt_der(&crt, der, sizeof(der),
                                            mbedtls_ctr_drbg_random, &drbg);
        }
        mbedtls_x509write_crt_free(&crt);
        if (len <= 0) return false;
        if (mbedtls_x509_crt_parse_der(&cert, der + sizeof(der) - len, len) != 0) return false;

        if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0) return false;
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
        return mbedtls_ssl_conf_own_cert(&conf, &cert, &key) == 0;
    }
};

static int peerSend(void* ctx, const unsigned char* buf, size_t len) {
    rawsocket_t fd = *static_cast<rawsocket_t*>(ctx);
    int n = static_cast<int>(::send(fd, reinterpret_cast<const char*>(buf),
                                    static_cast<int>(len), PEER_SEND_FLAGS));
    return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n;
}

static int peerRecv(void* ctx, unsigned char* buf, size_t len) {
    rawsocket_t fd = *static_cast<rawsocket_t*>(ctx);
    int n = static_cast<int>(::recv(fd, reinterpret_cast<char*>(buf),
                                    static_cast<int>(len), 0));
    if (n >= 0) return n;   // 0 = EOF
#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
#endif
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

// One accepted connection. Reads time out every 50 ms and are retried until
// the caller's deadline.
struct TlsPeer {
    rawsocket_t fd = kNoSocket;
    mbedtls_ssl_context ssl;

    TlsPeer() { mbedtls_ssl_init(&ssl); }
    ~TlsPeer() { reset(); mbedtls_ssl_free(&ssl); }
    TlsPeer(const TlsPeer&) = delete;
    TlsPeer& operator=(const TlsPeer&) = delete;

    void reset() {
        if (fd != kNoSocket) {
            TC_CLOSE(fd);
            fd = kNoSocket;
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_init(&ssl);
    }

    // Accept the next connection and complete the server side of the handshake
    bool accept(rawsocket_t listener, mbedtls_ssl_config& conf, int ms) {
        reset();
        fd = acceptWithin(listener, ms);
        if (fd == kNoSocket) return false;
        setNoSigpipe(fd);
        setRecvTimeout(fd, 50);
        if (mbedtls_ssl_setup(&ssl, &conf) != 0) return false;
        mbedtls_ssl_set_bio(&ssl, &fd, peerSend, peerRecv, nullptr);
        const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
        int ret;
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) return false;
            if (chrono::steady_clock::now() >= deadline) return false;
        }
        return true;
    }

    bool write(const string& msg) {
        size_t off = 0;
        while (off < msg.size()) {
            int n = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(msg.data()) + off,
                                      msg.size() - off);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (n <= 0) return false;
            off += static_cast<size_t>(n);
        }
        return true;
    }

    // The client sent msg: read until exactly that arrived, or ms pass
    bool expect(const string& msg, int ms) {
        const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
        string got;
        unsigned char buf[256];
        while (got.size() < msg.size() && chrono::steady_clock::now() < deadline) {
            int n = mbedtls_ssl_read(&ssl, buf, sizeof(buf));
            if (n > 0) {
                got.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            } else if (n != MBEDTLS_ERR_SSL_WANT_READ && n != MBEDTLS_ERR_SSL_WANT_WRITE) {
                break;
            }
        }
        return got == msg;
    }

    // A clean close: close_notify, then the socket. The client sees a remote close.
    void closeNotify() {
        mbedtls_ssl_close_notify(&ssl);
        reset();
    }

    // Bytes that are not a TLS record, then close: the client's read fails
    // with a TLS error.
    void sendJunkAndClose() {
        const char junk[] = "this is not a TLS record";
        ::send(fd, junk, static_cast<int>(sizeof(junk) - 1), PEER_SEND_FLAGS);
        reset();
    }
};

// -----------------------------------------------------------------------------
// Scenario
// -----------------------------------------------------------------------------

static void scenario() {
    TlsClient client;   // constructed first: it also starts Winsock
    client.setVerifyNone();

    TlsServer server;
    check("TLS peer: key and certificate are set up", server.setup());
    int port = 0;
    rawsocket_t listener = listenLoopback(port);
    check("loopback listener is up", listener != kNoSocket);
    if (g_fail) bail();

    // Everything the client receives, from its receive thread
    mutex rxMutex;
    string received;
    EventListener rxSub = client.onReceive.listen([&](TcpReceiveEventArgs& e) {
        lock_guard<mutex> lock(rxMutex);
        received.append(e.data.begin(), e.data.end());
    });

    // The peer sends msg; the client must receive exactly that
    auto peerToClient = [&](TlsPeer& p, const string& msg) {
        {
            lock_guard<mutex> lock(rxMutex);
            received.clear();
        }
        if (!p.write(msg)) return false;
        return waitFor(3000, [&] {
            lock_guard<mutex> lock(rxMutex);
            return received == msg;
        });
    };
    auto isConnected = [&] { return client.isConnected(); };

    // --- first connection ---------------------------------------------------
    TlsPeer peer;
    check("initial connect()", client.connect("127.0.0.1", port));
    check("TLS peer completes the handshake", peer.accept(listener, server.conf, 5000));
    check("client is connected", waitFor(3000, isConnected));
    if (g_fail) bail();
    check("data reaches the peer", client.send("hello") && peer.expect("hello", 3000));
    check("the client receives the peer's data", peerToClient(peer, "hello back"));
    if (g_fail) bail();

    // --- reconnect after the peer ended the connection, repeatedly -----------
    // Even rounds: close_notify (the remote-close branch). Odd rounds: junk
    // (the TLS-error branch).
    const int rounds = 10;
    bool noticed = true, reconnected = true, handshook = true, connectedAgain = true,
         delivered = true;
#ifdef __linux__
    int fdsAfterFirst = -1, fdsAfterLast = -1;
#endif
    for (int i = 0; i < rounds; ++i) {
        if (i % 2 == 0) {
            peer.closeNotify();
        } else {
            peer.sendJunkAndClose();
        }
        if (!waitFor(3000, [&] { return !client.isConnected(); })) { noticed = false; break; }

        if (!client.connect("127.0.0.1", port)) { reconnected = false; break; }
        if (!peer.accept(listener, server.conf, 5000)) { handshook = false; break; }
        if (!waitFor(3000, isConnected)) { connectedAgain = false; break; }
        const string msg = "ping " + to_string(i);
        if (!client.send(msg) || !peer.expect(msg, 3000)) { delivered = false; break; }

#ifdef __linux__
        const int fds = countEntries("/proc/self/fd");
        if (i == 0) fdsAfterFirst = fds;
        fdsAfterLast = fds;
#endif
    }
    check("client notices each close_notify / TLS error", noticed);
    check("connect() after the peer ended it succeeds (10 rounds)", reconnected);
    check("TLS peer completes every new handshake", handshook);
    check("client is connected after every reconnect", connectedAgain);
    check("data reaches the peer after every reconnect", delivered);
    if (g_fail) bail();

#ifdef __linux__
    printf("  (open descriptors: %d after the first reconnect, %d after the last)\n",
           fdsAfterFirst, fdsAfterLast);
    check("reconnecting leaks no descriptors", fdsAfterLast <= fdsAfterFirst);
#else
    printf("%-60s %s\n", "reconnecting leaks no descriptors", "SKIP (counted on Linux)");
#endif
    check("the client receives the peer's data", peerToClient(peer, "after the reconnects"));
    if (g_fail) bail();

    // --- disconnect() with an auto-reconnect listener attached ---------------
    // The usual auto-reconnect: an inline onDisconnect listener that reconnects
    // unless the app itself disconnected. disconnect()'s shutdown() wakes the
    // receive thread with EOF, which it must not report as a remote close.
    mutex reasonsMutex;
    vector<string> reasons;
    atomic<int> autoReconnects{0};
    auto autoReconnect = [&](TlsClient* c) {
        return c->onDisconnect.listen([&, c](TcpDisconnectEventArgs& e) {
            {
                lock_guard<mutex> lock(reasonsMutex);
                reasons.push_back(e.reason);
            }
            if (e.reason != "Disconnected by client") {
                ++autoReconnects;
                c->connect("127.0.0.1", port);
            }
        });
    };
    // A connection the client should not have made. Waiting for it is also
    // the grace period for a late onDisconnect from a stray receive thread.
    auto strayConnection = [&] {
        rawsocket_t stray = acceptWithin(listener, 300);
        if (stray == kNoSocket) return false;
        TC_CLOSE(stray);
        return true;
    };

    EventListener autoSub = autoReconnect(&client);
    thread([&] { client.disconnect(); }).join();   // not the receive thread
    const bool strayAfterDisconnect = strayConnection();
    {
        lock_guard<mutex> lock(reasonsMutex);
        check("auto-reconnect: disconnect() reports one onDisconnect",
              reasons.size() == 1);
        check("auto-reconnect: it says \"Disconnected by client\"",
              reasons.size() == 1 && reasons[0] == "Disconnected by client");
    }
    check("auto-reconnect: disconnect() does not reconnect",
          autoReconnects == 0 && !strayAfterDisconnect);
    check("auto-reconnect: the client is left disconnected", !client.isConnected());
    autoSub.disconnect();
    peer.reset();
    if (g_fail) bail();

    // --- a reconnect from the "Disconnected by client" onDisconnect ----------
    // disconnect() fires onDisconnect inline on the calling thread, and this
    // listener reconnects even then: its connect() starts a new handshake on
    // the SSL context. disconnect() used to reset that context after the
    // notification, freeing it under the new receive thread. The listener
    // waits a little after its connect(), as one that goes on with other work
    // would: by then the new handshake is under way (its ClientHello sent).
    check("by-client reconnect: connect()", client.connect("127.0.0.1", port));
    check("by-client reconnect: TLS peer completes the handshake",
          peer.accept(listener, server.conf, 5000));
    check("by-client reconnect: client is connected", waitFor(3000, isConnected));
    if (g_fail) bail();
    atomic<bool> byClientArmed{true};
    atomic<int> byClientReconnect{-1};   // -1 not run, 0 connect() failed, 1 ok
    EventListener byClientSub = client.onDisconnect.listen([&](TcpDisconnectEventArgs&) {
        if (byClientArmed.exchange(false)) {
            byClientReconnect = client.connect("127.0.0.1", port) ? 1 : 0;
            this_thread::sleep_for(chrono::milliseconds(200));
        }
    });
    client.disconnect();
    byClientSub.disconnect();
    check("by-client reconnect: the listener's connect() returned true",
          byClientReconnect == 1);
    check("by-client reconnect: TLS peer completes the new handshake",
          peer.accept(listener, server.conf, 5000));
    check("by-client reconnect: client is connected again", waitFor(3000, isConnected));
    if (g_fail) bail();
    check("by-client reconnect: data reaches the peer",
          client.send("reconnected") && peer.expect("reconnected", 3000));
    check("by-client reconnect: the client receives the peer's data",
          peerToClient(peer, "welcome again"));
    client.disconnect();
    peer.reset();
    if (g_fail) bail();

    // --- connect() elsewhere while connected, with a listener that reconnects -
    // connect() on a connected client first disconnects, and this listener,
    // which reconnects on every onDisconnect, reconnects to the old peer from
    // inside that call; its receive thread starts a handshake there, which no
    // one answers. connect() then closes that connection again, silently, and
    // must not wait for that handshake: it used to join the thread before
    // shutting the socket down, which hung, and a thread left running there
    // raced connect() on the same std::thread (join against detach).
    int portB = 0;
    rawsocket_t listenerB = listenLoopback(portB);
    check("connect(B): second TLS listener is up", listenerB != kNoSocket);
    check("connect(B): connect() to the first peer", client.connect("127.0.0.1", port));
    check("connect(B): TLS peer completes the handshake", peer.accept(listener, server.conf, 5000));
    check("connect(B): client is connected", waitFor(3000, isConnected));
    if (g_fail) bail();
    mutex everyMutex;
    vector<string> everyReasons;
    EventListener everySub = client.onDisconnect.listen([&](TcpDisconnectEventArgs& e) {
        {
            lock_guard<mutex> lock(everyMutex);
            everyReasons.push_back(e.reason);
        }
        client.connect("127.0.0.1", port);
        // As a listener that goes on with other work would: by then the
        // new receive thread is blocked in its handshake
        this_thread::sleep_for(chrono::milliseconds(200));
    });
    atomic<int> errorsDuringSwitch{0};
    EventListener switchErrSub = client.onError.listen([&](TcpErrorEventArgs&) {
        ++errorsDuringSwitch;
    });
#ifdef __linux__
    const int fdsBeforeSwitch = countEntries("/proc/self/fd");
    const int threadsBeforeSwitch = countEntries("/proc/self/task");
#endif
    bool switchOk = false;
    check("connect(B): connect() finishes within 10 s",
          completesWithin(10000, [&] { switchOk = client.connect("127.0.0.1", portB); }));
    if (g_fail) bail();
    everySub.disconnect();
    check("connect(B): connect() returns true", switchOk);
    {
        lock_guard<mutex> lock(everyMutex);
        check("connect(B): one onDisconnect, \"Disconnected by client\"",
              everyReasons.size() == 1 && everyReasons[0] == "Disconnected by client");
    }
    TlsPeer peerB;
    check("connect(B): the new TLS peer completes the handshake",
          peerB.accept(listenerB, server.conf, 5000));
    check("connect(B): client is connected to the new peer", waitFor(3000, isConnected));
    if (g_fail) bail();
    check("connect(B): data reaches the new peer",
          client.send("moved") && peerB.expect("moved", 3000));
    check("connect(B): the client receives the new peer's data", peerToClient(peerB, "hello from B"));
    check("connect(B): no onError while switching", errorsDuringSwitch == 0);
    switchErrSub.disconnect();

    // The listener's connection reached the first listener (its handshake
    // unanswered), and the client has closed it again
    rawsocket_t overruled = acceptWithin(listener, 2000);
    bool overruledClosed = false;
    if (overruled != kNoSocket) {
        setRecvTimeout(overruled, 2000);
        char buf[512];
        int n;
        while ((n = static_cast<int>(::recv(overruled, buf, sizeof(buf), 0))) > 0) {}
        overruledClosed = n == 0;   // EOF after its ClientHello, not a timeout
        TC_CLOSE(overruled);
    }
    check("connect(B): the listener's connection is closed again",
          overruled != kNoSocket && overruledClosed);
    rawsocket_t extra = acceptWithin(listener, 300);
    check("connect(B): no further reconnect to the first peer", extra == kNoSocket);
    if (extra != kNoSocket) TC_CLOSE(extra);
    peer.reset();   // the first connection, which the client closed
    if (g_fail) bail();
#ifdef __linux__
    int fdsAfterSwitch = -1, threadsAfterSwitch = -1;
    waitFor(1000, [&] {
        fdsAfterSwitch = countEntries("/proc/self/fd");
        threadsAfterSwitch = countEntries("/proc/self/task");
        return fdsAfterSwitch <= fdsBeforeSwitch && threadsAfterSwitch <= threadsBeforeSwitch;
    });
    printf("  (descriptors: %d before, %d after; threads: %d before, %d after)\n",
           fdsBeforeSwitch, fdsAfterSwitch, threadsBeforeSwitch, threadsAfterSwitch);
    check("connect(B): no descriptor or thread left over",
          fdsAfterSwitch <= fdsBeforeSwitch && threadsAfterSwitch <= threadsBeforeSwitch);
#else
    printf("%-60s %s\n", "connect(B): no descriptor or thread left over", "SKIP (counted on Linux)");
#endif
    client.disconnect();
    peerB.reset();
    TC_CLOSE(listenerB);
    if (g_fail) bail();

    // --- destroying a client with a reconnecting listener attached ----------
    // The destructor disconnects without onDisconnect. This listener reconnects
    // on every onDisconnect, "Disconnected by client" included: told by the
    // destructor, it would reconnect a client that is going away. Nor may the
    // receive thread report the EOF of the destructor's own shutdown().
    auto doomed = make_unique<TlsClient>();
    doomed->setVerifyNone();
    check("destroyed client: connect()", doomed->connect("127.0.0.1", port));
    check("destroyed client: TLS peer completes the handshake",
          peer.accept(listener, server.conf, 5000));
    check("destroyed client: client is connected",
          waitFor(3000, [&] { return doomed->isConnected(); }));
    if (g_fail) bail();
    // Data from the peer puts the receive thread in its receive loop, blocked
    // in a read by the time the destructor runs, as it is in a live app
    atomic<bool> doomedReceived{false};
    EventListener doomedRx = doomed->onReceive.listen([&](TcpReceiveEventArgs&) {
        doomedReceived = true;
    });
    check("destroyed client: the client receives the peer's data",
          peer.write("hi") && waitFor(3000, [&] { return doomedReceived.load(); }));
    if (g_fail) bail();
    atomic<int> doomedDisconnects{0};
    TlsClient* doomedPtr = doomed.get();
    EventListener doomedSub = doomed->onDisconnect.listen([&, doomedPtr](TcpDisconnectEventArgs&) {
        ++doomedDisconnects;
        doomedPtr->connect("127.0.0.1", port);
    });
    check("destroyed client: the destructor finishes within 5 s",
          completesWithin(5000, [&] { doomed.reset(); }));
    if (g_fail) bail();
    const bool strayAfterDestroy = strayConnection();
    check("destroyed client: no onDisconnect from the destructor", doomedDisconnects == 0);
    check("destroyed client: no reconnect after destruction", !strayAfterDestroy);
    // doomedSub and doomedRx outlive their Events: disconnecting them is a no-op
    peer.reset();
    if (g_fail) bail();

    // --- a failed handshake, then a reconnect from onError -------------------
    // The first peer accepts TCP but is not TLS: it closes at once, and the
    // client's handshake fails. A plain onError listener then reconnects to
    // the TLS peer from the receive thread. The failed connection has to be
    // gone before onError fires; tearing it down afterwards took the new
    // connection with it.
    int plainPort = 0;
    rawsocket_t plainListener = listenLoopback(plainPort);
    check("plain (not TLS) listener is up", plainListener != kNoSocket);
    if (g_fail) bail();

    atomic<bool> errArmed{true};
    atomic<int> errReconnect{-1};   // -1 not run, 0 connect() failed, 1 ok
    EventListener errSub = client.onError.listen([&](TcpErrorEventArgs& e) {
        if (e.message.find("handshake") != string::npos && errArmed.exchange(false)) {
            errReconnect = client.connect("127.0.0.1", port) ? 1 : 0;
        }
    });
    atomic<int> connectFailures{0};
    EventListener failSub = client.onConnect.listen([&](TcpConnectEventArgs& e) {
        if (!e.success) ++connectFailures;
    });

    check("handshake failure: connect() to the plain peer", client.connect("127.0.0.1", plainPort));
    rawsocket_t plainPeer = acceptWithin(plainListener, 2000);
    check("handshake failure: the plain peer accepted", plainPeer != kNoSocket);
    if (plainPeer != kNoSocket) TC_CLOSE(plainPeer);   // the handshake fails
    if (g_fail) bail();
    check("handshake failure: onError reconnected (connect() true)",
          waitFor(5000, [&] { return errReconnect.load() == 1; }));
    check("handshake failure: TLS peer completes the new handshake",
          peer.accept(listener, server.conf, 5000));
    check("handshake failure: client is connected", waitFor(3000, isConnected));
    check("handshake failure: onConnect(false) for the failed attempt",
          waitFor(1000, [&] { return connectFailures.load() == 1; }));
    errSub.disconnect();
    failSub.disconnect();
    if (g_fail) bail();
    check("handshake failure: data reaches the peer afterwards",
          client.send("after the failed handshake") &&
          peer.expect("after the failed handshake", 3000));
    check("handshake failure: the client receives the peer's data",
          peerToClient(peer, "welcome"));
    if (g_fail) bail();

    // --- teardown ---------------------------------------------------------
    client.disconnect();
    check("disconnect() leaves the client disconnected", !client.isConnected());
    peer.reset();
    TC_CLOSE(plainListener);
    TC_CLOSE(listener);
}

int main() {
    // mbedTLS starts its PSA subsystem lazily in the first TLS 1.3-capable
    // handshake, on the client's receive thread. Start it here instead, before
    // any thread exists: this build has no MBEDTLS_THREADING_C.
    if (psa_crypto_init() != PSA_SUCCESS) {
        check("psa_crypto_init()", false);
        bail();
    }
    if (!completesWithin(60000, scenario)) {
        check("scenario finished within 60 s", false);
        bail();
    }
    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
