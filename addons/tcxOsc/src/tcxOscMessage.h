#pragma once

#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <cstring>
#include <utility>
#include <algorithm>

namespace tcx::osc {

// =============================================================================
// Endian conversion utilities
// =============================================================================
namespace osc_internal {

// Align to 4-byte boundary
inline size_t alignTo4(size_t pos) {
    return (pos + 3) & ~3;
}

// Big-endian conversion (network byte order)
inline uint32_t toBigEndian(uint32_t value) {
    uint8_t* p = reinterpret_cast<uint8_t*>(&value);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline uint32_t fromBigEndian(uint32_t value) {
    return toBigEndian(value);  // Symmetric
}

inline uint64_t toBigEndian64(uint64_t value) {
    uint8_t* p = reinterpret_cast<uint8_t*>(&value);
    return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) |
           (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32) |
           (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) |
           (uint64_t(p[6]) << 8) | uint64_t(p[7]);
}

inline uint64_t fromBigEndian64(uint64_t value) {
    return toBigEndian64(value);
}

// Bit conversion between float and uint32
inline uint32_t floatToUint32(float f) {
    uint32_t result;
    std::memcpy(&result, &f, sizeof(float));
    return result;
}

inline float uint32ToFloat(uint32_t u) {
    float result;
    std::memcpy(&result, &u, sizeof(float));
    return result;
}

// Bit conversion between double and uint64
inline uint64_t doubleToUint64(double d) {
    uint64_t result;
    std::memcpy(&result, &d, sizeof(double));
    return result;
}

inline double uint64ToDouble(uint64_t u) {
    double result;
    std::memcpy(&result, &u, sizeof(double));
    return result;
}

// Find null terminator
inline size_t findNull(const uint8_t* data, size_t size, size_t start) {
    for (size_t i = start; i < size; ++i) {
        if (data[i] == 0) return i;
    }
    return size_t(-1);  // Not found
}

}  // namespace osc_internal

// =============================================================================
// Argument values without a C++ type of their own
// =============================================================================

// 'r': 32-bit RGBA color, one byte (0-255) per channel, as on the wire
struct OscRgba {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool operator==(const OscRgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const OscRgba& o) const { return !(*this == o); }
};

// 'm': 4-byte MIDI message, bytes in wire order (port id, status, data1, data2)
struct OscMidi {
    uint8_t port = 0, status = 0, data1 = 0, data2 = 0;
    bool operator==(const OscMidi& o) const {
        return port == o.port && status == o.status && data1 == o.data1 && data2 == o.data2;
    }
    bool operator!=(const OscMidi& o) const { return !(*this == o); }
};

// 't': timetag argument (NTP format, as OscBundle::getTimetag()). Its own
// type so that it is not mixed up with an 'h' (int64) argument.
struct OscTimetag {
    uint64_t value = 0;
    bool operator==(const OscTimetag& o) const { return value == o.value; }
    bool operator!=(const OscTimetag& o) const { return !(*this == o); }
};

// =============================================================================
// OscMessage - OSC message class
// =============================================================================
// Every OSC 1.0 type tag is supported, for sending and receiving:
//   i int32   f float32  s string   b blob     T true    F false
//   h int64   d float64  t timetag  S symbol   c char    r RGBA color
//   m MIDI    N nil      I impulse  [ array begin        ] array end
// Each tag is one argument, including the tags without data (T F N I [ ]),
// so getArgType(i) and the argument at index i always belong together.
class OscMessage {
public:
    // Argument values. Only tags with data store one; for T F N I [ ] the
    // type tag alone is the value.
    using ArgVariant = std::variant<int32_t, float, std::string, std::vector<uint8_t>, bool,
                                    int64_t, double, OscTimetag, char, OscRgba, OscMidi,
                                    std::monostate>;

    OscMessage() = default;
    explicit OscMessage(const std::string& address) : address_(address) {}

    // -------------------------------------------------------------------------
    // Address
    // -------------------------------------------------------------------------
    void setAddress(const std::string& address) { address_ = address; }
    std::string getAddress() const { return address_; }

    // -------------------------------------------------------------------------
    // Add arguments
    // -------------------------------------------------------------------------
    OscMessage& addInt(int32_t value) { return add<int32_t>('i', value); }
    OscMessage& addFloat(float value) { return add<float>('f', value); }
    OscMessage& addString(const std::string& value) { return add<std::string>('s', value); }

    OscMessage& addBlob(const void* data, size_t size) {
        std::vector<uint8_t> blob(static_cast<const uint8_t*>(data),
                                   static_cast<const uint8_t*>(data) + size);
        return add<std::vector<uint8_t>>('b', std::move(blob));
    }

    OscMessage& addBool(bool value) { return addTag(value ? 'T' : 'F'); }

    OscMessage& addInt64(int64_t value) { return add<int64_t>('h', value); }
    OscMessage& addDouble(double value) { return add<double>('d', value); }
    // NTP-format timetag (1 = immediately, as OscBundle::TIMETAG_IMMEDIATELY)
    OscMessage& addTimetag(uint64_t value) { return add<OscTimetag>('t', OscTimetag{value}); }
    // Symbol: encoded like a string, tagged 'S'
    OscMessage& addSymbol(const std::string& value) { return add<std::string>('S', value); }
    // ASCII character, sent as 32 bits
    OscMessage& addChar(char value) { return add<char>('c', value); }
    OscMessage& addRgba(const OscRgba& value) { return add<OscRgba>('r', value); }
    OscMessage& addMidi(const OscMidi& value) { return add<OscMidi>('m', value); }
    OscMessage& addNil() { return addTag('N'); }
    OscMessage& addImpulse() { return addTag('I'); }
    // The arguments added between these two form an array. Each bracket is
    // an argument of its own (tag '[' / ']'), so indexes stay aligned.
    OscMessage& addArrayBegin() { return addTag('['); }
    OscMessage& addArrayEnd() { return addTag(']'); }

    // -------------------------------------------------------------------------
    // Get arguments
    // -------------------------------------------------------------------------
    size_t getArgCount() const { return typeTags_.size(); }
    std::string getTypeTags() const { return typeTags_; }

    char getArgType(size_t index) const {
        if (index >= typeTags_.size()) return '\0';
        return typeTags_[index];
    }

    // The numeric getters (Int, Float, Int64, Double) convert between the
    // i, f, h and d arguments.
    int32_t getArgAsInt(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return 0;
        if (auto* v = std::get_if<int32_t>(arg)) return *v;
        if (auto* v = std::get_if<float>(arg)) return static_cast<int32_t>(*v);
        if (auto* v = std::get_if<int64_t>(arg)) return static_cast<int32_t>(*v);
        if (auto* v = std::get_if<double>(arg)) return static_cast<int32_t>(*v);
        return 0;
    }

    float getArgAsFloat(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return 0.0f;
        if (auto* v = std::get_if<float>(arg)) return *v;
        if (auto* v = std::get_if<int32_t>(arg)) return static_cast<float>(*v);
        if (auto* v = std::get_if<int64_t>(arg)) return static_cast<float>(*v);
        if (auto* v = std::get_if<double>(arg)) return static_cast<float>(*v);
        return 0.0f;
    }

    int64_t getArgAsInt64(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return 0;
        if (auto* v = std::get_if<int64_t>(arg)) return *v;
        if (auto* v = std::get_if<int32_t>(arg)) return *v;
        if (auto* v = std::get_if<float>(arg)) return static_cast<int64_t>(*v);
        if (auto* v = std::get_if<double>(arg)) return static_cast<int64_t>(*v);
        return 0;
    }

    double getArgAsDouble(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return 0.0;
        if (auto* v = std::get_if<double>(arg)) return *v;
        if (auto* v = std::get_if<float>(arg)) return *v;
        if (auto* v = std::get_if<int32_t>(arg)) return *v;
        if (auto* v = std::get_if<int64_t>(arg)) return static_cast<double>(*v);
        return 0.0;
    }

    // 's' and 'S' arguments
    std::string getArgAsString(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return "";
        if (auto* v = std::get_if<std::string>(arg)) return *v;
        return "";
    }

    // Same as getArgAsString() ('S' and 's' arguments)
    std::string getArgAsSymbol(size_t index) const { return getArgAsString(index); }

    std::vector<uint8_t> getArgAsBlob(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return {};
        if (auto* v = std::get_if<std::vector<uint8_t>>(arg)) return *v;
        return {};
    }

    // true for a 'T' argument, false otherwise
    bool getArgAsBool(size_t index) const {
        return index < typeTags_.size() && typeTags_[index] == 'T';
    }

    uint64_t getArgAsTimetag(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return 0;
        if (auto* v = std::get_if<OscTimetag>(arg)) return v->value;
        return 0;
    }

    char getArgAsChar(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return '\0';
        if (auto* v = std::get_if<char>(arg)) return *v;
        return '\0';
    }

    OscRgba getArgAsRgba(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return {};
        if (auto* v = std::get_if<OscRgba>(arg)) return *v;
        return {};
    }

    OscMidi getArgAsMidi(size_t index) const {
        const ArgVariant* arg = findArg(index);
        if (!arg) return {};
        if (auto* v = std::get_if<OscMidi>(arg)) return *v;
        return {};
    }

    // -------------------------------------------------------------------------
    // Serialize
    // -------------------------------------------------------------------------
    std::vector<uint8_t> toBytes() const;
    // ok is false when the data is not a message, a type tag is not an OSC
    // 1.0 tag, a ']' has no '[' before it or a '[' is not closed, or an
    // argument runs past the end of the data (e.g. a blob larger than the
    // bytes left).
    static OscMessage fromBytes(const uint8_t* data, size_t size, bool& ok);
    // Same, and paddingMissing tells whether the data ends before the zero
    // padding that aligns its last item to 4 bytes. Such a message still
    // parses (ok is true); OscReceiver logs a warning for it.
    static OscMessage fromBytes(const uint8_t* data, size_t size, bool& ok, bool& paddingMissing);

    // -------------------------------------------------------------------------
    // Debug string
    // -------------------------------------------------------------------------
    std::string toString() const;

    // -------------------------------------------------------------------------
    // Clear
    // -------------------------------------------------------------------------
    void clear() {
        address_.clear();
        typeTags_.clear();
        args_.clear();
    }

private:
    // An argument with data, and the index of its type tag
    struct StoredArg {
        size_t index;
        ArgVariant value;
    };

    template <typename T, typename V>
    OscMessage& add(char tag, V&& value) {
        storeArg<T>(typeTags_.size(), std::forward<V>(value));
        typeTags_ += tag;
        return *this;
    }

    // A tag without data (T F N I [ ]): the type tag alone
    OscMessage& addTag(char tag) {
        typeTags_ += tag;
        return *this;
    }

    // Append an argument with data for the type tag at index. Indexes are
    // appended in increasing order, so args_ stays sorted by index.
    template <typename T, typename... V>
    void storeArg(size_t index, V&&... value) {
        args_.push_back(StoredArg{ index, ArgVariant(std::in_place_type<T>, std::forward<V>(value)...) });
    }

    // The stored argument for the type tag at index, or nullptr when that
    // tag has no data or index is past the last tag
    const ArgVariant* findArg(size_t index) const {
        auto it = std::lower_bound(args_.begin(), args_.end(), index,
                                   [](const StoredArg& a, size_t i) { return a.index < i; });
        if (it == args_.end() || it->index != index) return nullptr;
        return &it->value;
    }

    friend struct OscMessageTestAccess;  // lets the addon tests count args_

    std::string address_;
    // One tag per argument, with or without data
    std::string typeTags_;
    // The arguments with data only, in tag order
    std::vector<StoredArg> args_;
};

}  // namespace tcx::osc

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::osc`. These
// silent aliases keep older code compiling: flat `tcx::OscMessage` and legacy
// `tc::OscMessage` / `trussc::OscMessage`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxOsc README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using osc::OscMessage; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::osc::OscMessage; } // deprecated: remove at v1.0.0
