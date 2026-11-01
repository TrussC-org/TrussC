#pragma once

#include "tcxOscMessage.h"
#include <variant>

namespace tcx::osc {

// =============================================================================
// OscBundle - OSC bundle class
// Bundles can contain multiple messages or other bundles
// =============================================================================
class OscBundle {
public:
    // Bundle element (message or bundle)
    using Element = std::variant<OscMessage, OscBundle>;

    // Timetag representing immediate execution
    static constexpr uint64_t TIMETAG_IMMEDIATELY = 1;

    // Bundles nest to any depth that fits in the packet. Parsing, copying,
    // destroying and serializing a bundle walk the nesting with a heap stack,
    // not recursion, so depth never costs call-stack space (a 64 KiB datagram
    // can hold a bit over 3000 levels).

    OscBundle() : timetag_(TIMETAG_IMMEDIATELY) {}
    explicit OscBundle(uint64_t timetag) : timetag_(timetag) {}

    // Copy and destruction are iterative (see above); moves just hand over
    // the element vector.
    OscBundle(const OscBundle& other);
    OscBundle& operator=(const OscBundle& other);
    OscBundle(OscBundle&&) noexcept = default;
    OscBundle& operator=(OscBundle&&) noexcept = default;
    ~OscBundle();

    // -------------------------------------------------------------------------
    // Timetag (NTP format)
    // -------------------------------------------------------------------------
    void setTimetag(uint64_t timetag) { timetag_ = timetag; }
    uint64_t getTimetag() const { return timetag_; }

    // -------------------------------------------------------------------------
    // Add elements
    // -------------------------------------------------------------------------
    OscBundle& addMessage(const OscMessage& msg) {
        elements_.emplace_back(msg);
        return *this;
    }

    OscBundle& addBundle(const OscBundle& bundle) {
        elements_.emplace_back(bundle);
        return *this;
    }

    // -------------------------------------------------------------------------
    // Get elements
    // -------------------------------------------------------------------------
    size_t getElementCount() const { return elements_.size(); }

    bool isBundle(size_t index) const {
        if (index >= elements_.size()) return false;
        return std::holds_alternative<OscBundle>(elements_[index]);
    }

    bool isMessage(size_t index) const {
        if (index >= elements_.size()) return false;
        return std::holds_alternative<OscMessage>(elements_[index]);
    }

    OscMessage getMessageAt(size_t index) const {
        if (index >= elements_.size()) return OscMessage();
        if (auto* msg = std::get_if<OscMessage>(&elements_[index])) {
            return *msg;
        }
        return OscMessage();
    }

    OscBundle getBundleAt(size_t index) const {
        if (index >= elements_.size()) return OscBundle();
        if (auto* bundle = std::get_if<OscBundle>(&elements_[index])) {
            return *bundle;
        }
        return OscBundle();
    }

    // Child bundle without a copy; nullptr if the element is not a bundle.
    // Valid while this bundle is alive and no element is added or removed.
    const OscBundle* bundleAt(size_t index) const {
        if (index >= elements_.size()) return nullptr;
        return std::get_if<OscBundle>(&elements_[index]);
    }

    // -------------------------------------------------------------------------
    // Serialize
    // -------------------------------------------------------------------------
    std::vector<uint8_t> toBytes() const;
    // ok is false when any element is truncated (its size runs past the end
    // of the data), or any nested message or bundle fails to parse (see
    // OscMessage::fromBytes()): the whole bundle is rejected, not a part of
    // it. The preflight also limits the whole outer packet to 65,536 bytes
    // (the UDP datagram size); nesting depth has no limit of its own.
    static OscBundle fromBytes(const uint8_t* data, size_t size, bool& ok);
    // Same, and paddingMissing tells whether any message in the bundle ends
    // before its zero padding to 4 bytes (it still parses, as in
    // OscMessage::fromBytes()).
    static OscBundle fromBytes(const uint8_t* data, size_t size, bool& ok, bool& paddingMissing);

    // -------------------------------------------------------------------------
    // Bundle detection (check if data starts with "#bundle")
    // -------------------------------------------------------------------------
    static bool isBundle(const uint8_t* data, size_t size) {
        if (size < 8) return false;
        return data[0] == '#' && data[1] == 'b' && data[2] == 'u' &&
               data[3] == 'n' && data[4] == 'd' && data[5] == 'l' &&
               data[6] == 'e' && data[7] == '\0';
    }

    // -------------------------------------------------------------------------
    // Clear
    // -------------------------------------------------------------------------
    void clear() {
        timetag_ = TIMETAG_IMMEDIATELY;
        elements_.clear();
    }

private:
    void copyElementsFrom(const OscBundle& other);

    uint64_t timetag_;
    std::vector<Element> elements_;
};

}  // namespace tcx::osc

// -----------------------------------------------------------------------------
// Backward compatibility: see tcxOscMessage.h. DEPRECATED — removed in v1.0.0.
// -----------------------------------------------------------------------------
namespace tcx    { using osc::OscBundle; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::osc::OscBundle; } // deprecated: remove at v1.0.0
