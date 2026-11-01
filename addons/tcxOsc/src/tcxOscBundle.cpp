#include "tcxOscBundle.h"
#include "tcxOscPreflight.h"

namespace tcx::osc {

using namespace osc_internal;

// =============================================================================
// Copy and destruction - iterative, so nesting depth never costs call stack.
// =============================================================================
OscBundle::OscBundle(const OscBundle& other) : timetag_(other.timetag_) {
    copyElementsFrom(other);
}

OscBundle& OscBundle::operator=(const OscBundle& other) {
    if (this != &other) {
        OscBundle copy(other);
        *this = std::move(copy);
    }
    return *this;
}

// Copies other's elements into this (empty) bundle. A child bundle is added
// empty, then filled when it is popped from the heap stack. Each bundle's
// vector is reserved before its children are queued, so the pointers on the
// stack stay valid until their turn.
void OscBundle::copyElementsFrom(const OscBundle& other) {
    struct Job { const OscBundle* from; OscBundle* to; };
    std::vector<Job> jobs;
    jobs.push_back({&other, this});
    while (!jobs.empty()) {
        const Job job = jobs.back();
        jobs.pop_back();
        job.to->elements_.reserve(job.from->elements_.size());
        for (const auto& element : job.from->elements_) {
            if (auto* msg = std::get_if<OscMessage>(&element)) {
                job.to->elements_.emplace_back(*msg);
            } else {
                const auto& child = std::get<OscBundle>(element);
                job.to->elements_.emplace_back(std::in_place_type<OscBundle>, child.timetag_);
                jobs.push_back({&child, &std::get<OscBundle>(job.to->elements_.back())});
            }
        }
    }
}

// Moves child bundles out onto a heap stack, so that each bundle is destroyed
// with no child bundles left in it: the destructor never recurses more than a
// level. If the stack cannot grow (out of memory), the rest is destroyed the
// ordinary, recursive way.
OscBundle::~OscBundle() {
    try {
        std::vector<OscBundle> pending;
        auto takeChildren = [&pending](OscBundle& bundle) {
            for (auto& element : bundle.elements_)
                if (auto* child = std::get_if<OscBundle>(&element))
                    if (!child->elements_.empty()) pending.push_back(std::move(*child));
        };
        takeChildren(*this);
        while (!pending.empty()) {
            OscBundle bundle = std::move(pending.back());
            pending.pop_back();
            takeChildren(bundle);
        }
    } catch (...) {
    }
}

// =============================================================================
// toBytes - Serialize bundle to byte array
// =============================================================================
std::vector<uint8_t> OscBundle::toBytes() const {
    std::vector<uint8_t> result;

    // Nested bundles are written in place, depth first, with a heap stack.
    // Each element's size field is filled in once the element is complete.
    struct Frame { const OscBundle* bundle; size_t next; size_t sizeAt; };
    std::vector<Frame> stack;

    // "#bundle\0" + timetag (8 bytes, big-endian)
    auto writeHeader = [&result](const OscBundle& bundle) {
        const char* bundleId = "#bundle";
        result.insert(result.end(), bundleId, bundleId + 8);
        uint64_t be = toBigEndian64(bundle.timetag_);
        auto* p = reinterpret_cast<uint8_t*>(&be);
        result.insert(result.end(), p, p + 8);
    };
    auto closeElement = [&result](size_t sizeAt) {
        // Size (4 bytes, big-endian), of the element data after it
        uint32_t size = static_cast<uint32_t>(result.size() - sizeAt - 4);
        uint32_t sizeBe = toBigEndian(size);
        std::memcpy(result.data() + sizeAt, &sizeBe, 4);
    };

    writeHeader(*this);
    stack.push_back({this, 0, 0});
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.next == frame.bundle->elements_.size()) {
            const size_t sizeAt = frame.sizeAt;
            const bool nested = stack.size() > 1;
            stack.pop_back();
            if (nested) closeElement(sizeAt);
            continue;
        }
        const Element& element = frame.bundle->elements_[frame.next++];
        const size_t sizeAt = result.size();
        result.insert(result.end(), 4, 0);  // size, filled in by closeElement()
        if (auto* msg = std::get_if<OscMessage>(&element)) {
            const auto elementBytes = msg->toBytes();
            result.insert(result.end(), elementBytes.begin(), elementBytes.end());
            closeElement(sizeAt);
        } else {
            const auto* child = &std::get<OscBundle>(element);
            writeHeader(*child);
            stack.push_back({child, 0, sizeAt});
        }
    }

    return result;
}

// =============================================================================
// fromBytes - Parse bundle from byte array (robust implementation)
// =============================================================================
OscBundle OscBundle::fromBytes(const uint8_t* data, size_t size, bool& ok) {
    bool paddingMissing = false;
    return fromBytes(data, size, ok, paddingMissing);
}

OscBundle OscBundle::fromBytes(const uint8_t* data, size_t size, bool& ok, bool& paddingMissing) {
    // paddingMissing is only ever set here (cleared first), so every nested
    // level adds to the same flag.
    paddingMissing = false;
    ok = false;
    PacketPreflight preflight;
    if (!preflight.scan(data, size)) return OscBundle();

    // Nested bundles are parsed with a heap stack of open bundles, not
    // recursion: the packet is untrusted, so its depth must not decide the
    // call-stack use. A frame's `bundle` points into its parent's element
    // vector; the parent appends nothing while its child is the top frame.
    struct Frame {
        OscBundle* bundle;
        const uint8_t* data;
        size_t size;
        size_t pos;
    };
    std::vector<Frame> stack;
    OscBundle root;

    // Open a bundle: "#bundle\0" (8) + timetag (8) = 16 bytes at least.
    auto open = [&stack](OscBundle& bundle, const uint8_t* bytes, size_t length) {
        if (!bytes || length < 16 || !isBundle(bytes, length)) return false;
        uint64_t be;
        std::memcpy(&be, bytes + 8, 8);
        bundle.timetag_ = fromBigEndian64(be);
        stack.push_back({&bundle, bytes, length, 16});
        return true;
    };
    if (!open(root, data, size)) return OscBundle();

    // Read elements: each is a 4-byte size, then that many bytes. The bundle
    // parses only if every element is there in full. pos <= size holds
    // throughout, and each check subtracts from size instead of adding to pos,
    // so none of them can wrap when size_t is 32 bits.
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.pos >= frame.size) {
            stack.pop_back();
            continue;
        }

        // Element size (a size field cut short is a truncated element)
        if (frame.size - frame.pos < 4) return OscBundle();
        uint32_t sizeBe;
        std::memcpy(&sizeBe, frame.data + frame.pos, 4);
        const uint32_t elementSize = fromBigEndian(sizeBe);
        frame.pos += 4;

        // An element larger than the data left is truncated
        // Bound (security): untrusted element lengths must stay within the received packet. Overflow rejects the whole packet; the receiver counts rejected datagrams.
        if (elementSize > frame.size - frame.pos) return OscBundle();

        const uint8_t* elementData = frame.data + frame.pos;
        OscBundle* parent = frame.bundle;
        frame.pos += elementSize;   // before a push_back below invalidates `frame`

        // Determine if bundle or message
        if (isBundle(elementData, elementSize)) {
            // A nested bundle that fails rejects this bundle too, so the
            // caller sees one parse error instead of a partial bundle.
            parent->elements_.emplace_back(std::in_place_type<OscBundle>);
            if (!open(std::get<OscBundle>(parent->elements_.back()), elementData, elementSize))
                return OscBundle();
        }
        else {
            bool elementOk = false;
            bool elementPaddingMissing = false;
            OscMessage msg = OscMessage::fromBytes(elementData, elementSize, elementOk,
                                                   elementPaddingMissing);
            // A message that fails rejects the bundle too, as a nested
            // bundle does.
            if (!elementOk) return OscBundle();
            if (elementPaddingMissing) paddingMissing = true;
            parent->elements_.emplace_back(std::move(msg));
        }
    }

    ok = true;
    return root;
}

}  // namespace tcx::osc
