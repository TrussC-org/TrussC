#include "GeneratedFile.h"
#include "TrussC.h"
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

    std::filesystem::path resolved;
    auto result = tc::internal::prepareOutputPath(
        path.empty() ? path : std::filesystem::absolute(path), resolved, nullptr);
    if (result) {
        result = tc::internal::atomicWrite(resolved, bytes.data(), bytes.size(), nullptr);
    }
    if (!result) throw std::runtime_error(result.message);
}
