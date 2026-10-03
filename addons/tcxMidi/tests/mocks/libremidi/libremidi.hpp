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
enum class API { DUMMY };
namespace midi1 { inline API default_api() { return API::DUMMY; } }
struct output_configuration {};
inline API midi_out_configuration_for(API api) { return api; }
inline API observer_configuration_for(API api) { return api; }
struct output_port { std::string display_name; };
struct observer_configuration {};
struct observer {
    observer(observer_configuration, API) {}
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
