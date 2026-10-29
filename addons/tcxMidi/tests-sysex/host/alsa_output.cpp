#if defined(__linux__) && !defined(__EMSCRIPTEN__)
// Production ALSA output with test symbols for dlopen and counted sleep requests.
// The encoder is the real libasound codec; no sequencer device is opened.
#include <cstring>
#include <chrono>
void alsaTestSleep(std::chrono::milliseconds duration);
void* alsaTestSymbol(const char* name);
#include <libremidi/backends/alsa_seq/midi_out.hpp>
#include <cstdio>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
Bytes delivered, pending;
int drains = 0, drops = 0, encoded = 0, transient = 0, reads = 0;
int permanentError = 0;
bool fatal = false, rejectedLarge = false, outputOnly = false;
struct Context { bool nonblocking = false; } privateContext;
int opens = 0, closes = 0, waits = 0;
int fakeNonblock(snd_seq_t* seq, int value) {
    reinterpret_cast<Context*>(seq)->nonblocking = value == 1;
    return 0;
}
int fakeOpen(snd_seq_t** seq, const char*, int, int mode) {
    ++opens;
    privateContext.nonblocking = (mode & SND_SEQ_NONBLOCK) != 0;
    *seq = reinterpret_cast<snd_seq_t*>(&privateContext);
    return 0;
}
int fakeClose(snd_seq_t*) { ++closes; return 0; }
int fakeName(snd_seq_t*, const char*) { return 0; }
int fakeMidiVersion(snd_seq_t*, int) { return 0; }
int fakeDrop(snd_seq_t*) { ++drops; pending.clear(); return 0; }
int fakeOutput(snd_seq_t*, snd_seq_event_t* event) {
    ++encoded;
    if (event->type == SND_SEQ_EVENT_NOTEON) {
        pending = {0x90, event->data.note.note, event->data.note.velocity};
        return static_cast<int>(pending.size());
    }
    if (event->type != SND_SEQ_EVENT_SYSEX || event->data.ext.len > 256) {
        rejectedLarge = true;
        return -EIO;
    }
    const auto* bytes = static_cast<const unsigned char*>(event->data.ext.ptr);
    pending.assign(bytes, bytes + event->data.ext.len);
    return static_cast<int>(pending.size());
}
int fakeDrain(snd_seq_t*) {
    ++drains;
    if (fatal) return -EIO;
    if (permanentError) return -permanentError;
    if (transient > 0) return (--transient % 2) ? -EAGAIN : -EINTR;
    delivered.insert(delivered.end(), pending.begin(), pending.end());
    pending.clear();
    return 0;
}
int fakeInput(snd_seq_t*, snd_seq_event_t** event) {
    ++reads;
    if (outputOnly) return -EBADF;
    // Unrelated input belongs to a different port sharing this duplex handle.
    static snd_seq_event_t note{};
    note.type = SND_SEQ_EVENT_NOTEON;
    *event = &note;
    return 0;
}
int fakeInputPending(snd_seq_t*, int) { ++reads; return outputOnly ? -EBADF : 1; }
int failures = 0;
void check(bool ok, const char* label) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
void reset() {
    delivered.clear(); pending.clear(); drains = drops = encoded = transient = 0;
    permanentError = waits = 0;
    fatal = rejectedLarge = false;
}
}
void alsaTestSleep(std::chrono::milliseconds duration) {
    ++waits;
    check(duration == std::chrono::milliseconds(1), "each output wait requests one millisecond");
}
void* alsaTestSymbol(const char* name) {
#define MOCK_SYMBOL(symbol, function) if (std::strcmp(name, #symbol) == 0) return reinterpret_cast<void*>(&function)
    MOCK_SYMBOL(snd_seq_nonblock, fakeNonblock);
    MOCK_SYMBOL(snd_seq_open, fakeOpen);
    MOCK_SYMBOL(snd_seq_close, fakeClose);
    MOCK_SYMBOL(snd_seq_set_client_name, fakeName);
    MOCK_SYMBOL(snd_seq_set_client_midi_version, fakeMidiVersion);
    MOCK_SYMBOL(snd_seq_drop_output, fakeDrop);
    MOCK_SYMBOL(snd_seq_event_output, fakeOutput);
    MOCK_SYMBOL(snd_seq_drain_output, fakeDrain);
    MOCK_SYMBOL(snd_seq_event_input, fakeInput);
    MOCK_SYMBOL(snd_seq_event_input_pending, fakeInputPending);
#undef MOCK_SYMBOL
    return nullptr;
}
int main() {
    Context context;
    {
        libremidi::alsa_seq::output_configuration api;
        api.context = reinterpret_cast<snd_seq_t*>(&context);
        libremidi::alsa_seq::midi_out_impl output({}, std::move(api));
        check(!context.nonblocking, "supplied blocking context mode unchanged after construction");
        Bytes bytes(1048577);
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>((i * 37 + i / 128) % 128);
        bytes.front() = 0xF0; bytes.back() = 0xF7;
        auto result = output.send_message(bytes.data(), bytes.size());
        check(result == stdx::error{} && delivered == bytes && !rejectedLarge && encoded == 4097,
              "real encoder splits reported-size SysEx; all bytes arrive in order");
        check(drains == encoded && pending.empty(), "every chunk drained before success");
        check(waits == 4096, "partial SysEx events are paced on the calling thread");
        check(reads == 0, "shared duplex output never reads unrelated input");
        bytes.resize(513); bytes.back() = 0xF7;
        reset(); transient = 2;
        result = output.send_message(bytes.data(), bytes.size());
        check(result == stdx::error{} && delivered == bytes && drains == encoded + 2,
              "transient EAGAIN and EINTR retry drain without repeating the event");
        reset(); fatal = true;
        result = output.send_message(bytes.data(), bytes.size());
        check(result == std::errc::io_error && encoded == 1 && drains == 1 && drops == 1 && pending.empty(),
              "fatal drain discards pending bytes and stops");
        for (int error : {EAGAIN, EINTR}) {
            reset(); permanentError = error;
            result = output.send_message(bytes.data(), bytes.size());
            check(result == std::errc::timed_out && encoded == 1 && drains == 1001 && drops == 1 && pending.empty(),
                  "permanent EAGAIN or EINTR exhausts the exact retry bound and discards pending bytes");
        }
        reset();
        result = output.send_message(bytes.data(), bytes.size());
        check(result == stdx::error{} && delivered == bytes, "new F0 recovers after a failed partial send");
        check(reads == 0, "shared duplex output never reads unrelated input");

        check(!context.nonblocking, "supplied blocking context mode unchanged after send");
    }
    check(!context.nonblocking, "supplied blocking context mode unchanged after destruction");
    check(opens == 0 && closes == 0, "supplied context is neither opened nor closed");

    // Also preserve a caller-supplied nonblocking output-only handle.
    context.nonblocking = true;
    {
        Bytes bytes(513, 0x01);
        bytes.front() = 0xF0; bytes.back() = 0xF7;
        stdx::error result;
        // Same supplied-context API with a handle opened SND_SEQ_OPEN_OUTPUT.
        // Any read would count and return EBADF, even when its result is ignored.
        reads = 0;
        outputOnly = true;
        libremidi::alsa_seq::output_configuration writeApi;
        writeApi.context = reinterpret_cast<snd_seq_t*>(&context);
        libremidi::alsa_seq::midi_out_impl writeOutput({}, std::move(writeApi));
        check(context.nonblocking, "supplied nonblocking context mode unchanged after construction");
        reset();
        result = writeOutput.send_message(bytes.data(), bytes.size());
        check(result == stdx::error{} && delivered == bytes, "supplied output-only context sends complete SysEx");
        reset();
        const Bytes note{0x90, 60, 100};
        result = writeOutput.send_message(note.data(), note.size());
        check(result == stdx::error{} && delivered == note, "supplied output-only context sends ordinary notes");
        check(reads == 0, "supplied output-only context never makes an input call");
        check(context.nonblocking, "supplied nonblocking context mode unchanged after send");
    }
    check(context.nonblocking, "supplied nonblocking context mode unchanged after destruction");
    {
        reset();
        libremidi::alsa_seq::midi_out_impl privateOutput({}, {});
        check(opens == 1 && privateContext.nonblocking, "private client opens nonblocking");
        const Bytes note{0x90, 60, 100};
        const auto result = privateOutput.send_message(note.data(), note.size());
        check(result == stdx::error{} && delivered == note && privateContext.nonblocking,
              "private client stays nonblocking after send");
    }
    check(closes == 1 && privateContext.nonblocking, "private client stays nonblocking until destruction closes it");
    std::printf("ALSA output host checks: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
