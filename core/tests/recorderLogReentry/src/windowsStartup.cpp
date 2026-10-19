// Compile the Windows AAC backend with a failing MFStartup, while retaining its
// actual once flag, startup helper and logging. This own-binary test supplies
// the backend definitions, so its library object is not pulled from TrussC.
#ifdef _WIN32
#include <TrussC.h>
#include <mfapi.h>
namespace {
int startupCalls = 0;
HRESULT WINAPI failingMFStartup(ULONG, DWORD = MFSTARTUP_FULL) {
    ++startupCalls;
    return E_FAIL;
}
}
#define MFStartup failingMFStartup
#include "../../../platform/win/tcSound_win.cpp"
#undef MFStartup

bool testWindowsStartupLogReentry() {
    int diagnostics = 0;
    auto log = trussc::getLogger().onLog.listen([&](trussc::LogEventArgs& e) {
        if (e.message != "[SoundBuffer] MFStartup failed (0x80004005)") return;
        ++diagnostics;
        trussc::EnsureMFStartup();
    });
    trussc::EnsureMFStartup();
    trussc::EnsureMFStartup();
    return diagnostics == 1 && startupCalls == 1;
}
#endif
