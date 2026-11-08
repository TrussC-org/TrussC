#include <TrussC.h>
#include "tc/app/internal/tcCommandLine.h"
#include "../../common/tcCoreTest.h"
#include "../../common/tcWindowTest.h"
#include "../../common/tcDisplayTest.h"
#include <sstream>
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("filesOpened: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void argumentTests() {
    const std::string bytes = std::string("app\0two words\0", 14) + "日本語.png" + '\0' + '\0';
    std::istringstream input(bytes);
    const auto args = internal::parseProcCommandLine(input);
    check("proc NUL parsing preserves spaces, UTF-8 and empty arguments",
        args == std::vector<std::string>{"app", "two words", "日本語.png", ""});
    check("missing proc input is empty", internal::parseProcCommandLine(input).empty());
    check("OS raw arguments include executable without main parameters", Platform::isDesktop() ? !getCommandLineArgs().empty() : getCommandLineArgs().empty());
    check("UTF-16 conversion preserves non-ASCII and surrogate pairs",
        internal::utf16ToUtf8Lossy<char16_t>(u"C:\\two words\\日本語\U0001f642.png") == "C:\\two words\\日本語🙂.png");
    const char16_t invalid[] = {0xd800, u'x', 0xdc00};
    check("invalid UTF-16 uses replacement characters",
        internal::utf16ToUtf8Lossy<char16_t>(std::u16string_view(invalid, 3)) == "�x�");

    const auto base = fs::current_path() / ".filesOpened-test";
    fs::create_directories(base / "folder");
    struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{base};
    for (const auto* name : {"two words.txt", "日本語.txt", "--foo"}) std::ofstream(base / utf8ToPath(name)).put('x');
    const auto previous = fs::current_path();
    struct Restore { fs::path path; ~Restore() { fs::current_path(path); } } restore{previous};
    fs::current_path(base);
    const auto files = internal::filesFromCommandLine({"two words.txt", "two words.txt", "日本語.txt",
        "folder", "--foo", "--missing", "missing.txt", "", "./--foo"});
    check("argv filtering skips executable, flags, nonexistent and empty paths",
        files == std::vector<fs::path>{base / "two words.txt", base / utf8ToPath("日本語.txt"),
            base / "folder", fs::absolute("./--foo")});
    check("empty fake argv is empty", internal::filesFromCommandLine({}).empty());
}
const fs::path launchFile = "launch-open-request.png";
struct LaunchProbe : App {
    EventListener first, second;
    int calls = 0;
    bool setupFinished = false;
    LaunchProbe() { internal::receiveFilesOpened({launchFile}); }
    void setup() override {
        check("launch request waits for setup", calls == 0);
        auto callback = [this](FilesOpenedEventArgs& e) {
            check("launch delivered after setup returns", setupFinished);
            check("process payload has paths", e.files == std::vector<fs::path>{launchFile});
            ++calls;
        };
        first = tc::runtime().events().filesOpened.listen(callback);
        second = tc::runtime().events().filesOpened.listen(callback);
        setupFinished = true;
    }
    void update() override {
        check("launch delivered once to both setup listeners before update", calls == 2);
        internal::finishFilesOpenedSetup(*this);
        check("setup completion does not redeliver launch", calls == 2);
        internal::receiveFilesOpened({launchFile});
        check("later requests reach both listeners", calls == 4);
        exitApp();
    }
};
struct WindowProbe : App {
    EventListener opened;
    int calls = 0;
    void setup() override {
        opened = tc::runtime().events().filesOpened.listen([this](FilesOpenedEventArgs&) {
            check("Runtime listener restores subscriber App context", getWindow() == internal::resolveWindow());
            ++calls;
        });
    }
};
void multipleWindows() {
    internal::beginRuntimeRun();
    tcCoreTest::WindowFixture a(101), b(102);
    auto one = internal::makeConstructedAppAt<WindowProbe>(&internal::WindowRequestAccess::context(*a.window));
    auto two = internal::makeConstructedAppAt<WindowProbe>(&internal::WindowRequestAccess::context(*b.window));
    for (auto pair : {std::make_pair(a.window, one), std::make_pair(b.window, two)}) {
        internal::WindowRequestAccess::attachCandidate(*pair.first, pair.second);
        internal::applyPendingAppForTests(*pair.first);
        internal::AppCallbackScope owner(internal::appOwner(*pair.second));
        internal::setupNodeOnce(*pair.second);
    }
    internal::receiveFilesOpened({"later.png"});
    check("listeners from different windows both receive process event", one->calls == 1 && two->calls == 1);
    internal::shutdownRuntime();
}
fs::path nativeExpected;
int nativeCalls = 0;
struct NativeLaunchProbe : App {
    EventListener opened;
    bool setupFinished = false;
    void setup() override {
        opened = tc::runtime().events().filesOpened.listen([this](FilesOpenedEventArgs& e) {
            check("native launch arrives after setup", setupFinished);
            check("native launch retains spaces and UTF-8", e.files == std::vector<fs::path>{nativeExpected});
            ++nativeCalls;
        });
        setupFinished = true;
    }
    void update() override {
        check("native launch delivered exactly once before update", nativeCalls == 1);
        exitApp(failures ? 1 : 0);
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--display-open") {
        nativeExpected = fs::absolute(utf8ToPath(argv[2]));
        WindowSettings settings;
        settings.setSize(100, 100);
        return tcCoreTest::runDisplayTest<NativeLaunchProbe>(settings, failures, [] {
            check("native launch listener ran", nativeCalls == 1);
        });
    }
    argumentTests();
    check("headless launch succeeds", runHeadlessApp<LaunchProbe>() == 0);
    multipleWindows();
    return failures ? 1 : 0;
}
