#include "tc/app/internal/tcWindowsBuildOutput.h"
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;
}
TC_CORE_TEST_MAIN() {
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
        captured = rc == 7 &&
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
