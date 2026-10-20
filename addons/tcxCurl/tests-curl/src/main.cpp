// =============================================================================
// tcxCurl tests-curl - headless behavioral test for HttpClient (no window).
// Links libcurl, unlike the curl-free ../tests.
//
// Built and run by the daily CI via examples/build_all.py --addon-tests-only
// --include-daily (daily-only marker; exit 0 = pass, non-zero = fail).
// Locally: trusscli run -p . from this directory.
//
// The HTTPS peer is a small Python http.server (GET and POST answer 200) on
// 127.0.0.1, port chosen by the OS (port 0). Its key and self-signed
// certificate are made by `openssl req` at startup in a temporary directory,
// so nothing leaves the machine and no key is committed. Without an `openssl`
// or Python command on PATH the handshake checks print SKIP. The option
// checks always run and determine the exit status.
//
// setTlsCACertificate() (#401):
//   - without a CA, a request to the self-signed server fails (the OS default
//     store does not hold it);
//   - with the server's certificate as the CA, get() and uploadFile() return
//     200;
//   - with another self-signed certificate, or text that is not a PEM, the
//     request fails: verification stays on;
//   - an empty string goes back to the OS default store, and the request
//     fails again.
// On Linux the PEM must be taken. Elsewhere the system libcurl may not take
// a PEM from memory; then the request must fail with an error naming
// setTlsCACertificate, and the test says so.
// =============================================================================

#include <TrussC.h>
#include "../../tests/ResponseMemoryTrace.h"
#include <curl/curl.h>
#include <string>
#include <type_traits>
#include <vector>

// Observe the real HttpClient call sites and inject unsupported options without
// contacting a peer. Outside these checks the wrappers call system libcurl.
struct TlsOptionsTrace {
    bool enabled = false;
    CURLoption reject = CURLOPT_LASTENTRY;
    CURLcode rejection = CURLE_UNKNOWN_OPTION;
    std::vector<CURLoption> options;
    std::string pem;
    long sslOptions = 0;
    bool caFileCleared = false, caPathCleared = false, verificationDisabled = false;
    bool performed = false;
    CURLcode transferResult = CURLE_ABORTED_BY_CALLBACK;
};
static TlsOptionsTrace g_tlsTrace;

// Exercise the production hop loop and real curl URL parser without contacting
// link-local interfaces. Only transport results/getinfo are supplied by this trace.
static struct {
    bool enabled = false;
    size_t hop = 0;
    std::string url, target;
    std::vector<std::string> headers;
} g_zoneTrace;

template<class T>
static CURLcode testCurlSetopt(CURL* handle, CURLoption option, T value) {
    if (g_zoneTrace.enabled) {
        if constexpr (std::is_same_v<T, const char*>) {
            if (option == CURLOPT_URL) g_zoneTrace.url = value;
        }
        if constexpr (std::is_same_v<T, curl_slist*>) {
            if (option == CURLOPT_HTTPHEADER && value) {
                std::string block;
                for (auto* item = value; item; item = item->next) block += std::string(item->data) + "\n";
                g_zoneTrace.headers.push_back(block);
            }
        }
    }
    if (!g_tlsTrace.enabled) return curl_easy_setopt(handle, option, value);
    g_tlsTrace.options.push_back(option);
    if (option == g_tlsTrace.reject) return g_tlsTrace.rejection;
#if LIBCURL_VERSION_NUM >= 0x074D00
    if constexpr (std::is_same_v<T, curl_blob*>) {
        if (option == CURLOPT_CAINFO_BLOB) {
            g_tlsTrace.pem.assign(static_cast<const char*>(value->data), value->len);
        }
    }
#endif
    if constexpr (std::is_pointer_v<T>) {
        if (option == CURLOPT_CAINFO) g_tlsTrace.caFileCleared = value == nullptr;
        if (option == CURLOPT_CAPATH) g_tlsTrace.caPathCleared = value == nullptr;
    }
    if constexpr (std::is_same_v<T, long>) {
        if (option == CURLOPT_SSL_OPTIONS) g_tlsTrace.sslOptions = value;
        if ((option == CURLOPT_SSL_VERIFYPEER || option == CURLOPT_SSL_VERIFYHOST) && value == 0) {
            g_tlsTrace.verificationDisabled = true;
        }
    }
    return CURLE_OK;
}

static CURLcode testCurlPerform(CURL* handle) {
    if (g_zoneTrace.enabled) { ++g_zoneTrace.hop; return CURLE_OK; }
    if (!g_tlsTrace.enabled) return curl_easy_perform(handle);
    g_tlsTrace.performed = true;
    return g_tlsTrace.transferResult;
}

template<class T>
static CURLcode testCurlGetinfo(CURL* handle, CURLINFO info, T output) {
    if (g_zoneTrace.enabled) {
        if constexpr (std::is_same_v<T, long*>) {
            if (info == CURLINFO_RESPONSE_CODE) { *output = g_zoneTrace.hop == 1 ? 302 : 200; return CURLE_OK; }
        }
        if constexpr (std::is_same_v<T, char**>) {
            if (info == CURLINFO_EFFECTIVE_URL) { *output = g_zoneTrace.url.data(); return CURLE_OK; }
            if (info == CURLINFO_REDIRECT_URL) {
                *output = g_zoneTrace.hop == 1 ? g_zoneTrace.target.data() : nullptr;
                return CURLE_OK;
            }
        }
    }
    return curl_easy_getinfo(handle, info, output);
}

#undef curl_easy_setopt
#define curl_easy_setopt testCurlSetopt
#define curl_easy_perform testCurlPerform
#undef curl_easy_getinfo
#define curl_easy_getinfo testCurlGetinfo
#include <tcxCurl.h>
#undef curl_easy_setopt
#undef curl_easy_perform
#undef curl_easy_getinfo

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/wait.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <spawn.h>
    #include <unistd.h>
    extern char** environ;
#endif

using namespace std;
namespace fs = std::filesystem;

static int g_failures = 0;

static void check(const string& what, bool ok, const string& detail = "") {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(),
           detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++g_failures;
}

static string readFile(const fs::path& p) {
    ifstream f(p, ios::binary);
    stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testTlsOptions() {
    const string pem = "test PEM bytes";
    tcx::curl::HttpClient http;
    http.setBaseUrl("https://127.0.0.1:1");
    for (bool upload : {false, true}) {
        string name = upload ? "uploadFile(): " : "get(): ";
        auto request = [&] { return upload ? http.uploadFile("/", "unused.txt") : http.get("/"); };
        auto reset = [&] { g_tlsTrace = {}; g_tlsTrace.enabled = true; };
        auto defaultOptions = [] {
            long options = 0;
#ifdef _WIN32
#ifdef CURLSSLOPT_NATIVE_CA
            options |= CURLSSLOPT_NATIVE_CA;
#endif
#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
            options |= CURLSSLOPT_REVOKE_BEST_EFFORT;
#endif
#endif
            return options;
        };
        auto sslOptionCalls = [&] {
            return std::count(g_tlsTrace.options.begin(), g_tlsTrace.options.end(), CURLOPT_SSL_OPTIONS);
        };

        http.setTlsCACertificate("");
        for (auto option : {CURLOPT_WRITEFUNCTION, CURLOPT_WRITEDATA, CURLOPT_HEADERFUNCTION,
                            CURLOPT_HEADERDATA, CURLOPT_XFERINFOFUNCTION, CURLOPT_XFERINFODATA,
                            CURLOPT_NOPROGRESS, CURLOPT_ACCEPT_ENCODING, CURLOPT_DNS_CACHE_TIMEOUT}) {
            reset();
            g_tlsTrace.reject = option;
            const auto bounded = request();
            check(name + "unsupported response option stops before transfer",
                  bounded.statusCode == 0 && bounded.body.empty() && !bounded.error.empty() && !g_tlsTrace.performed);
        }
        reset();
        const auto metadataBefore = tcx::curl::detail::responseLossCountForTests(tcx::curl::detail::ResponseLoss::Metadata);
        const auto backendBefore = tcx::curl::detail::responseLossCountForTests(tcx::curl::detail::ResponseLoss::BackendHeader);
        g_tlsTrace.transferResult = CURLE_OUT_OF_MEMORY;
        const auto oom = request();
        check(name + "allocation failure without header evidence is not metadata loss",
              oom.statusCode == 0 && oom.body.empty() && !oom.error.empty() &&
              tcx::curl::detail::responseLossCountForTests(tcx::curl::detail::ResponseLoss::Metadata) == metadataBefore &&
              tcx::curl::detail::responseLossCountForTests(tcx::curl::detail::ResponseLoss::BackendHeader) == backendBefore);
        reset();
        http.setTlsCACertificate("");
        request();
        const auto defaults = g_tlsTrace.options;
        check(name + "default CA keeps existing TLS options",
              g_tlsTrace.pem.empty() && !g_tlsTrace.caFileCleared && !g_tlsTrace.caPathCleared &&
              g_tlsTrace.sslOptions == defaultOptions() && g_tlsTrace.performed);
        check(name + "SSL options applied once, only when needed",
              sslOptionCalls() == (defaultOptions() != 0 ? 1 : 0));
        if (defaultOptions() != 0) {
            for (auto rejection : {CURLE_UNKNOWN_OPTION, CURLE_NOT_BUILT_IN}) {
                reset();
                g_tlsTrace.reject = CURLOPT_SSL_OPTIONS;
                g_tlsTrace.rejection = rejection;
                const auto res = request();
                check(name + "unsupported default SSL options stop before transfer",
                      res.statusCode == 0 && !res.error.empty() && !g_tlsTrace.performed);
            }
        }

#if LIBCURL_VERSION_NUM >= 0x074D00
        reset();
        http.setTlsCACertificate(pem);
        request();
        check(name + "PEM replaces CA file and directory",
              g_tlsTrace.pem == pem && g_tlsTrace.caFileCleared && g_tlsTrace.caPathCleared &&
              !g_tlsTrace.verificationDisabled && g_tlsTrace.performed);
#if defined(_WIN32) && defined(CURLSSLOPT_REVOKE_BEST_EFFORT)
        check(name + "PEM keeps best-effort revocation without native CA",
              g_tlsTrace.sslOptions == static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT) &&
              sslOptionCalls() == 1);
#else
        check(name + "PEM needs no SSL options", sslOptionCalls() == 0);
#endif
        for (auto rejection : {CURLE_UNKNOWN_OPTION, CURLE_NOT_BUILT_IN}) {
            for (auto option : {CURLOPT_CAINFO_BLOB, CURLOPT_CAINFO, CURLOPT_CAPATH,
#if defined(_WIN32) && defined(CURLSSLOPT_REVOKE_BEST_EFFORT)
                                CURLOPT_SSL_OPTIONS,
#endif
                               }) {
                reset();
                g_tlsTrace.reject = option;
                g_tlsTrace.rejection = rejection;
                const auto res = request();
                if (option == CURLOPT_CAPATH && rejection == CURLE_NOT_BUILT_IN) {
                    check(name + "backend without CA directories still accepts PEM",
                          g_tlsTrace.pem == pem && g_tlsTrace.performed);
                    continue;
                }
                check(name + "unsupported TLS option " + to_string(option) + "/" + to_string(rejection),
                      res.statusCode == 0 && res.error.find("setTlsCACertificate") != string::npos &&
                      !g_tlsTrace.performed, res.error);
            }
        }
#else
        reset();
        http.setTlsCACertificate(pem);
        const auto res = request();
        check(name + "old curl headers reject CA PEM before transfer",
              res.statusCode == 0 && res.error.find("setTlsCACertificate") != string::npos &&
              !g_tlsTrace.performed, res.error);
#endif
        reset();
        http.setTlsCACertificate("");
        request();
        check(name + "empty PEM restores default options",
              g_tlsTrace.options == defaults && g_tlsTrace.sslOptions == defaultOptions());
    }
    g_tlsTrace.enabled = false;
}

static void testHeaderNamesAndZones() {
    for (const auto& name : {string(), string(" \t"), string("X Bad"), string("X\tBad"),
             string("X:Bad"), string("X\rBad"), string("X\nBad"), string("X\x01"),
             string("X\x7f"), string("X\xc3\xa9"), string("X\0Bad", 5)}) {
        tcx::HttpClient client;
        client.addHeader("X-Good", "value"); // invalid later entry must still prevent all I/O
        client.addHeader(name, "secret");
        g_tlsTrace = {};
        g_tlsTrace.enabled = true;
        const auto res = client.get("http://127.0.0.1:1/");
        const auto upload = client.uploadFile("http://127.0.0.1:1/", "unused-file");
        check("invalid header refuses request and upload before transfer",
              !g_tlsTrace.performed && res.statusCode == 0 && res.body.empty() &&
              !res.error.empty() && upload.statusCode == 0 && !upload.error.empty());
        g_tlsTrace.enabled = false;
    }
    struct ZoneCase {
        const char* name;
        const char* from;
        const char* to;
        const char* rawFrom;
        const char* rawTo;
        bool keep;
    };
    for (const auto& zone : {
        ZoneCase{"same IPv6 zone retains credentials", "%251", "%251", "1", "1", true},
        ZoneCase{"changed IPv6 zone drops credentials", "%251", "%252", "1", "2", false},
        ZoneCase{"encoded delimiter and raw numeric zone retain credentials", "%251", "%1", "1", "1", true},
        ZoneCase{"encoded interface name drops credentials", "%25eth0", "%25eth%30", "eth0", "eth%30", false},
        ZoneCase{"encoded source interface drops credentials", "%25eth%30", "%25eth0", "eth%30", "eth0", false},
        ZoneCase{"identical escaped zones conservatively drop credentials", "%25eth%30", "%25eth%30", "eth%30", "eth%30", false},
        ZoneCase{"same literal interface retains credentials", "%25eth0", "%25eth0", "eth0", "eth0", true},
        ZoneCase{"interface names are case sensitive", "%25eth0", "%25ETH0", "eth0", "ETH0", false}
    }) {
        const string from = "http://[fe80::1" + string(zone.from) + "]:1/start";
        const string to = "http://[fe80::1" + string(zone.to) + "]:1/next";
        auto rawZone = [](const string& url, const char* expected) {
            CURLU* parsed = curl_url();
            if (!parsed) return false;
            char* raw = nullptr;
            const bool ok = curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK &&
                curl_url_get(parsed, CURLUPART_ZONEID, &raw, 0) == CURLUE_OK && raw && string(raw) == expected;
            curl_free(raw);
            curl_url_cleanup(parsed);
            return ok;
        };
        check(string("actual curl raw zone parse: ") + zone.name,
              rawZone(from, zone.rawFrom) && rawZone(to, zone.rawTo));
        g_zoneTrace = {};
        g_zoneTrace.enabled = true;
        g_zoneTrace.target = to;
        tcx::HttpClient client;
        for (auto name : tcx::curl::detail::credentialHeaders) client.addHeader(string(name), "zone-secret");
        client.addHeader("X-Neutral", "kept");
        const auto res = client.get(from);
        bool credentials = g_zoneTrace.headers.size() == 2;
        if (credentials) {
            for (auto name : tcx::curl::detail::credentialHeaders) {
                const string header = string(name) + ": zone-secret";
                credentials = credentials && g_zoneTrace.headers[0].find(header) != string::npos &&
                    (g_zoneTrace.headers[1].find(header) != string::npos) == zone.keep;
            }
        }
        check(zone.name, res.statusCode == 200 && res.error.empty() && credentials &&
              g_zoneTrace.headers[1].find("X-Neutral: kept") != string::npos);
        g_zoneTrace.enabled = false;
    }
}

static int run(const string& cmd) {
#ifdef _WIN32
    // cmd.exe strips one pair of outer quotes.
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

static string quoted(const fs::path& p) { return "\"" + p.string() + "\""; }

#ifdef _WIN32
static const char* kNull = "NUL";
#else
static const char* kNull = "/dev/null";
#endif

// The Python HTTPS server as a child process, stopped in the destructor.
struct TlsServer {
#ifdef _WIN32
    PROCESS_INFORMATION pi{};
    bool started = false;
#else
    pid_t pid = -1;
#endif

    bool start(const string& python, const fs::path& script, const fs::path& cert,
               const fs::path& key, const fs::path& portFile, const fs::path& logFile) {
#ifdef _WIN32
        string cmd = python + " " + quoted(script) + " " + quoted(cert) + " " + quoted(key) +
                     " " + quoted(portFile) + " " + quoted(logFile);
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        vector<char> buf(cmd.begin(), cmd.end());
        buf.push_back('\0');
        started = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
        return started;
#else
        // No C++ work in a fork child: macOS may already have runtime threads.
        string s = script.string(), c = cert.string(), k = key.string();
        string p = portFile.string(), l = logFile.string();
        char* argv[] = {const_cast<char*>(python.c_str()), s.data(), c.data(),
                        k.data(), p.data(), l.data(), nullptr};
        posix_spawn_file_actions_t actions;
        if (posix_spawn_file_actions_init(&actions) != 0) return false;
        int rc = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        if (rc == 0) rc = posix_spawnp(&pid, python.c_str(), &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        if (rc != 0) pid = -1;
        return rc == 0;
#endif
    }

    bool isRunning() {
#ifdef _WIN32
        return started && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT;
#else
        if (pid <= 0) return false;
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            pid = -1;  // Already reaped; don't signal a reused PID in the destructor.
            return false;
        }
        return true;
#endif
    }

    ~TlsServer() {
#ifdef _WIN32
        if (started) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
#else
        if (pid > 0) {
            kill(pid, SIGTERM);
            int status = 0;
            waitpid(pid, &status, 0);
        }
#endif
    }
};

static const char* kServerScript = R"PY(import os, sys

# Keep startup errors on every platform, including errors importing ssl.
sys.stderr = open(sys.argv[4], "w", buffering=1)
import http.server, socketserver, ssl, gzip, threading, json, urllib.parse

class Handler(http.server.BaseHTTPRequestHandler):
    def answer(self):
        self.connection.settimeout(15) # bounded fixture failure instead of a hung upload
        if self.path == "/request-count":
            data = str(self.server.accepted).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        n = int(self.headers.get("Content-Length") or 0)
        payload = self.rfile.read(n) if n else b""
        route = self.path
        if route.startswith("/to?") or route.startswith("/hop/") or route == "/echo":
            if route == "/echo":
                data = json.dumps({"method": self.command, "body": payload.decode(),
                    "headers": {k.lower(): v for k, v in self.headers.items()}}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                if self.command != "HEAD": self.wfile.write(data)
                return
            if route.startswith("/hop/"):
                left = int(route.rsplit("/", 1)[1])
                code, location = 302, ("/hop/" + str(left - 1) if left > 1 else "/echo")
            else:
                query = urllib.parse.parse_qs(urllib.parse.urlsplit(route).query)
                code, location = int(query.get("status", ["302"])[0]), query["url"][0]
            self.send_response(code)
            self.send_header("Location", location)
            if "pad" in route: self.send_header("X-Pad", "a" * 40000)
            self.send_header("Content-Length", "3")
            self.end_headers()
            if self.command != "HEAD": self.wfile.write(b"old")
            return
        if route in ("/wire-budget", "/wire-budget-final"):
            data = gzip.compress(b"") # 20 wire bytes per hop, zero decoded bytes
            self.send_response(302 if route == "/wire-budget" else 200)
            if route == "/wire-budget": self.send_header("Location", "/wire-budget-final")
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if route.startswith("/budget/"):
            # Every hop is individually within 4 bytes; only the sum can reject.
            size = int(route.rsplit("/", 1)[1])
            self.send_response(302)
            self.send_header("Location", "/budget-final/" + str(size))
            self.send_header("Content-Length", str(size))
            self.end_headers()
            self.wfile.write(b"a" * size)
            return
        if route.startswith("/budget-final/"):
            size = int(route.rsplit("/", 1)[1])
            self.send_response(200)
            self.send_header("Content-Length", str(size))
            self.end_headers()
            self.wfile.write(b"b" * size)
            return
        limit = 64 * 1024 * 1024
        if route in ("/backend-header", "/backend-status"):
            prefix = b"HTTP/1.1 200 " if route == "/backend-status" else b"HTTP/1.1 200 OK\r\nX-Huge: "
            try: self.wfile.write(prefix + b"a" * 150000 + b"\r\nContent-Length: 2\r\n\r\nok")
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError): pass
            return
        if route.startswith("/trailer/"):
            trailer = {"normal": b"X-Trailer: yes", "short": b"HTTP/1.1 200: yes", "long": b"HTTP/1.1 200 OK: yes"}[route.rsplit("/", 1)[1]]
            self.wfile.write(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nbody\r\n0\r\n" + trailer + b"\r\n\r\n")
            return
        if route in ("/head", "/not-modified", "/no-content"):
            status = {"/head": b"200 OK", "/not-modified": b"304 Not Modified", "/no-content": b"204 No Content"}[route]
            self.wfile.write(b"HTTP/1.1 " + status + b"\r\nContent-Length: 999999999\r\nConnection: close\r\n\r\n")
            return
        if route == "/interim":
            self.wfile.write(b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok")
            return
        if route in ("/metadata-complete", "/metadata-excess"):
            prefix = b"HTTP/1.0 200 OK\r\nContent-Length: 2\r\nX-Pad: "
            ending = b"\r\n\r\n"
            count = 65536 + (1 if route == "/metadata-excess" else 0) - len(prefix) - len(ending)
            try: self.wfile.write(prefix + b"a" * count + ending + b"ok")
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError): pass
            return
        if route == "/cancel":
            self.send_response(200)
            self.send_header("Content-Length", "1024")
            self.end_headers()
            self.wfile.write(b"a")
            self.wfile.flush()
            self.rfile.read(1) # client timeout/close releases the fixture, no sleep
            return
        if route in ("/redirect", "/redirect-loop"):
            self.send_response(302)
            self.send_header("Location", "/redirect-loop" if route == "/redirect-loop" else "/")
            self.send_header("Content-Length", "3")
            self.end_headers()
            self.wfile.write(b"old")
            return
        self.send_response(200)
        if route == "/metadata":
            for i in range(80): self.send_header("X-Data-" + str(i), "a" * 1024)
        if route == "/metadata-small": self.send_header("X-Pad", "a" * 40000)
        if route in ("/compressed", "/compressed-complete"):
            packed = gzip.compress(b"a" * (limit if route == "/compressed-complete" else limit + 1))
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(packed)))
            self.end_headers()
            try: self.wfile.write(packed)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError): pass
            return
        amount = limit if route == "/complete" else limit + 1
        chunked = route == "/chunks"
        large = route in ("/complete", "/excess", "/chunks", "/unknown")
        if chunked: self.send_header("Transfer-Encoding", "chunked")
        elif route == "/short": self.send_header("Content-Length", "1000")
        elif route != "/unknown": self.send_header("Content-Length", str(amount if large else 2))
        self.end_headers()
        try:
            if large:
                while amount:
                    block = b"a" * min(amount, 16384)
                    if chunked: self.wfile.write(("%x\r\n" % len(block)).encode())
                    self.wfile.write(block)
                    if chunked: self.wfile.write(b"\r\n")
                    amount -= len(block)
                if chunked: self.wfile.write(b"0\r\n\r\n")
            else: self.wfile.write(b"ok")
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError): pass
    do_GET = answer
    do_POST = answer
    do_HEAD = answer
    do_PUT = answer
    do_DELETE = answer
    def log_message(self, *args):
        pass

class LoopbackServer(http.server.HTTPServer):
    accepted = 0
    def get_request(self):
        connection = super().get_request()
        self.accepted += 1 # count even malformed requests before header parsing
        return connection
    def server_bind(self):
        # HTTPServer normally calls getfqdn(), which can wait on external DNS.
        socketserver.TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]

cert, key, port_file = sys.argv[1], sys.argv[2], sys.argv[3]
server = LoopbackServer(("127.0.0.1", 0), Handler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(cert, key)
server.socket = ctx.wrap_socket(server.socket, server_side=True)
for suffix in (".http", ".other"):
    plain = LoopbackServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=plain.serve_forever, daemon=True).start()
    with open(port_file + suffix, "w") as f: f.write(str(plain.server_address[1]))
with open(port_file + ".tmp", "w") as f:
    f.write(str(server.server_address[1]))
os.replace(port_file + ".tmp", port_file)
server.serve_forever()
)PY";

static void testInvalidHeadersAtPeer(const string& base, const fs::path& uploadPath) {
    tcx::HttpClient probe;
    probe.setBaseUrl(base);
    probe.setTimeout(10);
    auto count = [&] {
        const auto response = probe.get("/request-count");
        check("request-count control reaches loopback peer", response.ok());
        return response.ok() ? stoi(response.body) : -1000;
    };
    auto failed = [](const tcx::HttpResponse& response) {
        return response.statusCode == 0 && response.body.empty() &&
            response.error.find("Invalid HTTP header") != string::npos;
    };
    size_t warnings = 0;
    bool leakedValue = false;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning && event.message.find("Invalid HTTP header") != string::npos) {
            ++warnings;
            if (event.message.find("private-secret") != string::npos) leakedValue = true;
        }
    });
    auto rejected = [&](tcx::HttpClient& client, const string& label) {
        const int before = count();
        const auto response = client.get("/echo");
        const auto upload = client.uploadFile("/echo", uploadPath.string());
        check(label + ": request and upload fail whole", failed(response) && failed(upload));
        check(label + ": peer receives no request or connection", count() == before + 1);
    };
    for (const auto& name : {string(), string(" \t"), string("X Bad"), string("X\tBad"),
             string("X:Bad"), string("X\rBad"), string("X\nBad"), string("X\x01"),
             string("X\x7f"), string("X\xc3\xa9"), string("X\0Bad", 5)}) {
        tcx::HttpClient client;
        client.setBaseUrl(base);
        client.setTimeout(10);
        client.addHeader("X-Good", "value");
        client.addHeader(name, "private-secret");
        const size_t before = warnings;
        rejected(client, "invalid name");
        client.clearHeaders();
        client.addHeader("X-Value", "private-secret\r");
        rejected(client, "invalid value after name");
        check("name and value failures share one per-client Warning", warnings == before + 1);
    }
    for (char forbidden : {'\r', '\n', '\0'}) {
        for (int position : {0, 1, 2}) {
            const string value = (position == 0 ? string() : string("private-secret")) +
                string(1, forbidden) + (position == 2 ? string() : string("private-secret"));
            for (bool bearer : {false, true}) {
                tcx::HttpClient client;
                client.setBaseUrl(base);
                client.setTimeout(10);
                client.addHeader("X-Good", "value");
                if (bearer) client.setBearerToken(value);
                else client.addHeader("X-Value", value);
                const size_t before = warnings;
                rejected(client, bearer ? "invalid Bearer value" : "invalid app value");
                rejected(client, "repeated invalid value");
                client.clearHeaders();
                check("clearing invalid headers allows retry", client.get("/echo").ok());
                client.addHeader("X Bad", "private-secret");
                rejected(client, "invalid name after value and valid retry");
                check("one Warning per client survives clear and successful retry", warnings == before + 1);
            }
            // The Content-Type synthesized from the body request's argument is
            // also app input; it must not bypass configured-header validation.
            tcx::HttpClient client;
            client.setBaseUrl(base);
            client.setTimeout(10);
            const size_t beforeWarnings = warnings;
            const int before = count();
            const auto response = client.postRaw("/echo", "payload", value);
            const auto repeated = client.postRaw("/echo", "payload", value);
            check("invalid Content-Type fails whole", failed(response) && failed(repeated));
            check("invalid Content-Type: peer receives nothing", count() == before + 1);
            check("invalid Content-Type uses per-client Warning", warnings == beforeWarnings + 1);
        }
    }
    check("invalid-header Warning never logs app values", !leakedValue);
    tcx::HttpClient original;
    original.setBaseUrl(base);
    original.addHeader("X Bad", "private-secret");
    const size_t beforeCopies = warnings;
    rejected(original, "original client");
    auto copy = original;
    rejected(copy, "copied client");
    check("copied clients each have their own first Warning", warnings == beforeCopies + 2);
    copy = original;
    rejected(copy, "assigned client");
    check("assignment preserves the destination Warning lifetime", warnings == beforeCopies + 2);
    tcx::HttpClient valid;
    valid.setBaseUrl(base);
    valid.addHeader(" X-Value\t", "plain\tvalue");
    valid.addHeader("X-Empty", "");
    const auto response = valid.get("/echo");
    check("valid name trimming and tab value still work", response.ok() &&
          response.json()["headers"].value("x-value", "") == "plain\tvalue");
}

static bool makeSelfSigned(const fs::path& dir, const string& name, const fs::path& cfg) {
    string cmd = "openssl req -x509 -sha256 -newkey rsa:2048 -nodes -days 2 -subj /CN=tcxcurl-test.invalid"
                 " -config " + quoted(cfg) + " -extensions v3" +
                 " -keyout " + quoted(dir / (name + "-key.pem")) +
                 " -out " + quoted(dir / (name + ".pem")) +
                 " > " + kNull + " 2>&1";
    return run(cmd) == 0 && fs::exists(dir / (name + ".pem"));
}

static void testCaCertificate(const fs::path& dir, const string& python) {
    fs::path cfg = dir / "openssl.cnf";
    {
        ofstream f(cfg);
        f << "[req]\n"
             "distinguished_name = dn\n"
             "[dn]\n"
             "[v3]\n"
             "basicConstraints = critical,CA:TRUE\n"
             "keyUsage = critical,digitalSignature,keyEncipherment,keyCertSign\n"
             "subjectKeyIdentifier = hash\n"
             "subjectAltName = IP:127.0.0.1\n";
    }
    fs::path uploadPath = dir / "upload.txt";
    {
        ofstream f(uploadPath);
        f << "hello\n";
    }
    {
        ofstream f(dir / "server.py");
        f << kServerScript;
    }

    bool made = makeSelfSigned(dir, "server", cfg) && makeSelfSigned(dir, "other", cfg);
    check("openssl made two self-signed certificates", made);
    if (!made) return;
    string serverPem = readFile(dir / "server.pem");
    string otherPem = readFile(dir / "other.pem");

    fs::path portFile = dir / "port.txt";
    fs::path logFile = dir / "server.log";
    TlsServer server;
    bool started = server.start(python, dir / "server.py", dir / "server.pem",
                                dir / "server-key.pem", portFile, logFile);
    int port = 0;
    for (int i = 0; started && i < 200 && port == 0; ++i) {
        if (fs::exists(portFile)) port = atoi(readFile(portFile).c_str());
        if (port == 0 && !server.isRunning()) break;
        if (port == 0) this_thread::sleep_for(chrono::milliseconds(50));
    }
    check("HTTPS server listens on 127.0.0.1", port > 0,
          port > 0 ? to_string(port) : (started ? "server exited or did not become ready\n" + readFile(logFile)
                                              : "could not start Python"));
    if (port <= 0) return;

    tcx::curl::HttpClient http;
    http.setBaseUrl("https://127.0.0.1:" + to_string(port));
    http.setTimeout(10);

    auto res = http.get("/");
    check("no CA: get() fails", !res.error.empty() && res.statusCode == 0, res.error);
    auto up = http.uploadFile("/", uploadPath.string());
    check("no CA: uploadFile() fails", !up.error.empty() && up.statusCode == 0, up.error);

    http.setTlsCACertificate(serverPem);
    res = http.get("/");
    bool notTaken = res.error.find("setTlsCACertificate") != string::npos;
#ifdef __linux__
    bool allowNotTaken = false;
#else
    bool allowNotTaken = true;
#endif
    if (notTaken && allowNotTaken) {
        printf("  [NOTE] this libcurl does not take a CA PEM: %s\n", res.error.c_str());
        check("server's certificate as CA: get() fails with a setTlsCACertificate error",
              res.statusCode == 0);
        up = http.uploadFile("/", uploadPath.string());
        check("server's certificate as CA: uploadFile() fails with a setTlsCACertificate error",
              up.statusCode == 0 && up.error.find("setTlsCACertificate") != string::npos, up.error);
    } else {
        check("server's certificate as CA: get() returns 200",
              res.error.empty() && res.statusCode == 200,
              res.error.empty() ? to_string(res.statusCode) : res.error);
        up = http.uploadFile("/", uploadPath.string());
        check("server's certificate as CA: uploadFile() returns 200",
              up.error.empty() && up.statusCode == 200,
              up.error.empty() ? to_string(up.statusCode) : up.error);

        const string fixtureFilePath = fs::absolute(uploadPath).generic_string();
        const string fixtureFileUrl = "file://" + (fixtureFilePath.front() == '/' ? string() : string("/")) + fixtureFilePath;
        const string tlsUrl = "https://127.0.0.1:" + to_string(port);
        const string plainUrl = "http://127.0.0.1:" + readFile(portFile.string() + ".http");
        const string otherUrl = "http://127.0.0.1:" + readFile(portFile.string() + ".other");
        testInvalidHeadersAtPeer(plainUrl, uploadPath);
        auto wholeFailure = [](const tcx::HttpResponse& value) {
            return value.statusCode == 0 && value.body.empty() && !value.error.empty();
        };
        // Request paths contain only loopback URLs without query delimiters.
        auto to = [](const string& target, int status = 302) {
            return "/to?status=" + to_string(status) + "&url=" + target;
        };
        tcx::HttpClient redirects;
        redirects.setTlsCACertificate(serverPem);
        redirects.setBaseUrl(plainUrl);
        redirects.setTimeout(10);
        res = redirects.get(to("/echo"));
        check("default follows HTTP redirects", res.ok() && res.json()["method"] == "GET");
        redirects.setBaseUrl(tlsUrl);
        res = redirects.get(to("/echo"));
        check("default follows HTTPS redirects", res.ok() && res.json()["method"] == "GET");
        for (const auto& base : {plainUrl, tlsUrl}) {
            redirects.setBaseUrl(base);
            redirects.setFollowRedirects(false);
            res = redirects.get(to("/echo"));
            check("explicit false returns complete 3xx", res.statusCode == 302 && res.body == "old" && res.error.empty());
            redirects.setFollowRedirects(true);
        }
        using namespace tcx::curl::detail;
        const auto policyBefore = responseLossCountForTests(ResponseLoss::RedirectPolicy);
        size_t policyWarnings = 0;
        auto policyListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Warning && event.message.find("redirect policy") != string::npos) ++policyWarnings;
        });
        for (const auto& base : {plainUrl, tlsUrl}) {
            redirects.setBaseUrl(base);
            for (const auto& target : {string("ftp://127.0.0.1:1/no"), fixtureFileUrl}) {
                res = redirects.get(to(target));
                check("non-HTTP redirect target refused", wholeFailure(res));
            }
        }
        redirects.setBaseUrl(tlsUrl);
        res = redirects.get(to(plainUrl + "/echo"));
        check("HTTPS downgrade refused", wholeFailure(res));
        check("redirect policy losses counted with first Warning", policyWarnings == 1 &&
              responseLossCountForTests(ResponseLoss::RedirectPolicy) == policyBefore + 5);
        redirects.setBaseUrl(plainUrl);
        res = redirects.get(to(tlsUrl + "/echo"));
        check("HTTP to HTTPS succeeds", res.ok() && res.json()["method"] == "GET");
        for (auto name : credentialHeaders) redirects.addHeader(string(name), "app-secret");
        redirects.setBearerToken("app-secret");
        redirects.addHeader("X-Neutral", "retained");
        for (const auto& target : {string("/echo"), otherUrl + "/echo", tlsUrl + "/echo"}) {
            res = redirects.get(to(target));
            check("origin redirect returns complete response", res.ok());
            if (!res.ok()) continue;
            auto headers = res.json()["headers"];
            bool admitted = headers.value("x-neutral", "") == "retained";
            for (auto name : credentialHeaders) {
                string lower(name);
                transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
                admitted = admitted && (target == "/echo" ? headers.contains(lower) : !headers.contains(lower));
            }
            check(target == "/echo" ? "same origin keeps all credential headers" : "different origin drops all credentials, keeps neutral", admitted);
        }
        // All listed names must be canonicalized for serialization and both consumers.
        for (auto name : credentialHeaders) {
            string mixed(name);
            for (size_t i = 0; i < mixed.size(); ++i)
                mixed[i] = i % 2 ? std::tolower(mixed[i]) : std::toupper(mixed[i]);
            for (auto spelling : {mixed + " ", mixed + "\t", " " + mixed, "\t" + mixed + " \t"}) {
                tcx::HttpClient variants;
                variants.setBaseUrl(plainUrl);
                variants.addHeader(spelling, "variant-secret");
                for (const auto& target : {string("/echo"), otherUrl + "/echo"}) {
                    res = variants.get(to(target));
                    const auto echoed = res.json().value("headers", nlohmann::json::object());
                    string lower(name);
                    transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
                    check("canonical credential header on same/different origin: " + spelling,
                          res.ok() && (target == "/echo" ? echoed.value(lower, "") == "variant-secret" : !echoed.contains(lower)));
                }
            }
        }
        for (const int code : {301, 302, 303, 307, 308}) {
            res = redirects.postRaw(to("/echo", code), "post-payload");
            const bool keep = code == 307 || code == 308;
            auto value = res.json();
            check("redirect POST method and body rule " + to_string(code), res.ok() &&
                  value["method"] == (keep ? "POST" : "GET") && value["body"] == (keep ? "post-payload" : ""));
        }
        res = redirects.put(to("/echo", 303), "put-payload");
        check("303 PUT becomes GET without body", res.ok() && res.json()["method"] == "GET" && res.json()["body"] == "");
        res = redirects.request("HEAD", to("/echo", 303));
        check("303 keeps HEAD", res.statusCode == 200 && res.body.empty() && res.error.empty());
        for (int code : {303, 307}) {
            res = redirects.uploadFile(to("/echo", code), uploadPath.string());
            auto value = res.json();
            check("multipart redirect method and body rule " + to_string(code), res.ok() &&
                  value["method"] == (code == 307 ? "POST" : "GET") &&
                  (code == 307 ? value["body"].get<string>().find("hello") != string::npos : value["body"] == ""));
        }
        res = redirects.get("/hop/5");
        check("five redirects succeed", res.ok() && res.json()["method"] == "GET");
        const auto countBefore = responseLossCountForTests(ResponseLoss::Redirects);
        size_t countWarnings = 0;
        auto countListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Warning && event.message.find("redirect count") != string::npos) ++countWarnings;
        });
        res = redirects.get("/hop/6");
        check("sixth redirect rejects whole response", wholeFailure(res) &&
              responseLossCountForTests(ResponseLoss::Redirects) == countBefore + 1);
        tcx::HttpClient localFile;
        res = localFile.get(fixtureFileUrl);
        check("app chosen initial file URL still works", res.error.empty() && res.body == readFile(uploadPath));

        using namespace tcx::curl::detail;
        size_t bodyWarnings = 0, metadataWarnings = 0;
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level != tc::LogLevel::Warning) return;
            if (event.message.find("response body") != string::npos) ++bodyWarnings;
            if (event.message.find("response metadata") != string::npos) ++metadataWarnings;
        });
        const size_t beforeBody = responseLossCountForTests(ResponseLoss::Body);
        const size_t beforeMeta = responseLossCountForTests(ResponseLoss::Metadata);
        redirects.setResponseLimit(4);
        res = redirects.get("/budget/4");
        check("redirect 4+4 exceeds cumulative body budget 4", wholeFailure(res) &&
              responseLossCountForTests(ResponseLoss::Body) == beforeBody + 1 && bodyWarnings == 1);
        res = redirects.get("/budget/2");
        check("redirect 2+2 fits budget and publishes only final bytes", res.ok() && res.body == "bb");
        res = redirects.get("/budget/4");
        check("cumulative redirect body rejection counted, Warning once",
              wholeFailure(res) && responseLossCountForTests(ResponseLoss::Body) == beforeBody + 2 && bodyWarnings == 1);
        redirects.setResponseLimit(responseByteLimit);
        auto release = [](tcx::curl::HttpResponse& value) {
            string().swap(value.body);
            string().swap(value.error);
            value.statusCode = 0;
        };
#ifdef __linux__
        responseMemoryTrace::begin();
#endif
        res = http.get("/complete");
#ifdef __linux__
        responseMemoryTrace::end();
        check("response C++ peak allocation budget", responseMemoryTrace::peak.load() <= 128 * 1024 * 1024 + 1024 * 1024);
#endif
        check("complete boundary response", res.ok() && res.body.size() == responseByteLimit &&
              res.body.front() == 'a' && res.body.back() == 'a');
        release(res);
#ifdef __linux__
        check("complete response storage released", responseMemoryTrace::live.load() == 0);
#endif
        for (const char* route : {"/excess", "/chunks", "/unknown", "/compressed"}) {
#ifdef __linux__
            responseMemoryTrace::begin();
#endif
            res = http.get(route);
#ifdef __linux__
            responseMemoryTrace::end();
            check("failed response C++ peak allocation budget", responseMemoryTrace::peak.load() <= responseByteLimit + 1024 * 1024);
#endif
            check(string("response admission ") + route,
                  !res.ok() && res.statusCode == 0 && res.body.empty() && !res.error.empty());
            release(res);
#ifdef __linux__
            check("failed response storage released", responseMemoryTrace::live.load() == 0);
#endif
            res = http.get("/");
            check("fresh request after rejection", res.ok() && res.body == "ok");
        }
        up = http.uploadFile("/excess", uploadPath.string());
        check("upload reply admission", !up.ok() && up.statusCode == 0 && up.body.empty());
        check("cumulative body rejection count", responseLossCountForTests(ResponseLoss::Body) == beforeBody +
                  ((curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_LIBZ) ? 7 : 6));
        res = http.get("/short");
        check("incomplete response is not published", !res.ok() && res.body.empty());
        res = http.get("/metadata-complete");
        check("complete metadata boundary", res.ok() && res.body == "ok");
        res = http.get("/metadata-excess");
        check("metadata boundary admission", !res.ok() && res.body.empty());
        res = http.get("/metadata");
        check("metadata admission", !res.ok() && res.body.empty() &&
              responseLossCountForTests(ResponseLoss::Metadata) == beforeMeta + 2);
        check("one Warning per rejecting store", bodyWarnings == 1 && metadataWarnings == 1);
        http.setFollowRedirects(true);
        res = http.get("/redirect");
        check("redirect releases earlier response", res.ok() && res.body == "ok");
        res = http.get("/redirect-loop");
        check("redirect admission releases partial responses", !res.ok() && res.body.empty() &&
              responseLossCountForTests(ResponseLoss::Metadata) == beforeMeta + 2 && metadataWarnings == 1 &&
              responseLossCountForTests(ResponseLoss::Redirects) == countBefore + 2 && countWarnings == 1);
        redirects.setResponseLimit(2);
        res = redirects.get(to("/"));
        check("intermediate redirect body is admitted before follow", wholeFailure(res));
        redirects.setResponseLimit(responseByteLimit);
        res = redirects.get("/metadata-small");
        check("each individual metadata block fits", res.ok());
        res = redirects.get(to("/metadata-small", 302) + "&pad=1");
        check("metadata admission is cumulative across hops", wholeFailure(res));
        http.setFollowRedirects(false);
        http.setTimeout(1);
        res = http.get("/cancel");
        check("cancelled response is not published", !res.ok() && res.body.empty() && !res.error.empty());
        http.setTimeout(10);
        res = http.get("/");
        check("retry after cancellation", res.ok() && res.body == "ok");
        if (curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_LIBZ) {
            res = http.get("/compressed-complete");
            check("complete decoded boundary", res.ok() && res.body.size() == responseByteLimit);
        }
        release(res);

        // Backend header ceilings must report even before HEADERFUNCTION runs.
        const size_t backendBefore = responseLossCountForTests(ResponseLoss::BackendHeader);
        size_t backendWarnings = 0;
        auto backendListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Warning && event.message.find("backend header limit") != string::npos) ++backendWarnings;
        });
        for (int i = 0; i < 2; ++i) {
            res = http.get("/backend-header");
            check("libcurl header ceiling rejects whole response", res.statusCode == 0 && res.body.empty() && !res.error.empty());
        }
        res = http.get("/backend-status");
        check("backend status-line ceiling rejects before first header callback", res.statusCode == 0 && res.body.empty() && !res.error.empty());
        check("backend header counter and first Warning", responseLossCountForTests(ResponseLoss::BackendHeader) == backendBefore + 3 && backendWarnings == 1);
        for (const char* route : {"/trailer/short", "/trailer/long"}) {
            res = http.get(route);
            check("status-like trailer never publishes mutilated success", res.statusCode == 0 && res.body.empty() && !res.error.empty());
        }
        res = http.get("/trailer/normal");
        check("normal trailer preserves complete body", res.ok() && res.body == "body");
        res = http.get("/interim");
        check("interim status starts genuine next response", res.ok() && res.body == "ok");
        http.setResponseLimit(1);
        for (const char* route : {"/head", "/not-modified", "/no-content"}) {
            res = http.request(string(route) == "/head" ? "HEAD" : "GET", route);
            check("representation length does not limit bodyless response", res.statusCode == (string(route) == "/head" ? 200 : string(route) == "/not-modified" ? 304 : 204) && res.body.empty() && res.error.empty());
        }
        res = http.get("/");
        check("per-client lowered limit rejects complete reply", res.statusCode == 0 && res.body.empty() && !res.error.empty());
        up = http.uploadFile("/", uploadPath.string());
        check("setter also limits upload replies", up.statusCode == 0 && up.body.empty() && !up.error.empty());
        size_t invalidWarnings = 0;
        auto invalidListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Warning && event.message.find("setResponseLimit") != string::npos) ++invalidWarnings;
        });
        http.setResponseLimit(0);
        http.setResponseLimit(0);
        res = http.get("/");
        check("zero keeps previous response limit with one Warning", res.statusCode == 0 && invalidWarnings == 1);
        http.setResponseLimit(2);
        res = http.get("/");
        check("raised response limit admits exact reply", res.ok() && res.body == "ok");
        tcx::HttpClient independent;
        independent.setBaseUrl("https://127.0.0.1:" + to_string(port));
        independent.setTlsCACertificate(serverPem);
        res = independent.get("/");
        check("response limit belongs to each client", res.ok() && res.body == "ok");
        http.setResponseLimit(responseByteLimit + 1);
        res = http.get("/excess");
        check("raised limit above default admits complete response", res.ok() && res.body.size() == responseByteLimit + 1 && res.body.back() == 'a');
        release(res);
        http.setResponseLimit(responseByteLimit);

        if (curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_LIBZ) {
            http.setResponseLimit(30);
            res = http.get("/wire-budget-final");
            check("one empty gzip reply fits wire budget", res.ok() && res.body.empty());
            const auto wireBefore = responseLossCountForTests(ResponseLoss::Body);
            http.setFollowRedirects(true);
            res = http.get("/wire-budget");
            check("wire budget is cumulative even with empty decoded hop bodies", wholeFailure(res) &&
                  responseLossCountForTests(ResponseLoss::Body) == wireBefore + 1);
            http.setResponseLimit(40);
            res = http.get("/wire-budget");
            check("exact cumulative wire budget succeeds", res.ok() && res.body.empty());
            http.setResponseLimit(responseByteLimit);
        }

        http.setBaseUrl("https://localhost:" + to_string(port));
        res = http.get("/");
        check("trusted certificate with a different host name: get() fails",
              !res.error.empty() && res.statusCode == 0, res.error);
        http.setBaseUrl("https://127.0.0.1:" + to_string(port));

        http.setTlsCACertificate(otherPem);
        res = http.get("/");
        check("another certificate as CA: get() fails", !res.error.empty() && res.statusCode == 0,
              res.error);
        up = http.uploadFile("/", uploadPath.string());
        check("another certificate as CA: uploadFile() fails",
              !up.error.empty() && up.statusCode == 0, up.error);

        http.setTlsCACertificate("not a certificate");
        res = http.get("/");
        check("text that is not a PEM as CA: get() fails",
              !res.error.empty() && res.statusCode == 0, res.error);
        up = http.uploadFile("/", uploadPath.string());
        check("text that is not a PEM as CA: uploadFile() fails",
              !up.error.empty() && up.statusCode == 0, up.error);
    }

    http.setTlsCACertificate("");
    res = http.get("/");
    check("empty CA (OS default store): get() fails", !res.error.empty() && res.statusCode == 0,
          res.error);
    up = http.uploadFile("/", uploadPath.string());
    check("empty CA (OS default store): uploadFile() fails",
          !up.error.empty() && up.statusCode == 0, up.error);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("tcxCurl tests\n");
    testTlsOptions();
    testHeaderNamesAndZones();

    if (run(string("openssl version > ") + kNull + " 2>&1") != 0) {
        printf("  [SKIP] no openssl command on PATH: HTTPS handshake checks skipped\n");
        return g_failures ? 1 : 0;
    }
    string python;
    for (const char* name : {"python3", "python"}) {
        if (run(string(name) + " -c \"import ssl\" > " + kNull + " 2>&1") == 0) {
            python = name;
            break;
        }
    }
    if (python.empty()) {
        printf("  [SKIP] no Python with ssl on PATH: HTTPS handshake checks skipped\n");
        return g_failures ? 1 : 0;
    }

    fs::path dir = fs::temp_directory_path() /
                   ("tcxcurl-tests-" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    testCaCertificate(dir, python);
    std::error_code ec;
    fs::remove_all(dir, ec);

    printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
