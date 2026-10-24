// Host execution of the actual patched WinMM callback, with driver calls mocked.
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiSysex.h"
#include <libremidi/detail/midi_stream_decoder.hpp>
#include "allocation.h"
#include <array>
#include <cstring>
#include <thread>

namespace winmm_test {
using DWORD_PTR = uintptr_t;
using UINT = unsigned int;
using MMRESULT = unsigned int;
using HMIDIIN = void*;
struct MIDIHDR {
    char* lpData{};
    unsigned long dwBytesRecorded{};
    DWORD_PTR dwUser{};
};
constexpr UINT MIM_DATA = 0x3c3, MIM_LONGDATA = 0x3c4, MIM_LONGERROR = 0x3c6;
constexpr MMRESULT MMSYSERR_NOERROR = 0;
struct MockMutex { int depth = 0; };
void EnterCriticalSection(MockMutex* mutex) { ++mutex->depth; }
void LeaveCriticalSection(MockMutex* mutex) { --mutex->depth; }
MMRESULT requeueResult = 0;
int requeues = 0;
MMRESULT midiInAddBuffer(HMIDIIN, MIDIHDR*, size_t) {
    ++requeues;
    return requeueResult;
}
using libremidi::timestamp_backend_info;
struct midi_in_winmm {
    libremidi::input_configuration configuration;
    libremidi::midi1::input_state_machine m_processing{configuration};
    HMIDIIN inHandle{};
    std::array<MIDIHDR*, 1> sysexBuffer{};
    MockMutex _mutex;
    // CALLBACK is a calling-convention decoration only; no Windows SDK on host.
#pragma push_macro("CALLBACK")
#undef CALLBACK
#define CALLBACK
#include <winmm_callback.inc>
#pragma pop_macro("CALLBACK")
};
} // namespace winmm_test

void testWinmmCallback(void (*check)(bool, const char*)) {
    using namespace winmm_test;
    // Exercise first and subsequent losses, both together, ignored SysEx, and
    // the empty shutdown buffer that must never be requeued.
    for (int scenario = 0; scenario < 6; ++scenario) {
        auto loss = tcx::midi::internal::SysexLoss::create();
        int warnings = 0;
        bool truthful = true, safeThread = true;
        const auto mainThread = std::this_thread::get_id();
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level != tc::LogLevel::Warning) return;
            ++warnings;
            safeThread &= std::this_thread::get_id() == mainThread;
            truthful &= event.message.find("size unknown") != std::string::npos &&
                        event.message.find("1048576") == std::string::npos &&
                        event.message.find("needed 0") == std::string::npos;
        });
        midi_in_winmm input;
        input.configuration.ignore_sysex = scenario == 3;
        // count() is noexcept in production. Let a mutation that removes that
        // guarantee reach the worker's catcher and fail assertions, not abort.
        input.configuration.on_sysex_overflow = [loss](size_t needed) { loss->count(needed); };
        input.configuration.timestamps = libremidi::timestamp_mode::NoTimestamp;
        size_t delivered = 0;
        input.configuration.on_message = [&](libremidi::message&&) { ++delivered; };
        // Seed a partial message outside measurement: LONGERROR must reset it.
        input.m_processing.message.bytes.reserve(1024);
        const std::array<uint8_t, 2> partial{0xF0, 1};
        input.m_processing.on_bytes(partial, 0);
        const bool longError = scenario != 1 && scenario != 4;
        const bool failRequeue = scenario == 1 || scenario == 2;
        const bool empty = scenario == 5;
        MIDIHDR header{};
        char payload = 1;
        header.lpData = &payload;
        header.dwBytesRecorded = empty ? 0 : 1;
        input.sysexBuffer[0] = &header;
        requeueResult = failRequeue ? 1 : MMSYSERR_NOERROR;
        requeues = 0;
        size_t allocations = 0;
        bool threw = false;
        const auto before = loss->discarded.load();
        std::thread worker([&] {
            allocation_test::allocations = 0;
            allocation_test::measure = allocation_test::fail = true;
            try {
                for (int i = 0; i < 2; ++i)
                    midi_in_winmm::midiInputCallback(nullptr, longError ? MIM_LONGERROR : MIM_LONGDATA,
                        reinterpret_cast<DWORD_PTR>(&input),
                        reinterpret_cast<DWORD_PTR>(&header), 0);
            } catch (...) { threw = true; }
            allocations = allocation_test::allocations;
            allocation_test::measure = allocation_test::fail = false;
        });
        worker.join();
        const uint64_t perEvent = (longError ? 1 : 0) + (failRequeue ? 1 : 0);
        check(!threw && allocations == 0, "WinMM callback allocates nothing with backend allocation failure");
        check(loss->discarded == before + 2 * perEvent,
              scenario == 1 ? "WinMM failed requeue counts one loss per event" :
              scenario == 2 ? "WinMM LONGERROR and failed requeue count separate loss events" :
              scenario == 4 ? "WinMM successful requeue counts no loss" :
              "WinMM LONGERROR counts one loss per event (including ignored and empty input)");
        check(requeues == (empty ? 0 : 2) && input._mutex.depth == 0,
              "WinMM preserves requeue and shutdown guards and releases its lock");
        check(!longError || (input.m_processing.message.empty() && delivered == 0),
              "WinMM LONGERROR clears the partial decoder state");
        check(warnings == 0, "WinMM callback does not log on the backend thread");
#if !defined(__EMSCRIPTEN__)
        check(tc::internal::countQueuedMainThreadSlots() == (perEvent ? 1u : 0u),
              "WinMM first loss posts exactly one Warning slot");
#endif
        tc::internal::drainMainThreadQueue();
        check(warnings == (perEvent ? 1 : 0) && truthful && safeThread,
              "WinMM loss produces one truthful main-thread Warning per owner");
        // Reset must cancel running SysEx, so an orphan EOX cannot deliver it.
        const std::array<uint8_t, 1> eox{0xF7};
        if (longError) {
            input.m_processing.on_bytes(eox, 0);
            check(delivered == 0, "WinMM LONGERROR does not deliver the damaged SysEx tail");
        }
    }
}
