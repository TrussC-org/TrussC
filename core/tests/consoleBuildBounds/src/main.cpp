#include <TrussC.h>
#include "tc/app/internal/tcBuildOutputCapture.h"
#include "tc/app/internal/tcHotReloadScan.h"
#include "tc/app/internal/tcWindowsBuildOutput.h"
#include "tc/app/internal/tcPosixBuildOutput.h"
#include "../../common/tcCoreTest.h"
#include <sstream>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <chrono>
#include <thread>

namespace {
using namespace trussc;
int failures = 0;
size_t largestAllocation = 0;
void check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
void* trackedAllocation(void* pointer, size_t bytes) {
    largestAllocation = (std::max)(largestAllocation, bytes);
    return std::realloc(pointer, bytes);
}
void* failedAllocation(void*, size_t bytes) {
    largestAllocation = (std::max)(largestAllocation, bytes);
    return nullptr;
}

// Generates bytes on demand, including an unterminated final line; the test
// itself does not allocate the input, and checks rejection before EOF.
class RepeatedInput : public std::streambuf {
public:
    explicit RepeatedInput(size_t count) : remaining_(count) {}
    size_t rejected = 0;
private:
    size_t remaining_;
    char byte_ = 'x';
    int_type underflow() override {
        if (!remaining_) {
            check(rejected == 1, "unterminated line rejected before EOF");
            return traits_type::eof();
        }
        --remaining_;
        setg(&byte_, &byte_, &byte_ + 1);
        return traits_type::to_int_type(byte_);
    }
};

void linesAndQueue() {
    using Channel = console::internal::CommandChannel;
    RepeatedInput generated(Channel::lineBytes * 3);
    std::istream flood(&generated);
    largestAllocation = 0;
    internal::readBoundedLines(flood, Channel::lineBytes, SIZE_MAX,
        [](std::string_view) { check(false, "no rejected prefix delivered"); },
        [&](bool) { ++generated.rejected; }, trackedAllocation);
    check(generated.rejected == 1 && largestAllocation <= Channel::lineBytes,
          "line storage admitted before bounded allocation");
    std::istringstream noStorage("hello\nnext\n");
    int rejected = 0;
    internal::readBoundedLines(noStorage, Channel::lineBytes, SIZE_MAX,
        [](std::string_view) { check(false, "allocation failure cannot deliver prefix"); },
        [&](bool) { ++rejected; }, failedAllocation);
    check(rejected == 2, "allocation failure drains and rejects each line");
    std::istringstream growing("key=value\nnext=value\nlast=value\n");
    int fileRejections = 0;
    largestAllocation = 0;
    internal::readBoundedLines(growing, 16, 24, [](std::string_view) {},
        [&](bool fileLimit) { if (fileLimit) ++fileRejections; }, trackedAllocation);
    check(fileRejections == 1 && largestAllocation <= 16, "growing file total checked while reading");

    auto& channel = console::internal::getChannel();
    channel.clear();
    console::internal::isRunning().store(true);
    const auto beforeLines = channel.rejectedLines.load();
    std::string exact(Channel::lineBytes, 'a');
    std::istringstream input(exact + "\n" + exact + "b\nnext café\r\n");
    console::internal::readLines(input);
    auto result = channel.receiveAll();
    check(result.size() == 2 && result[0].raw == exact && result[1].raw == "next café" &&
          result[1].args.size() == 2, "whole line boundary, recovery, CRLF and UTF-8");
    check(channel.rejectedLines.load() == beforeLines + 1, "line rejection counted once");
    const auto beforeDrops = channel.droppedCommands.load();
    for (size_t index = 0; index < Channel::commandCount; ++index) check(channel.sendRaw("queued"), "queue exact count");
    check(!channel.sendRaw("excess") && !channel.sendRaw("excess"), "queue drops whole new commands without waiting");
    check(channel.size() == Channel::commandCount && channel.droppedCommands.load() == beforeDrops + 2,
          "queue count and cumulative loss");
    channel.clear();
    for (size_t index = 0; index < Channel::queuedBytes / exact.size(); ++index)
        check(channel.sendRaw(exact), "queue exact bytes");
    largestAllocation = 0;
    check(!channel.sendRaw("excess", trackedAllocation) && largestAllocation == 0,
          "queue byte admission precedes allocation");
    check(channel.bytes() == Channel::queuedBytes, "queued bytes bounded");
    channel.clear();
    check(!channel.sendRaw("allocation", failedAllocation) && channel.empty(), "failed queue allocation is clean");
    check(channel.sendRaw("after"), "queue recovers after rejection");
    result = channel.receiveAll();
    check(result.size() == 1 && result[0].args == std::vector<std::string>{"after"} && channel.bytes() == 0,
          "whole command and credit release");
    console::internal::isRunning().store(false);
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("[Console]") == 0) ++warnings;
    });
    // Model an existing reader after stop(), without starting a real thread.
    console::internal::spawned().store(true);
    console::internal::processQueue();
    console::internal::processQueue();
    console::internal::spawned().store(false);
    check(warnings == 2, "one warning per stdin store, automatically pumped while stopped");
    // Accumulated counters are not reset by warning delivery or querying.
    check(channel.rejectedLines.load() == beforeLines + 1 && channel.droppedCommands.load() == beforeDrops + 4,
          "warning delivery preserves cumulative stdin totals");
    channel.clear();
    channel.rejectLine(); channel.sendRaw(std::string(Channel::lineBytes + 1, 'x'));
    channel.sendRaw("dropped", +[](void*, size_t) -> void* { return nullptr; });
    channel.reportLoss(); channel.reportLoss();
    check(warnings == 4, "drained stdin stores re-arm once per new overflow episode");
    channel.clear(); channel.reportLoss();
    check(warnings == 4, "historical losses do not warn after drain");
}

void capture() {
    using internal::BuildOutputCapture;
    const size_t edge = BuildOutputCapture::endBytes;
    std::string first(edge, 'h'), middle(edge, 'm'), last(edge, 't');
    BuildOutputCapture output(trackedAllocation);
    largestAllocation = 0;
    output.append(first.data(), first.size());
    output.append(middle.data(), middle.size());
    check(output.omittedBytes() == 0 && output.text() == first + middle, "capture exact bound complete");
    output.append(last.data(), 1);
    check(output.omittedBytes() == 1, "capture bound plus one counted");
    output.append(last.data() + 1, last.size() - 1);
    const auto text = output.text();
    check(text.substr(0, edge) == first && text.substr(text.size() - edge) == last &&
          text.find("65536 bytes omitted") != std::string::npos, "capture retains first and last bytes with truthful marker");
    check(output.omittedBytes() == edge && largestAllocation <= edge, "capture allocation sizes bounded");
    std::string pattern(edge * 5 + 71, ' ');
    for (size_t index = 0; index < pattern.size(); ++index) pattern[index] = static_cast<char>('!' + index % 89);
    BuildOutputCapture chunked(trackedAllocation);
    const size_t chunks[] = {7, 8191, 513, 4096, 65537};
    for (size_t offset = 0, turn = 0; offset < pattern.size(); ++turn) {
        const size_t count = (std::min)(chunks[turn % 5], pattern.size() - offset);
        chunked.append(pattern.data() + offset, count);
        offset += count;
    }
    const auto chunkedText = chunked.text();
    check(chunkedText.substr(0, edge) == pattern.substr(0, edge) &&
          chunkedText.substr(chunkedText.size() - edge) == pattern.substr(pattern.size() - edge) &&
          chunked.omittedBytes() == pattern.size() - edge * 2,
          "tail ring preserves byte order across wrap and arbitrary read boundaries");
    for (const std::string character : {std::string("¢"), std::string("€"), std::string("𐍈")}) {
        // Euro text crosses both cuts; an ASCII prefix also splits two-/four-byte head characters.
        std::string unicode = character.size() == 3 ? "" : "x";
        for (size_t i = 0; i < edge; ++i) unicode += character;
        BuildOutputCapture utf8;
        for (size_t offset = 0; offset < unicode.size();) {
            const size_t count = (std::min)(size_t{513}, unicode.size() - offset);
            utf8.append(unicode.data() + offset, count); offset += count;
        }
        size_t head = edge, tail = unicode.size() - edge;
        while ((static_cast<unsigned char>(unicode[head]) & 0xc0) == 0x80) --head;
        while ((static_cast<unsigned char>(unicode[tail]) & 0xc0) == 0x80) ++tail;
        const size_t omitted = tail - head;
        check(utf8.omittedBytes() == omitted && utf8.text() == unicode.substr(0, head) +
              "\n… " + std::to_string(omitted) + " bytes omitted …\n" + unicode.substr(tail),
              "UTF-8 head and tail cuts preserve complete characters and count extra omitted bytes");
    }
    internal::BuildOutputLoss loss;
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning) ++warnings;
    });
    loss.record(1); loss.record(3);
    check(warnings == 1, "one warning across repeated capture loss");
    check(loss.truncatedBuilds == 2 && loss.omittedBytes == 4 && loss.warned, "capture totals survive queries and repeated builds");
    internal::BuildOutputLoss forwardingLoss;
    forwardingLoss.recordForwardingFailure(); forwardingLoss.recordForwardingFailure();
    check(warnings == 2 && forwardingLoss.forwardingFailures == 2 && forwardingLoss.warned,
          "forwarding failures counted with one warning per output store");
    BuildOutputCapture unavailable(failedAllocation);
    unavailable.append(first.data(), first.size());
    unavailable.append(last.data(), last.size());
    check(unavailable.omittedBytes() == edge * 2 && unavailable.text().find("131072 bytes omitted") != std::string::npos,
          "capture allocation failure counts bytes and returns explicit omission");
}

#ifndef _WIN32
void closedForwarding(std::vector<std::string> arguments) {
    arguments.back() = "--closed-output-child";
    for (int scenario = 0; scenario < 6; ++scenario) {
        // Isolate descriptor/signal changes from the test runner. The first
        // case is the review shape: closed stdout, 8192 bytes, default SIGPIPE.
        const pid_t host = fork();
        if (!host) {
            const bool stderrOnly = scenario == 1;
            const bool both = scenario == 2;
            const bool blocked = scenario == 3 || scenario == 4;
            const bool alreadyPending = scenario == 4;
            const bool ignored = scenario == 5;
            arguments.emplace_back(both ? "both" : stderrOnly ? "stderr" : "stdout");
            struct sigaction disposition = {};
            disposition.sa_handler = ignored ? SIG_IGN : SIG_DFL;
            sigemptyset(&disposition.sa_mask);
            if (sigaction(SIGPIPE, &disposition, nullptr)) _exit(10);
            sigset_t pipeSignal, maskBefore;
            sigemptyset(&pipeSignal); sigaddset(&pipeSignal, SIGPIPE);
            if (pthread_sigmask(blocked ? SIG_BLOCK : SIG_UNBLOCK, &pipeSignal, nullptr)) _exit(11);
            if (alreadyPending && raise(SIGPIPE)) _exit(12);
            if (pthread_sigmask(SIG_BLOCK, nullptr, &maskBefore)) _exit(13);
            int destination[2];
            if (pipe(destination)) _exit(14);
            close(destination[0]);
            if (dup2(destination[1], stderrOnly ? STDERR_FILENO : STDOUT_FILENO) < 0 ||
                (both && dup2(destination[1], STDERR_FILENO) < 0)) _exit(15);
            close(destination[1]);
            auto& loss = internal::buildOutputLoss();
            loss = {};
            int warnings = 0;
            auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
                if (event.level == LogLevel::Warning && event.message.find("forwarding") != std::string::npos)
                    ++warnings;
            });
            const std::string expected = both ? std::string(8192, 'x') + '\n' + std::string(8192, 'x')
                                              : std::string(8192, 'x') + (stderrOnly ? "" : "\n");
            for (int build = 0; build < 2; ++build) {
                std::string output;
                if (internal::runPosixBuildWithOutput(arguments, output) != 7 || output != expected) _exit(16);
                struct sigaction after = {};
                sigset_t maskAfter, pending;
                if (sigaction(SIGPIPE, nullptr, &after) || after.sa_handler != disposition.sa_handler ||
                    pthread_sigmask(SIG_BLOCK, nullptr, &maskAfter) || sigpending(&pending)) _exit(17);
                for (int signal = 1; signal < NSIG; ++signal)
                    if (sigismember(&maskBefore, signal) != sigismember(&maskAfter, signal)) _exit(18);
                if ((sigismember(&pending, SIGPIPE) == 1) != alreadyPending) _exit(19);
            }
            _exit(loss.forwardingFailures == 2 && warnings == 1 ? 0 : 20);
        }
        int status = 0;
        pid_t waited;
        do { waited = host > 0 ? waitpid(host, &status, 0) : -1; } while (waited < 0 && errno == EINTR);
        check(waited == host && host > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "closed forwarding preserves host, child status, diagnostics, signal state and one warning");
    }
}
#else
void closedForwarding(std::vector<std::string> arguments) {
    arguments.back() = "--closed-output-child";
    for (int scenario = 0; scenario < 3; ++scenario) {
        HANDLE reader = nullptr, writer = nullptr;
        if (!CreatePipe(&reader, &writer, nullptr, 0)) {
            check(false, "closed forwarding destination created"); continue;
        }
        CloseHandle(reader);
        const DWORD streams[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        const HANDLE previous[] = {GetStdHandle(streams[0]), GetStdHandle(streams[1])};
        bool redirected = true;
        for (int stream = 0; stream < 2; ++stream)
            if (scenario == 2 || scenario == stream)
                redirected = SetStdHandle(streams[stream], writer) != 0 && redirected;
        std::string command;
        for (const auto& argument : arguments) command += '"' + argument + "\" ";
        command += scenario == 2 ? "both" : scenario == 1 ? "stderr" : "stdout";
        auto& loss = internal::buildOutputLoss();
        loss = {};
        int warnings = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Warning && event.message.find("forwarding") != std::string::npos)
                ++warnings;
        });
        const std::string expected = scenario == 2 ? std::string(8192, 'x') + '\n' + std::string(8192, 'x')
                                                   : std::string(8192, 'x') + (scenario == 1 ? "" : "\n");
        bool survived = redirected;
        if (redirected) for (int build = 0; build < 2; ++build) {
            std::string output;
            const int status = internal::runWindowsBuildWithOutput(command, output);
            survived = status == 7 && output == expected && survived;
        }
        for (int stream = 0; stream < 2; ++stream) SetStdHandle(streams[stream], previous[stream]);
        CloseHandle(writer);
        check(survived && loss.forwardingFailures == 2 && warnings == 1,
              "closed Windows forwarding preserves host, child status, diagnostics and one warning");
    }
}
#endif

void scans() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("tc-console-build-bounds-" + std::to_string(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        getpid()
#endif
    ));
    fs::remove_all(root);
    fs::create_directories(root);
    internal::HotReloadScanLoss loss;
    std::ofstream(root / "one.CPP") << "// source\n";
    std::ofstream(root / "two.hpp") << "// header\n";
    std::ofstream(root / "ignored.txt") << "text\n";
    auto deep = root;
    for (int index = 0; index < 66; ++index) { deep /= "d"; fs::create_directory(deep); }
    std::ofstream(deep / "deep.cpp") << "// deep\n";
    auto sources = internal::hotReloadSources(root, loss);
    check(sources.size() == 2 && loss.skippedEntries == 1, "source depth skipped with counter, valid files retained");
    check(internal::hotReloadSources(root / "missing", loss).empty() && loss.incompleteScans == 1,
          "unreadable source scan does not throw");
    const auto cache = root / "CMakeCache.txt";
    std::ofstream(cache, std::ios::binary) << "CMAKE_COMMAND:INTERNAL=/example/cmake\r\n";
    check(internal::hotReloadCMakeCommand(cache, loss) == "/example/cmake", "cache normal command CRLF");
    { std::ofstream file(cache, std::ios::binary); file << "CMAKE_COMMAND:INTERNAL=/prefix\n";
      const std::string block(4096, 'x'); for (int index = 0; index < 257; ++index) file << block; }
    check(internal::hotReloadCMakeCommand(cache, loss).empty() && loss.rejectedCaches == 1,
          "overlong cache line never publishes earlier command");
    fs::resize_file(cache, 16 * 1024 * 1024 + 1);
    check(internal::hotReloadCMakeCommand(cache, loss).empty() && loss.rejectedCaches == 2,
          "oversized cache rejected before reading");
    fs::create_directory(root / "cache-directory");
    check(internal::hotReloadCMakeCommand(root / "cache-directory", loss).empty() && loss.rejectedCaches == 3,
          "non-file cache entry is skipped and counted");
    // Generate the inventory in-test. No compiler, downloader or other tool is needed.
    const auto inventory = root / "inventory";
    fs::create_directory(inventory);
    for (size_t index = 0; index < 65537; ++index)
        std::ofstream(inventory / (std::to_string(index) + ".cpp"));
    internal::HotReloadScanLoss countLoss;
    sources = internal::hotReloadSources(inventory, countLoss);
    check(sources.size() == 65536 && countLoss.incompleteScans == 1, "source entry boundary and explicit incomplete scan");
#ifndef _WIN32
    // Alternate spellings keep this aggregate-storage probe independent of
    // Windows long-path configuration; the shared admission code runs above.
    auto longRoot = inventory;
    for (int index = 0; index < 200; ++index) longRoot /= ".";
    internal::HotReloadScanLoss nameLoss;
    sources = internal::hotReloadSources(longRoot, nameLoss);
    size_t nameBytes = 0;
    for (const auto& path : sources) nameBytes += (path.native().size() + 1) * sizeof(fs::path::value_type);
    check(sources.size() < 65536 && nameBytes <= 16 * 1024 * 1024 && nameLoss.skippedEntries > 0,
          "source aggregate path storage stays bounded");
#endif
    fs::remove_all(root);
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 2 && std::string_view(argv[1]) == "--closed-output-child") {
        const std::string bytes(8192, 'x');
        const std::string_view stream = argv[2];
        for (int descriptor = 1; descriptor <= 2; ++descriptor) {
            if (stream != "both" && (descriptor == 1) != (stream == "stdout")) continue;
#ifdef _WIN32
            DWORD written = 0;
            if (!WriteFile(GetStdHandle(descriptor == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE),
                           bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size()) return 8;
#else
            size_t offset = 0;
            while (offset < bytes.size()) {
                const ssize_t count = write(descriptor, bytes.data() + offset, bytes.size() - offset);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) return 8;
                offset += static_cast<size_t>(count);
            }
#endif
        }
        return 7;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--silent-descendant") {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--inherited-writer-child") {
#ifdef _WIN32
        std::string command = "\"" + std::string(argv[0]) + "\" ";
#ifdef TC_CORE_TEST_NAME
        command += std::string(TC_CORE_TEST_NAME) + " ";
#endif
        command += "--silent-descendant";
        STARTUPINFOA si = {}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process = {};
        if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &process)) return 8;
        CloseHandle(process.hProcess); CloseHandle(process.hThread);
#else
        const pid_t descendant = fork();
        if (descendant < 0) return 8;
        if (!descendant) { sleep(3); _exit(0); }
#endif
        return 7;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--output-child") {
#ifdef _WIN32
        // Binary mode: CRLF translation would change the exact byte counts below.
        _setmode(_fileno(stdout), _O_BINARY); _setmode(_fileno(stderr), _O_BINARY);
#endif
        const std::string block(4096, 'b');
        std::fputs("stdout head\n", stdout); std::fputs("stderr head\n", stderr);
        for (int index = 0; index < 80; ++index) {
            std::fwrite(block.data(), 1, block.size(), stdout);
            std::fwrite(block.data(), 1, block.size(), stderr);
        }
        std::fputs("\nstdout tail\n", stdout); std::fputs("\nstderr tail\n", stderr);
        return 7;
    }
    linesAndQueue(); capture(); scans();
    std::string output;
    std::vector<std::string> arguments{argv[0]};
#ifdef TC_CORE_TEST_NAME
    arguments.emplace_back(TC_CORE_TEST_NAME);
#endif
    arguments.emplace_back("--output-child");
#ifdef _WIN32
    std::string command;
    for (const auto& argument : arguments) { if (!command.empty()) command += ' '; command += '"' + argument + '"'; }
    const int result = internal::runWindowsBuildWithOutput(command, output);
#else
    const int result = internal::runPosixBuildWithOutput(arguments, output);
#endif
    check(result == 7 && output.find("stdout head") != std::string::npos && output.find("stderr head") != std::string::npos &&
          output.find("stdout tail") != std::string::npos && output.find("stderr tail") != std::string::npos &&
          output.find("bytes omitted") != std::string::npos && output.size() < 4 * internal::BuildOutputCapture::endBytes + 200,
          "real child drains both streams, preserves ends and exit code");
    check(internal::buildOutputLoss().truncatedBuilds == 1 && internal::buildOutputLoss().omittedBytes == 393266,
          "real child omitted-byte count exact");
    arguments.back() = "--inherited-writer-child";
    const auto started = std::chrono::steady_clock::now();
#ifdef _WIN32
    command.clear();
    for (const auto& argument : arguments) { if (!command.empty()) command += ' '; command += '"' + argument + '"'; }
    const int inheritedResult = internal::runWindowsBuildWithOutput(command, output);
#else
    const int inheritedResult = internal::runPosixBuildWithOutput(arguments, output);
#endif
    check(inheritedResult == 7 && std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1500),
          "direct child status returns promptly while a silent descendant retains writers");
    closedForwarding(arguments);
    std::printf("consoleBuildBounds: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
