// Two real ALSA clients; the receiver uses the sequencer directly so this check
// does not depend on observer virtual-port enumeration (#982).
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiOut.h"
#include <atomic>
#include <cstdio>
#include <thread>
#if defined(LIBREMIDI_ALSA)
#include <alsa/asoundlib.h>
#include <unistd.h>
#endif

int alsaRoundtrip(bool requireDevice) {
#if defined(LIBREMIDI_ALSA)
    snd_seq_t* receiver{};
    const int opened = snd_seq_open(&receiver, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (opened < 0) {
        std::printf("%s: ALSA sequencer cannot be opened: %s\n",
                    requireDevice ? "FAIL" : "SKIP", snd_strerror(opened));
        return requireDevice ? 1 : 0;
    }
    struct CloseClient {
        snd_seq_t* client;
        ~CloseClient() { snd_seq_close(client); }
    } closeClient{receiver};
    const int port = snd_seq_create_simple_port(receiver, "TrussC-992-input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_APPLICATION);
    // Bound (pacing regression): retain only 200 input pool cells; overflow loses receiver events and fails the byte comparison.
    if (port < 0 || snd_seq_set_client_pool_input(receiver, 200) < 0) return 1;
    const std::string name = "TrussC-992-output-" + std::to_string(getpid());
    tcx::midi::MidiOut output;
    if (!output.openVirtualPort(name)) return 1;
    snd_seq_client_info_t* clientInfo{};
    snd_seq_client_info_alloca(&clientInfo);
    snd_seq_port_info_t* portInfo{};
    snd_seq_port_info_alloca(&portInfo);
    snd_seq_client_info_set_client(clientInfo, -1);
    bool connected = false;
    while (!connected && snd_seq_query_next_client(receiver, clientInfo) >= 0) {
        const int client = snd_seq_client_info_get_client(clientInfo);
        snd_seq_port_info_set_client(portInfo, client);
        snd_seq_port_info_set_port(portInfo, -1);
        while (snd_seq_query_next_port(receiver, portInfo) >= 0) {
            if (name == snd_seq_port_info_get_name(portInfo)) {
                connected = snd_seq_connect_from(receiver, port, client,
                    snd_seq_port_info_get_port(portInfo)) >= 0;
                break;
            }
        }
    }
    if (!connected) return 1;
    // Test the two original #992 regression sizes.
    std::vector<unsigned char> first(1048577), second(1048576);
    for (size_t i = 0; i < first.size(); ++i) first[i] = static_cast<unsigned char>((i * 37 + i / 128) % 128);
    for (size_t i = 0; i < second.size(); ++i) second[i] = static_cast<unsigned char>((i * 53 + i / 127) % 128);
    first.front() = second.front() = 0xF0;
    first.back() = second.back() = 0xF7;
    auto expected = first;
    expected.insert(expected.end(), second.begin(), second.end());
    std::vector<unsigned char> received;
    received.reserve(expected.size());
    std::atomic<bool> stop{false};
    bool receiveOk = true;
    // Bound (device check): stop on completion, driver error or a 30 s deadline;
    // deadline expiry is a failed delivery, never a timing assertion.
    std::thread reader([&] {
        try {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!stop && received.size() < expected.size() && std::chrono::steady_clock::now() < deadline) {
                snd_seq_event_t* event{};
                const int result = snd_seq_event_input(receiver, &event);
                if (result == -EAGAIN) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                if (result < 0) { receiveOk = false; break; }
                if (event->type == SND_SEQ_EVENT_SYSEX) {
                    const auto* bytes = static_cast<const unsigned char*>(event->data.ext.ptr);
                    received.insert(received.end(), bytes, bytes + event->data.ext.len);
                }
                snd_seq_free_event(event);
            }
        } catch (...) { receiveOk = false; }
    });
    const bool sent = output.sendSysex(first) && output.sendSysex(second);
    if (!sent) stop = true;
    reader.join();
    const bool matches = receiveOk && received == expected;
    std::printf("%s: ALSA SysEx roundtrip sizes=1048577,1048576 sent=%d received=%zu byte_match=%d\n",
                sent && matches ? "PASS" : "FAIL", sent, received.size(), matches);
    return sent && matches ? 0 : 1;
#else
    std::printf("%s: ALSA sequencer backend not built\n", requireDevice ? "FAIL" : "SKIP");
    return requireDevice ? 1 : 0;
#endif
}
