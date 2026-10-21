#include "tc/utils/tcCompress.h"
#include "tc/utils/tcLog.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#if defined(__linux__) && SIZE_MAX > UINT32_MAX
#include <sys/mman.h>
#endif
namespace {
size_t largestRequest = 0;
size_t watchedSize = 0, watchedRequests = 0;
bool track = false, failNext = false, failEveryAllocation = false;
}
void* operator new(size_t bytes) {
    if (track && bytes > largestRequest) largestRequest = bytes;
    if (track && bytes == watchedSize) ++watchedRequests;
    if (failNext || failEveryAllocation) { failNext = false; throw std::bad_alloc(); }
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, size_t) noexcept { std::free(ptr); }
int main() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) { std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL"); failures += !ok; };
    std::vector<uint8_t> out, encoded;
    const std::string input(100000, 'q');
    check("encode", trussc::compress(input.data(), input.size(), encoded, trussc::Codec::LZ4));
    check("decode complete", trussc::decompress(encoded.data(), encoded.size(), out, input.size(), trussc::Codec::LZ4) &&
          std::string(out.begin(), out.end()) == input);
#if SIZE_MAX > UINT32_MAX && !defined(__EMSCRIPTEN__)
    // Reserve a real destination range; only the decoded bytes are touched.
    // Avoid a multi-GiB Web heap or 32-bit address-space requirement.
    const size_t largeCapacity = size_t(std::numeric_limits<int>::max()) + 1;
    if (void* destination = std::malloc(largeCapacity)) {
        const int result = trussc::decompress(encoded.data(), encoded.size(), destination, largeCapacity, trussc::Codec::LZ4);
        check("raw LZ4 clamps capacity above INT_MAX", result == static_cast<int>(input.size()) &&
              std::memcmp(destination, input.data(), input.size()) == 0);
        std::free(destination);
    } else {
        std::puts("raw LZ4 clamps capacity above INT_MAX: SKIP (virtual range unavailable)");
    }
#else
    std::puts("raw LZ4 clamps capacity above INT_MAX: SKIP (requires native 64-bit address space)");
#endif
    auto reject = [&](const void* data, size_t size, size_t outputSize, trussc::Codec codec) {
        std::vector<uint8_t>{1, 2, 3}.swap(out);
        largestRequest = 0; watchedSize = outputSize; watchedRequests = 0; track = true;
        bool result = trussc::decompress(data, size, out, outputSize, codec);
        track = false;
        return !result && out.empty() && out.capacity() == 0 && largestRequest < 4096 && watchedRequests == 0;
    };
    check("declared output inconsistent with encoded block before allocation", reject(encoded.data(), encoded.size(), 256u*1024u*1024u+1, trussc::Codec::LZ4));
    check("input length before integer narrowing/allocation", reject(encoded.data(), size_t(std::numeric_limits<int>::max())+1, 100000, trussc::Codec::LZ4));
    check("mismatched decoded size before allocation", reject(encoded.data(), encoded.size(), 32u*1024u*1024u, trussc::Codec::LZ4));
    check("identity size mismatch before allocation", reject("x", 1, 32u*1024u*1024u, trussc::Codec::None));
    check("incomplete block before allocation", reject(encoded.data(), 1, 32u*1024u*1024u, trussc::Codec::LZ4));
    {
        constexpr size_t decoded = 64u*1024u*1024u;
        std::vector<uint8_t> block{0x1f, 'A', 1, 0};
        size_t extension = decoded - 1 - 4 - 15;
        while (extension >= 255) { block.push_back(255); extension -= 255; }
        block.push_back(static_cast<uint8_t>(extension));
        block.push_back(0); // Final sequence has no literals.
        check("LZ4 missing literal tail before allocation", reject(block.data(), block.size(), decoded, trussc::Codec::LZ4));
        block.back() = 0x50;
        block.insert(block.end(), 5, 'A');
        check("LZ4 five-literal tail decodes", trussc::decompress(block.data(), block.size(), out, decoded + 5, trussc::Codec::LZ4) &&
              out.size() == decoded + 5 && std::all_of(out.begin(), out.end(), [](uint8_t value) { return value == 'A'; }));
        const uint8_t nearEnd[]{0x12, 'A', 1, 0, 0x50, 'A', 'A', 'A', 'A', 'A'};
        check("LZ4 match starts too near end before allocation", reject(nearEnd, sizeof(nearEnd), 12, trussc::Codec::LZ4));
        const uint8_t atLimit[]{0x13, 'A', 1, 0, 0x50, 'A', 'A', 'A', 'A', 'A'};
        check("LZ4 match starts exactly twelve bytes before end", trussc::decompress(atLimit, sizeof(atLimit), out, 13, trussc::Codec::LZ4) &&
              out == std::vector<uint8_t>(13, 'A'));
        bool shortLiterals = true;
        for (size_t size = 0; size <= 5; ++size) {
            std::vector<uint8_t> literalBlock(size + 1, 'A');
            literalBlock[0] = static_cast<uint8_t>(size << 4);
            shortLiterals &= trussc::decompress(literalBlock.data(), literalBlock.size(), out, size, trussc::Codec::LZ4) &&
                out == std::vector<uint8_t>(size, 'A');
        }
        check("LZ4 short literal-only blocks remain valid", shortLiterals);
    }
    std::vector<uint8_t>().swap(out);
    failNext = true;
    bool allocationResult = true;
    try { allocationResult = trussc::decompress(encoded.data(), encoded.size(), out, input.size(), trussc::Codec::LZ4); }
    catch (...) { check("allocation failure contained", false); }
    failNext = false;
    check("allocation failure returns empty failure", !allocationResult && out.empty());
    std::vector<uint8_t>().swap(out);
    failEveryAllocation = true;
    bool persistentResult = true, persistentThrew = false;
    try { persistentResult = trussc::decompress(encoded.data(), encoded.size(), out, input.size(), trussc::Codec::LZ4); }
    catch (...) { persistentThrew = true; }
    failEveryAllocation = false;
    check("persistent allocation failure and reporting are contained", !persistentResult && !persistentThrew && out.empty());
    check("next valid load recovers", trussc::decompress(encoded.data(), encoded.size(), out, input.size(), trussc::Codec::LZ4));
    check("empty identity input", trussc::decompress(nullptr, 0, out, 0, trussc::Codec::None) && out.empty());
    const std::vector<uint8_t> identitySource(1500, 23);
    std::vector<uint8_t>(1024, 0).swap(out);
    largestRequest = 0; track = true;
    bool growthResult = trussc::decompress(identitySource.data(), identitySource.size(), out, identitySource.size(), trussc::Codec::None);
    track = false;
    check("reused output grows without capacity overshoot", growthResult && out.size() == identitySource.size() && largestRequest <= identitySource.size());
#if defined(__linux__) && SIZE_MAX > UINT32_MAX
    {
        // A real, demand-zero source range avoids storing a multi-GiB fixture.
        // Only the output needs physical memory; release it before other checks.
        const size_t size = size_t(std::numeric_limits<int>::max()) + 1;
        auto* source = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (source != MAP_FAILED) {
            source[0] = 17;
            source[size - 1] = 29;
            const bool result = trussc::decompress(source, size, out, size, trussc::Codec::None);
            check("identity above INT_MAX copies completely", result && out.size() == size &&
                  std::memcmp(out.data(), source, size) == 0);
            munmap(source, size);
            std::vector<uint8_t>().swap(out);
        } else {
            std::puts("identity above INT_MAX copies completely: SKIP (virtual range unavailable)");
        }
    }
#else
    std::puts("identity above INT_MAX copies completely: SKIP (requires Linux 64-bit mapped range)");
#endif
    // Release each large fixture before the next: peak is about 600 MiB.
    constexpr size_t largeSize = 300u*1024u*1024u;
    {
        std::vector<uint8_t> source(largeSize, 17);
        check("identity 300 MiB succeeds", trussc::decompress(source.data(), source.size(), out, source.size(), trussc::Codec::None) && out == source);
        std::vector<uint8_t>().swap(out);
        check("encode 300 MiB", trussc::compress(source.data(), source.size(), encoded, trussc::Codec::LZ4));
    }
    trussc::getLogger().notices = 0;
    check("LZ4 300 MiB succeeds completely", trussc::decompress(encoded.data(), encoded.size(), out, largeSize, trussc::Codec::LZ4) &&
          out.size() == largeSize && std::all_of(out.begin(), out.end(), [](uint8_t value) { return value == 17; }));
    check("large LZ4 emits exactly one Notice", trussc::getLogger().notices == 1);
    std::vector<uint8_t>().swap(out);
    // Generated deterministic blocks exercise literal/match length extensions.
    for (size_t size = 0; size < 16000; size += 127) {
        std::string generated(size, ' ');
        for (size_t i = 0; i < size; ++i) generated[i] = static_cast<char>((i*i + i/17) % 251);
        if (!trussc::compress(generated.data(), size, encoded, trussc::Codec::LZ4) ||
            !trussc::decompress(encoded.data(), encoded.size(), out, size, trussc::Codec::LZ4) ||
            std::string(out.begin(), out.end()) != generated) { check("generated round trips", false); break; }
    }
    return failures ? 1 : 0;
}
