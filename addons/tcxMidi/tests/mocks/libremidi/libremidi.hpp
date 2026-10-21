#pragma once

// Minimal output backend double. Production tcxMidiOut.h and tcLog.h are used
// unchanged; only libremidi is replaced so sends can fail deterministically.
#include <cstddef>
#include <string>
#include <vector>

namespace stdx {
struct error {
    bool failed = false;
    bool operator!=(const error& other) const { return failed != other.failed; }
    std::string message() const { return "injected send error"; }
};
}

namespace libremidi {
enum class API { DUMMY, WEBMIDI };
namespace midi1 { inline API default_api() { return API::DUMMY; } }
struct output_configuration {};
inline API midi_out_configuration_for(API api) { return api; }
inline API observer_configuration_for(API api) { return api; }
struct output_port { std::string display_name; };
struct input_port { std::string display_name; };
struct observer_configuration {};
struct observer {
    observer(observer_configuration, API) {}
    std::vector<input_port> get_input_ports() const { return {{"test input"}}; }
    std::vector<output_port> get_output_ports() const { return {{"test output"}}; }
};

namespace test {
inline bool failSend = false;
inline size_t sends = 0;
inline std::vector<unsigned char> lastBytes;
}

class midi_out {
public:
    midi_out(output_configuration, API) {}
    stdx::error open_port(const output_port&) { open_ = true; return {}; }
    stdx::error open_virtual_port(const std::string&) { open_ = true; return {}; }
    stdx::error close_port() { open_ = false; return {}; }
    bool is_port_open() const { return open_; }
    stdx::error send_message(const unsigned char* bytes, size_t size) {
        ++test::sends;
        test::lastBytes.assign(bytes, bytes + size);
        return {test::failSend};
    }
private:
    bool open_ = false;
};
}

// Input backend double: a copied callback can represent a message already
// acquired by the driver before close_port() unregisters it.
#include <functional>
#include <cstdint>
#include <thread>
namespace libremidi {
enum class timestamp_mode { Relative };
struct message { std::vector<unsigned char> bytes; std::int64_t timestamp = 0; };
struct input_configuration {
    std::function<void(const message&)> on_message;
    std::function<void(size_t)> on_sysex_overflow;
    bool ignore_sysex = false, ignore_timing = false, ignore_sensing = false;
    timestamp_mode timestamps = timestamp_mode::Relative;
};
inline API midi_in_configuration_for(API api) { return api; }
namespace test {
inline std::function<void(const message&)> inputCallback;
inline std::function<void()> duringClose;
inline std::function<void(size_t)> sysexOverflow;
inline int inputCloses = 0, inputDestructs = 0;
inline std::thread::id closeThread, destructThread;
}
class midi_in {
public:
    midi_in(input_configuration cfg, API) : callback_(std::move(cfg.on_message)) {
        test::inputCallback = callback_;
        test::sysexOverflow = std::move(cfg.on_sysex_overflow);
    }
    ~midi_in() { ++test::inputDestructs; test::destructThread = std::this_thread::get_id(); }
    stdx::error open_port(const input_port&) { open_ = true; return {}; }
    stdx::error open_virtual_port(const std::string&) { open_ = true; return {}; }
    stdx::error close_port() {
        ++test::inputCloses;
        test::closeThread = std::this_thread::get_id();
        if (test::duringClose) test::duringClose();
        open_ = false;
        return {};
    }
    bool is_port_open() const { return open_; }
private:
    std::function<void(const message&)> callback_;
    bool open_ = false;
};
}
