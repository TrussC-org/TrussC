#include "tcHttpResponseBounds.h"
#include "TrussC.h" // definitions used by platform logging/file helpers
#include <array>
#include <atomic>
#include <limits>
#include <new>
#include <cmath>

namespace tcx::curl::detail {
namespace {
// Owning addon-module lifetime, cumulative across clients/TUs and retries;
// never reset on query. A newly loaded addon module starts a new lifetime.
std::array<std::atomic<size_t>, static_cast<size_t>(ResponseLoss::Count)> losses{};

struct JsonWorkingBudget {
    size_t live = 0, limit;
};
thread_local JsonWorkingBudget* activeJsonBudget = nullptr;
struct JsonBudgetScope {
    JsonWorkingBudget* previous;
    explicit JsonBudgetScope(JsonWorkingBudget& budget) : previous(activeJsonBudget) { activeJsonBudget = &budget; }
    ~JsonBudgetScope() { activeJsonBudget = previous; }
};
// Every DOM container/reallocation is admitted including old+new arrays and
// allocator bookkeeping. Strings/lexer and the public result use the separate
// conservative reservation computed by preflight below. No custom DOM escapes scope.
template<class T> struct JsonAllocator {
    using value_type = T;
    JsonAllocator() = default;
    template<class U> JsonAllocator(const JsonAllocator<U>&) {}
    struct alignas(std::max_align_t) Header { size_t bytes; JsonWorkingBudget* budget; };
    T* allocate(size_t count) {
        if (!activeJsonBudget || count > (SIZE_MAX - sizeof(Header) - 32) / sizeof(T)) throw std::bad_alloc();
        const size_t bytes = count * sizeof(T) + sizeof(Header) + 32;
        if (bytes > activeJsonBudget->limit - activeJsonBudget->live) throw std::bad_alloc();
        auto* header = static_cast<Header*>(::operator new(bytes));
        header->bytes = bytes;
        header->budget = activeJsonBudget;
        activeJsonBudget->live += bytes;
        return reinterpret_cast<T*>(header + 1);
    }
    void deallocate(T* pointer, size_t) noexcept {
        auto* header = reinterpret_cast<Header*>(pointer) - 1;
        header->budget->live -= header->bytes;
        ::operator delete(header);
    }
    template<class U> bool operator==(const JsonAllocator<U>&) const { return true; }
    template<class U> bool operator!=(const JsonAllocator<U>&) const { return false; }
};
using BudgetJson = nlohmann::basic_json<std::map, std::vector, std::string, bool,
    std::int64_t, std::uint64_t, double, JsonAllocator>;

// A lexer-only grammar walk precedes DOM construction. Unlike sax_parse(),
// this path never formats a server-controlled token into a syntax exception.
// Reserve strings/lexer/result copies plus conservative per-node space; the
// remaining credits admit the private DOM's actual container allocations.
struct JsonAdmission {
    size_t nodes = 0, working;
    bool exceeded = false;
    size_t rejectedNeeded = 0, rejectedCapacity = 0;
    const char* units = "bytes";
    explicit JsonAdmission(size_t bytes) : working(bytes * 4 + 65536) {}
    bool reject(size_t needed, size_t capacity, const char* unit) {
        exceeded = true; rejectedNeeded = needed; rejectedCapacity = capacity; units = unit;
        return false;
    }
    bool node() {
        if (++nodes > jsonNodeLimit) return reject(nodes, jsonNodeLimit, "nodes");
        if (256 > jsonWorkingLimit - working) return reject(working + 256, jsonWorkingLimit, "bytes");
        working += 256;
        return true;
    }
    template<class Lexer> bool value(Lexer& lexer, typename Lexer::token_type& token, size_t depth) {
        using Token = typename Lexer::token_type;
        if (!node()) return false;
        if (token == Token::begin_object || token == Token::begin_array) {
            if (++depth > jsonDepthLimit) return reject(depth, jsonDepthLimit, "levels");
            const bool object = token == Token::begin_object;
            const Token end = object ? Token::end_object : Token::end_array;
            token = lexer.scan();
            if (token != end) {
                for (;;) {
                    if (object) {
                        if (token != Token::value_string || !node()) return false;
                        if (lexer.scan() != Token::name_separator) return false;
                        token = lexer.scan();
                    }
                    if (!value(lexer, token, depth)) return false;
                    if (token != Token::value_separator) break;
                    token = lexer.scan();
                }
                if (token != end) return false;
            }
        } else {
            switch (token) {
                case Token::literal_null: case Token::literal_true: case Token::literal_false:
                case Token::value_string: case Token::value_integer: case Token::value_unsigned: break;
                case Token::value_float: if (!std::isfinite(lexer.get_number_float())) return false; break;
                default: return false;
            }
        }
        token = lexer.scan();
        return true;
    }
};

bool preflightJson(const std::string& body, JsonAdmission& admission) {
    auto adapter = nlohmann::detail::input_adapter(body);
    using Lexer = nlohmann::detail::lexer<nlohmann::json, decltype(adapter)>;
    Lexer lexer(std::move(adapter));
    auto token = lexer.scan();
    return admission.value(lexer, token, 0) && token == Lexer::token_type::end_of_input;
    // Lexer buffers are destroyed before either DOM/result is constructed.
}
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
                            kind == ResponseLoss::RedirectPolicy ? "redirect policy" : "response JSON";
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
    if (body.size() > jsonByteLimit) {
        reportResponseLoss(ResponseLoss::Json, body.size(), jsonByteLimit);
        return nlohmann::json::object();
    }
    try {
        JsonAdmission admission(body.size());
        if (!preflightJson(body, admission)) {
            if (admission.exceeded) reportResponseLoss(ResponseLoss::Json, admission.rejectedNeeded, admission.rejectedCapacity, admission.units);
            return nlohmann::json::object();
        }
        JsonWorkingBudget budget{0, jsonWorkingLimit - admission.working};
        JsonBudgetScope scope(budget);
        auto parsed = BudgetJson::parse(body);
        return nlohmann::json(parsed);
    } catch (const std::bad_alloc&) {
        reportResponseLoss(ResponseLoss::Json, jsonWorkingLimit + 1, jsonWorkingLimit);
        return nlohmann::json::object();
    } catch (...) { return nlohmann::json::object(); }
}
} // namespace tcx::curl::detail
