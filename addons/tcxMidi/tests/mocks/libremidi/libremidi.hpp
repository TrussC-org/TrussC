#pragma once

// Minimal output backend double. Production tcxMidiOut.h and tcLog.h are used
// unchanged; only libremidi is replaced so sends can fail deterministically.
#include <algorithm>
#include <cstddef>
#include <functional>
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
struct observer_configuration {
    std::function<void(const input_port&)> input_added, input_removed;
    std::function<void(const output_port&)> output_added, output_removed;
    bool track_hardware = true;
    bool track_virtual = false;
    bool notify_in_constructor = true;
};
namespace test {
inline bool virtualPorts = false;
// Ports the "OS" has removed: they vanish from every listing.
inline std::vector<std::string> unplugged;
inline bool isUnplugged(const std::string& name) {
    return std::find(unplugged.begin(), unplugged.end(), name) != unplugged.end();
}
// Live observers that have a removal callback, so a test can play the
// backend's thread: unplug("test input") then removePorts() from any thread.
inline std::vector<observer_configuration*> watchers;
inline int observersCreated = 0, observersDestroyed = 0;
}
struct observer {
    observer_configuration config;
    observer(observer_configuration cfg, API) : config(std::move(cfg)) {
        if (config.input_removed || config.output_removed) {
            ++test::observersCreated;
            test::watchers.push_back(&config);
        }
    }
    observer(const observer&) = delete;
    ~observer() {
        auto it = std::find(test::watchers.begin(), test::watchers.end(), &config);
        if (it != test::watchers.end()) {
            test::watchers.erase(it);
            ++test::observersDestroyed;
        }
    }
    std::vector<input_port> get_input_ports() const {
        std::vector<input_port> ports;
        if (config.track_hardware && !test::isUnplugged("test input")) ports.push_back({"test input"});
        if (test::virtualPorts && config.track_virtual && !test::isUnplugged("virtual source")) ports.push_back({"virtual source"});
        return ports;
    }
    std::vector<output_port> get_output_ports() const {
        std::vector<output_port> ports;
        if (config.track_hardware && !test::isUnplugged("test output")) ports.push_back({"test output"});
        if (test::virtualPorts && config.track_virtual && !test::isUnplugged("virtual destination")) ports.push_back({"virtual destination"});
        return ports;
    }
};
namespace test {
// What the backend does when a port goes away: the observer's removal callbacks.
inline void removeInput(const std::string& name) {
    for (auto* w : std::vector<observer_configuration*>(watchers))
        if (w->input_removed) w->input_removed(input_port{name});
}
inline void removeOutput(const std::string& name) {
    for (auto* w : std::vector<observer_configuration*>(watchers))
        if (w->output_removed) w->output_removed(output_port{name});
}
}

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
    bool ignore_sysex = false, ignore_timing = false, ignore_sensing = false;
    timestamp_mode timestamps = timestamp_mode::Relative;
};
inline API midi_in_configuration_for(API api) { return api; }
namespace test {
inline std::function<void(const message&)> inputCallback;
inline std::function<void()> duringClose;
inline int inputCloses = 0, inputDestructs = 0;
inline std::thread::id closeThread, destructThread;
}
class midi_in {
public:
    midi_in(input_configuration cfg, API) : callback_(std::move(cfg.on_message)) {
        test::inputCallback = callback_;
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
