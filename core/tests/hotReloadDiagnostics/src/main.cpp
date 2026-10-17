#include "tc/app/internal/tcHotReloadDiagnostics.h"
#include "tc/app/internal/tcWindowsBuildOutput.h"
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <string>

namespace {
struct Sample {
    const char* name;
    const char* output;
    bool hint;
};
} // namespace

TC_CORE_TEST_MAIN() {
    const Sample samples[] = {
        {"Ninja guest failure followed by another parallel job's link",
         "[1/2] Linking CXX shared library guest.dll\n"
         "FAILED: guest.dll\n"
         "link.exe /out:guest.dll tcApp.obj\n"
         "tcApp.obj : error LNK2019: unresolved external symbol missing\n"
         "guest.dll : fatal error LNK1120: 1 unresolved externals\n"
         "[2/2] Linking CXX static library host_only.lib\n"
         "ninja: build stopped: subcommand failed.\n", true},
        {"MSBuild guest LNK2019 followed by another project's diagnostic",
         "1>tcApp.obj : error LNK2019: unresolved external symbol missing [C:\\app\\build\\guest.vcxproj]\n"
         "2>main.obj : warning LNK4098: defaultlib conflicts [C:\\app\\build\\app.vcxproj]\n", true},
        {"MSVC LNK2019 with guest DLL on the same line",
         "guest.dll : error LNK2019: unresolved external symbol hostOnly referenced in function setup\n", true},
        {"MSVC LNK2019 with trailing guest DLL summary",
         "tcApp.obj : error LNK2019: unresolved external symbol hostOnly referenced in function setup\n"
         "C:\\app\\bin\\guest.dll : fatal error LNK1120: 1 unresolved externals\n", true},
        {"MSVC LNK2001 with guest project",
         "tcApp.obj : error LNK2001: unresolved external symbol hostOnly [C:\\app\\build\\guest.vcxproj]\r\n", true},
        {"MSVC input library alone does not identify the target",
         "LINK : error LNK2001: unresolved external symbol hostOnly (guest.lib)\n", false},
        {"Ninja MSVC guest link step",
         "[4/5] Linking CXX shared library C:\\app\\bin\\guest.dll\n"
         "FAILED: C:/app/bin/guest.dll guest.lib\n"
         "cmd.exe /C link.exe /OUT:C:\\app\\bin\\guest.dll tcApp.obj\n"
         "tcApp.obj : error LNK2019: unresolved external symbol hostOnly referenced in function setup\n", true},
        {"lld-link guest link step",
         "[3/4] Linking CXX shared library bin/guest.dll\n"
         "lld-link: error: undefined symbol: hostOnly\n"
         ">>> referenced by tcApp.cpp:12\n", true},
        {"lld-link with guest command",
         "lld-link.exe /out:bin/guest.dll tcApp.obj\n"
         "lld-link: error: undefined symbol: hostOnly\n", true},
        {"case insensitive Windows artifacts",
         "TCAPP.OBJ : error LNK2001: unresolved external symbol hostOnly [GUEST.VCXPROJ]\n", true},
        {"compile error in guest",
         "tcApp.cpp(12): error C2065: 'missing': undeclared identifier [guest.vcxproj]\n", false},
        {"compile error with guest target in Ninja output",
         "FAILED: CMakeFiles/guest.dir/src/tcApp.cpp.obj\n"
         "tcApp.cpp(12): error C2065: 'missing': undeclared identifier\n", false},
        {"host link error",
         "[4/4] Linking CXX executable app.exe\n"
         "main.obj : error LNK2019: unresolved external symbol hostOnly referenced in function main\n"
         "app.exe : fatal error LNK1120: 1 unresolved externals\n", false},
        {"host project link error",
         "main.obj : error LNK2001: unresolved external symbol hostOnly [C:\\app\\app.vcxproj]\n", false},
        {"successful guest link before host failure",
         "[3/4] Linking CXX shared library bin/guest.dll\n"
         "[4/4] Linking CXX executable app.exe\n"
         "main.obj : error LNK2019: unresolved external symbol hostOnly referenced in function main\n", false},
        {"successful guest link before MSBuild host failure",
         "guest.vcxproj -> C:\\app\\bin\\guest.dll\n"
         "main.obj : error LNK2019: unresolved external symbol hostOnly [C:\\app\\app.vcxproj]\n", false},
        {"lld-link host failure after guest success",
         "[3/4] Linking CXX shared library bin/guest.dll\n"
         "lld-link.exe /out:bin/app.exe main.obj\n"
         "lld-link: error: undefined symbol: hostOnly\n", false},
        {"non-unresolved guest link error",
         "LINK : fatal error LNK1104: cannot open file 'guest.dll'\n", false},
        {"unresolved without a guest target",
         "tcApp.obj : error LNK2019: unresolved external symbol hostOnly referenced in function setup\n", false},
        {"unrelated DLL with guest in its name",
         "[4/4] Linking CXX shared library notguest.dll\n"
         "main.obj : error LNK2019: unresolved external symbol hostOnly referenced in function setup\n", false},
        {"successful guest build", "[4/4] Linking CXX shared library bin/guest.dll\n", false},
        {"empty output", "", false},
        {"bracketed input artifacts do not override output",
         "link.exe /out:app.exe [guest.dll]\n"
         "main.obj : error LNK2019: unresolved external symbol missing\n", false},
        {"MSBuild lld-link spaced diagnostic", "lld-link : error : undefined symbol: hostOnly [C:\\app\\build\\guest.vcxproj]\n", true},
        {"prefixed MSBuild lld-link spaced diagnostic", "1>Link:\n1>lld-link : error : undefined symbol: hostOnly [C:\\app\\build\\guest.vcxproj]\n", true},
        {"MSBuild lld-link command and spaced diagnostic", "C:\\LLVM\\bin\\lld-link.exe /OUT:C:\\app\\bin\\guest.dll tcApp.obj\nlld-link : error : undefined symbol: hostOnly [C:\\app\\build\\guest.vcxproj]\n", true},
        {"host link command containing guest input library", "[4/4] Linking CXX executable app.exe\nFAILED: app.exe\nlink.exe /out:app.exe main.obj C:\\vendor\\guest.lib\nmain.obj : error LNK2019: unresolved external symbol missing\napp.exe : fatal error LNK1120: 1 unresolved externals\n", false},
        {"host error from input library guest.lib", "guest.lib(loader.obj) : error LNK2019: unresolved external symbol missing [C:\\app\\build\\app.vcxproj]\n", false},
        {"unrelated guest artifact in status line", "[4/4] Linking CXX executable app.exe\n-- Copying guest.dll to distribution\nmain.obj : error LNK2019: unresolved external symbol missing\napp.exe : fatal error LNK1120: 1 unresolved externals\n", false},
        {"compile failure in LNK2019 reproduction directory", "C:\\bugs\\LNK2019\\src\\tcApp.cpp(12): error C2065: 'missing': undeclared identifier [C:\\bugs\\LNK2019\\build\\guest.vcxproj]\n", false},
        {"prefixed localized MSVC LNK2001 with trailing summary", "1>tcApp.obj : error LNK2001: 未解決の外部シンボル hostOnly\n1>C:\\app\\bin\\guest.dll : fatal error LNK1120: 1 件の未解決の外部参照\n", true},
        {"error before guest link step", "tcApp.obj : error LNK2001: unresolved external symbol missing\n[4/4] Linking CXX shared library guest.dll\n", true},


        {"missing include named LNK2001.h",
         "tcApp.cpp(12): fatal error C1083: Cannot open include file: 'LNK2001.h': No such file [guest.vcxproj]\n", false},
        {"LNK2019 in object filename",
         "C:\\app\\LNK2019.obj : error C2065: missing [guest.vcxproj]\n", false},
        {"host command containing guest DLL input",
         "link.exe /out:app.exe main.obj C:\\vendor\\guest.dll\n"
         "main.obj : error LNK2019: unresolved external symbol missing\n", false},
        {"guest archive member without target evidence",
         "guest.lib(loader.obj) : error LNK2001: unresolved external symbol missing\n", false},
        {"later non-guest output wins",
         "[3/4] Linking CXX shared library guest.dll\n"
         "link.exe /out:app.exe main.obj guest.lib\n"
         "main.obj : error LNK2019: unresolved external symbol missing\n", false},
        {"guest-owned LNK2019 stays confirmed after a later host error",
         "tcApp.obj : error LNK2019: unresolved external symbol missing [guest.vcxproj]\n"
         "guest.lib(loader.obj) : error LNK2001: unresolved external symbol missing [app.vcxproj]\n", true},
        {"host project error naming a guest archive member alone",
         "guest.lib(loader.obj) : error LNK2001: unresolved external symbol missing [app.vcxproj]\n", false},
        {"later non-guest summary wins",
         "[3/4] Linking CXX shared library guest.dll\n"
         "main.obj : error LNK2001: unresolved external symbol missing\n"
         "app.exe : fatal error LNK1120: 1 unresolved externals\n", false},
        {"quoted guest output path",
         "lld-link.exe /OUT:\"C:\\my app\\bin\\guest.dll\" tcApp.obj\n"
         "lld-link : error : undefined symbol: hostOnly\n", true},
        {"FAILED guest output alone",
         "FAILED: C:/app/bin/guest.dll guest.lib\n"
         "tcApp.obj : error LNK2019: unresolved external symbol missing\n", true},
        {"guest project overrides input archive name",
         "host.lib(loader.obj) : error LNK2019: unresolved external symbol missing [guest.vcxproj]\n", true},
    };
    int failures = 0;
    for (const auto& sample : samples) {
        const bool ok = trussc::internal::hasWindowsGuestLinkError(sample.output) == sample.hint;
        std::printf("%s: %s\n", sample.name, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    }
#ifdef _WIN32
    // Redirect the helper's destinations independently, as with >out 2>err.
    // More than one pipe buffer on each stream also checks concurrent draining.
    char tempDir[MAX_PATH] = {};
    char paths[2][MAX_PATH] = {};
    HANDLE files[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    bool ready = GetTempPathA(MAX_PATH, tempDir) != 0;
    for (int i = 0; i < 2 && ready; ++i) {
        ready = GetTempFileNameA(tempDir, "tch", 0, paths[i]) != 0;
        if (ready) {
            files[i] = CreateFileA(paths[i], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
            ready = files[i] != INVALID_HANDLE_VALUE;
        }
    }
    bool captured = false;
    bool destinations = false;
    if (ready) {
        const HANDLE originalOut = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE originalErr = GetStdHandle(STD_ERROR_HANDLE);
        SetStdHandle(STD_OUTPUT_HANDLE, files[0]);
        SetStdHandle(STD_ERROR_HANDLE, files[1]);
        std::string output;
        const int rc = trussc::internal::runWindowsBuildWithOutput(
            "cmd.exe /d /c \"echo Linking CXX shared library guest.dll & "
            "(for /L %i in (1,1,700) do @echo stdout-only) & "
            "echo lld-link : error : undefined symbol: hostOnly 1>&2 & "
            "(for /L %i in (1,1,700) do @echo stderr-only 1>&2) & exit /b 7\"", output);
        SetStdHandle(STD_OUTPUT_HANDLE, originalOut);
        SetStdHandle(STD_ERROR_HANDLE, originalErr);
        std::string forwarded[2];
        for (int i = 0; i < 2; ++i) {
            SetFilePointer(files[i], 0, nullptr, FILE_BEGIN);
            char buffer[4096];
            DWORD count = 0;
            while (ReadFile(files[i], buffer, sizeof(buffer), &count, nullptr) && count) {
                forwarded[i].append(buffer, count);
            }
        }
        captured = rc == 7 && trussc::internal::hasWindowsGuestLinkError(output) &&
                   output.find("stdout-only") != std::string::npos &&
                   output.find("stderr-only") != std::string::npos;
        destinations = forwarded[0].size() > 8192 && forwarded[1].size() > 8192 &&
                       forwarded[0].find("stdout-only") != std::string::npos &&
                       forwarded[0].find("stderr-only") == std::string::npos &&
                       forwarded[0].find("undefined symbol") == std::string::npos &&
                       forwarded[1].find("stderr-only") != std::string::npos &&
                       forwarded[1].find("undefined symbol") != std::string::npos &&
                       forwarded[1].find("stdout-only") == std::string::npos &&
                       forwarded[1].find("Linking CXX") == std::string::npos;
    }
    for (int i = 0; i < 2; ++i) {
        if (files[i] != INVALID_HANDLE_VALUE) CloseHandle(files[i]);
        if (paths[i][0]) DeleteFileA(paths[i]);
    }
    std::printf("Windows build stdout/stderr capture and exit code: %s\n", captured ? "PASS" : "FAIL");
    std::printf("Windows build preserves stdout/stderr destinations: %s\n", destinations ? "PASS" : "FAIL");
    if (!captured) ++failures;
    if (!destinations) ++failures;
#endif
    return failures ? 1 : 0;
}
