#include "GeneratedFile.h"
#include "TrussC.h"
#include <cctype>
#include <stdexcept>

void writeGeneratedFile(const std::filesystem::path& path, const std::string& text) {
    std::string bytes;
    bytes.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            bytes += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            bytes += text[i];
        }
    }

    // Batch files keep CRLF (.gitattributes: "*.bat text eol=crlf"); cmd.exe
    // misreads labels and goto in LF-only scripts.
    auto extension = path.extension().string();
    for (auto& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (extension == ".bat" || extension == ".cmd") {
        std::string crlf;
        crlf.reserve(bytes.size() + bytes.size() / 16);
        for (char ch : bytes) {
            if (ch == '\n') crlf += '\r';
            crlf += ch;
        }
        bytes.swap(crlf);
    }

    std::filesystem::path resolved;
    auto result = tc::internal::prepareOutputPath(
        path.empty() ? path : std::filesystem::absolute(path), resolved, nullptr);
    if (result) {
        result = tc::internal::atomicWrite(resolved, bytes.data(), bytes.size(), nullptr);
    }
    if (!result) throw std::runtime_error(result.message);
}
