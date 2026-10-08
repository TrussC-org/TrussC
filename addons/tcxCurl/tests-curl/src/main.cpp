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
};
static TlsOptionsTrace g_tlsTrace;

template<class T>
static CURLcode testCurlSetopt(CURL* handle, CURLoption option, T value) {
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
    if (!g_tlsTrace.enabled) return curl_easy_perform(handle);
    g_tlsTrace.performed = true;
    return CURLE_ABORTED_BY_CALLBACK;
}

#undef curl_easy_setopt
#define curl_easy_setopt testCurlSetopt
#define curl_easy_perform testCurlPerform
#include <tcxCurl.h>
#undef curl_easy_setopt
#undef curl_easy_perform

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
import http.server, socketserver, ssl

class Handler(http.server.BaseHTTPRequestHandler):
    def answer(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n:
            self.rfile.read(n)
        body = b"ok"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    do_GET = answer
    do_POST = answer
    def log_message(self, *args):
        pass

class LoopbackServer(http.server.HTTPServer):
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
with open(port_file + ".tmp", "w") as f:
    f.write(str(server.server_address[1]))
os.replace(port_file + ".tmp", port_file)
server.serve_forever()
)PY";

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
