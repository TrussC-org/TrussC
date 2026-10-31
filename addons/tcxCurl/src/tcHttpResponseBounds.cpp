#include "tcHttpResponseBounds.h"
#include "TrussC.h" // definitions used by platform logging/file helpers
#include <array>
#include <atomic>

namespace tcx::curl::detail {
namespace {
// Owning addon-module lifetime, cumulative across clients/TUs and retries;
// never reset on query. A newly loaded addon module starts a new lifetime.
std::array<std::atomic<size_t>, static_cast<size_t>(ResponseLoss::Count)> losses{};
}

void reportResponseLoss(ResponseLoss kind, size_t needed, size_t capacity, const char* units) {
    if (losses[static_cast<size_t>(kind)].fetch_add(1) == 0) {
        if (kind == ResponseLoss::HeaderSyntax) {
            trussc::logWarning("tcxCurl") << "ambiguous HTTP response trailer rejected (one whole response lost)";
            return;
        }
        if (kind == ResponseLoss::BackendHeader) {
            trussc::logWarning("tcxCurl") << "backend header limit or allocation failure awaiting response headers (one whole response lost)";
            return;
        }
        const char* store = kind == ResponseLoss::Body ? "response body" :
                            kind == ResponseLoss::Metadata ? "response metadata" :
                            kind == ResponseLoss::Redirects ? "redirect count" :
                            "redirect policy";
        trussc::logWarning("tcxCurl") << store << " rejected: capacity " << capacity
            << ", needed at least " << needed << " " << units << " (one whole unit lost)";
    }
}
void reportInvalidResponseLimit() {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true))
        trussc::logWarning("tcxCurl") << "setResponseLimit: 0 is invalid; keeping the previous limit";
}
void reportInvalidRequestHeader(RequestHeaderWarning& warning, const char* reason) {
    // Settle the client gate before synchronous logger listeners run.
    if (warning.issued.exchange(true)) return;
    trussc::logWarning("tcxCurl") << reason << "; request rejected before sending";
}
size_t responseLossCountForTests(ResponseLoss kind) {
    return losses[static_cast<size_t>(kind)].load();
}
nlohmann::json parseResponseJson(const std::string& body) {
    if (body.empty()) return nlohmann::json::object();
    try { return nlohmann::json::parse(body); }
    catch (const std::exception&) { return nlohmann::json::object(); }
}
} // namespace tcx::curl::detail
