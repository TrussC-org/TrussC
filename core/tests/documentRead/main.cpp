// Filebuf may report an I/O error as EOF. Never parse a short, valid prefix.
#include "nlohmann/json.hpp"
#include "impl/tcDocumentInput.h"
#include "tc/3d/tcIesParse.h"
#include "tcLutParse.h"
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
bool trackBufferAllocations = false;
size_t peakBufferAllocation = 0;
}
// Observe the fallback's actual string allocations without a production hook.
void* operator new(std::size_t size) {
    if (trackBufferAllocations) peakBufferAllocation = std::max(peakBufferAllocation, size);
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <fstream>
EM_JS(void, installReadErrors, (), {
    const inputs = {
        '/read.json': '{"count":42}',
        '/read.ies': 'TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0 42\n',
        '/read.cube': 'LUT_3D_SIZE 2\n' + '0 0 0\n'.repeat(8)
    };
    for (const path in inputs) {
        const prefix = new TextEncoder().encode(inputs[path]);
        FS.writeFile(path, inputs[path] + ' \n');
        const node = FS.lookupPath(path).node;
        node.stream_ops = {...node.stream_ops, read(stream, buffer, offset, length, position) {
            if (position >= prefix.length) throw new FS.ErrnoError(29);
            const count = Math.min(length, prefix.length - position);
            buffer.set(prefix.subarray(position, position + count), offset);
            return count;
        }};
    }
});
#endif
namespace {
// A seekable filebuf model: size includes the tail, reads stop at the prefix
// with ordinary EOF, as libc++/MSVC may do on an underlying read error.
// The unread whitespace is valid, so a read without the fault would succeed.
class ShortFile : public std::streambuf {
public:
    explicit ShortFile(const std::string& prefix)
        : bytes_(prefix + " \n"), limit_(prefix.size()) {}
private:
    std::string bytes_;
    size_t limit_, position_ = 0;
    int_type underflow() override {
        return position_ < limit_ ? traits_type::to_int_type(bytes_[position_]) : traits_type::eof();
    }
    int_type uflow() override {
        const auto value = underflow();
        if (value != traits_type::eof()) ++position_;
        return value;
    }
    std::streamsize xsgetn(char* destination, std::streamsize count) override {
        const size_t available = position_ < limit_ ? limit_ - position_ : 0;
        const size_t size = std::min(size_t(count), available);
        std::copy_n(bytes_.data() + position_, size, destination);
        position_ += size;
        return std::streamsize(size);
    }
    pos_type seekoff(off_type offset, std::ios::seekdir direction, std::ios::openmode) override {
        const off_type base = direction == std::ios::end ? off_type(bytes_.size()) :
                              direction == std::ios::cur ? off_type(position_) : 0;
        if (offset < -base || offset > off_type(bytes_.size()) - base) return pos_type(off_type(-1));
        position_ = size_t(base + offset);
        return pos_type(position_);
    }
    pos_type seekpos(pos_type position, std::ios::openmode mode) override {
        return seekoff(off_type(position), std::ios::beg, mode);
    }
};

// Regular pseudo-file model: readable bytes with zero/unavailable metadata.
class PseudoFile : public std::stringbuf {
public:
    enum class Seek { ZeroSize, NoEnd, NoTell, None };
    PseudoFile(const std::string& text, Seek behavior)
        : std::stringbuf(text, std::ios::in), behavior_(behavior) {}
private:
    Seek behavior_;
    pos_type seekoff(off_type offset, std::ios::seekdir direction,
                     std::ios::openmode mode) override {
        if (behavior_ == Seek::None ||
            (behavior_ == Seek::NoEnd && direction == std::ios::end) ||
            (behavior_ == Seek::NoTell && direction == std::ios::cur))
            return pos_type(off_type(-1));
        // A zero-sized file's end position is its beginning, even with content.
        if (direction == std::ios::end && behavior_ != Seek::NoEnd)
            direction = std::ios::beg;
        return std::stringbuf::seekoff(offset, direction, mode);
    }
};

class GrowingFile : public std::stringbuf {
public:
    explicit GrowingFile(const std::string& text) : std::stringbuf(text, std::ios::in) {}
private:
    pos_type seekoff(off_type offset, std::ios::seekdir direction,
                     std::ios::openmode mode) override {
        return std::stringbuf::seekoff(direction == std::ios::end ? offset - 1 : offset,
                                      direction, mode);
    }
};

// Generate bytes without storing the input. Endless mode throws at a test-only
// cutoff instead of returning EOF, so removing the reader cap cannot run away.
class GeneratedFile : public std::streambuf {
public:
    GeneratedFile(size_t length, bool endless = false, bool sized = false)
        : length_(length), endless_(endless), sized_(sized) {}
    size_t delivered = 0;
private:
    size_t length_;
    bool endless_, sized_;
    std::streamsize xsgetn(char* destination, std::streamsize count) override {
        if (endless_ && size_t(count) > length_ - delivered) throw Cutoff{};
        const size_t size = std::min(size_t(count), length_ - delivered);
        std::fill_n(destination, size, ' ');
        delivered += size;
        return std::streamsize(size);
    }
    int_type underflow() override {
        return endless_ || delivered < length_ ? traits_type::to_int_type(' ') : traits_type::eof();
    }
    pos_type seekoff(off_type offset, std::ios::seekdir direction, std::ios::openmode) override {
        if (offset != 0) return pos_type(off_type(-1));
        if (direction == std::ios::beg) delivered = 0;
        if (direction == std::ios::end) delivered = sized_ ? length_ : 0;
        return pos_type(delivered);
    }
    struct Cutoff {};
};

bool readJson(std::istream& input) {
    std::string bytes;
    if (!trussc::internal::readDocumentBytes(input, bytes)) return false;
    return !nlohmann::json::parse(bytes, nullptr, false).is_discarded();
}
bool readIes(std::istream& input) {
    std::string bytes, error;
    if (!trussc::internal::readDocumentBytes(input, bytes)) return false;
    trussc::internal::IesParseResult result;
    return trussc::internal::parseIes(bytes, result, error);
}
bool readCube(std::istream& input) {
    tcx::lut::detail::CubeData result;
    std::string error;
    std::string bytes;
    if (!trussc::internal::readDocumentBytes(input, bytes)) return false;
    std::istringstream complete(std::move(bytes));
    return tcx::lut::detail::parseCube(complete, result, error);
}
}
int main() {
    const std::string json = R"({"count":42})";
    const std::string ies = "TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0 42\n";
    std::string cube = "LUT_3D_SIZE 2\n";
    for (int i = 0; i < 8; ++i) cube += "0 0 0\n";
    using Reader = bool (*)(std::istream&);
    const Reader readers[] = {readJson, readIes, readCube};
    const std::string inputs[] = {json, ies, cube};
    const char* names[] = {"JSON", "IES", "LUT"};
    int failures = 0;
#ifdef __EMSCRIPTEN__
    installReadErrors();
    const char* paths[] = {"/read.json", "/read.ies", "/read.cube"};
#endif
    for (int i = 0; i < 3; ++i) {
        std::istringstream complete(inputs[i] + " \n");
        ShortFile buffer(inputs[i]);
        std::istream shortRead(&buffer);
        const bool good = readers[i](complete);
        const bool rejected = !readers[i](shortRead);
        printf("%s: complete=%d short read rejected=%d\n", names[i], good, rejected);
        failures += !good || !rejected;
        GrowingFile growing(inputs[i] + " \n");
        std::istream growingStream(&growing);
        const bool growthRejected = !readers[i](growingStream);
        printf("%s: sized file growth rejected=%d\n", names[i], growthRejected);
        failures += !growthRejected;
        for (auto behavior : {PseudoFile::Seek::ZeroSize, PseudoFile::Seek::NoEnd,
                              PseudoFile::Seek::NoTell, PseudoFile::Seek::None}) {
            PseudoFile pseudo(inputs[i], behavior);
            std::istream stream(&pseudo);
            const bool loaded = readers[i](stream);
            printf("%s: pseudo-file mode=%d loaded=%d\n", names[i], int(behavior), loaded);
            failures += !loaded;
        }
#ifdef __EMSCRIPTEN__
        std::ifstream file(paths[i], std::ios::binary);
        const bool eioRejected = !readers[i](file);
        printf("%s: libc++ file read EIO rejected=%d\n", names[i], eioRejected);
        failures += !eioRejected;
#endif
    }
    auto check = [&](const char* label, bool ok) {
        printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
        failures += !ok;
    };
    std::string bytes = "unchanged";
    PseudoFile empty("", PseudoFile::Seek::ZeroSize);
    std::istream emptyStream(&empty);
    check("empty zero-size document reads as empty",
          trussc::internal::readDocumentBytes(emptyStream, bytes) && bytes.empty());
    PseudoFile emptyJson("", PseudoFile::Seek::ZeroSize);
    std::istream emptyJsonStream(&emptyJson);
    check("empty zero-size JSON remains invalid", !readJson(emptyJsonStream));
    const std::string large(3 * 8192 + 7, ' ');
    PseudoFile largeFile(large, PseudoFile::Seek::ZeroSize);
    std::istream largeStream(&largeFile);
    check("zero-size stream retains all chunks exactly",
          trussc::internal::readDocumentBytes(largeStream, bytes) && bytes == large);
    constexpr size_t cap = 64u * 1024u * 1024u;
    GeneratedFile endless(cap + 2 * 8192, true);
    std::istream endlessStream(&endless);
    bytes = "unchanged";
    const char* readError = nullptr;
    peakBufferAllocation = 0;
    trackBufferAllocations = true;
    const bool endlessRejected = !trussc::internal::readDocumentBytes(endlessStream, bytes, &readError);
    trackBufferAllocations = false;
    check("endless unknown-size stream rejects at 64 MiB plus one byte",
          endlessRejected && endless.delivered == cap + 1 && bytes == "unchanged");
    check("unknown-size overflow returns the fixed read error",
          readError && std::string(readError) == "document size unknown and over 64 MiB");
    // Allow library-dependent capacity rounding, but not a doubled buffer.
    printf("endless stream peak buffer allocation: %zu bytes\n", peakBufferAllocation);
    check("endless stream peak buffer allocation stays bounded",
          peakBufferAllocation >= cap && peakBufferAllocation <= cap + cap / 4);

    GeneratedFile exact(cap);
    std::istream exactStream(&exact);
    check("unknown-size stream of exactly 64 MiB succeeds",
          trussc::internal::readDocumentBytes(exactStream, bytes, &readError) &&
          bytes.size() == cap && bytes.find_first_not_of(' ') == std::string::npos && !readError);
    bytes.clear();
    GeneratedFile sizedAboveCap(cap + 1, false, true);
    std::istream sizedStream(&sizedAboveCap);
    check("sized documents above 64 MiB remain uncapped",
          trussc::internal::readDocumentBytes(sizedStream, bytes) && bytes.size() == cap + 1);
    bytes.clear();
    // Exercise the production fallback with a reduced string storage ceiling.
    // No multi-exabyte allocation is needed to verify the ABI bound.
    for (size_t ceiling : {size_t(0), size_t(5), size_t(8192), large.size()}) {
        PseudoFile full(large.substr(0, ceiling), PseudoFile::Seek::ZeroSize);
        std::istream fullStream(&full);
        check("fallback accepts exactly the storage ceiling",
              trussc::internal::readDocumentBytesToEnd(fullStream, bytes, ceiling) &&
              bytes == large.substr(0, ceiling));
        PseudoFile excess(large + " ", PseudoFile::Seek::ZeroSize);
        std::istream excessStream(&excess);
        bytes = "unchanged";
        const bool rejected = !trussc::internal::readDocumentBytesToEnd(excessStream, bytes, ceiling);
        check("fallback rejects overflow without publishing a prefix",
              rejected && bytes == "unchanged");
        check("fallback stops after the first excess byte",
              excessStream.rdbuf()->pubseekoff(0, std::ios::cur, std::ios::in) ==
              std::streampos(ceiling + 1));
    }
    std::istringstream bad("42");
    bad.setstate(std::ios::badbit);
    bytes = "unchanged";
    check("fallback rejects stream errors without publishing bytes",
          !trussc::internal::readDocumentBytesToEnd(bad, bytes, bytes.max_size()) &&
          bytes == "unchanged");
    return failures ? 1 : 0;
}
