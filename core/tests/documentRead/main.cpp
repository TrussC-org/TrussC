// Filebuf may report an I/O error as EOF. Never parse a short, valid prefix.
#include "nlohmann/json.hpp"
#include "impl/tcDocumentInput.h"
#include "tc/3d/tcIesParse.h"
#include "tcLutParse.h"
#include <sstream>
#include <cstdio>
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
        FS.writeFile(path, inputs[path] + 'unread-invalid-tail');
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
class ShortFile : public std::streambuf {
public:
    explicit ShortFile(const std::string& prefix)
        : bytes_(prefix + "unread-invalid-tail"), limit_(prefix.size()) {}
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
        std::istringstream complete(inputs[i]);
        ShortFile buffer(inputs[i]);
        std::istream shortRead(&buffer);
        const bool good = readers[i](complete);
        const bool rejected = !readers[i](shortRead);
        printf("%s: complete=%d short read rejected=%d\n", names[i], good, rejected);
        failures += !good || !rejected;
#ifdef __EMSCRIPTEN__
        std::ifstream file(paths[i], std::ios::binary);
        const bool eioRejected = !readers[i](file);
        printf("%s: libc++ file read EIO rejected=%d\n", names[i], eioRejected);
        failures += !eioRejected;
#endif
    }
    return failures ? 1 : 0;
}
