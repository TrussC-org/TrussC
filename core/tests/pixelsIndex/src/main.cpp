// =============================================================================
// pixelsIndex — Pixels indexes with size_t, so images past INT_MAX bytes work
//
// - internal::pixelOffset() forms the element offset in size_t: the far
//   corner of a 23171x23171 RGBA image is past INT_MAX.
// - internal::pixelBufferCount() checks every product before forming it, so
//   the byte count of Pixels::allocate() cannot wrap. UINT32_MAX stands in
//   for a 32-bit size_t (wasm32), so the 32-bit cases run here too.
// - Pixels::allocate() refuses a size that wraps (or is negative) with an
//   error log and leaves the buffer empty; crop() and Image::allocate() stop
//   there instead of writing into the empty buffer.
// - When the machine has the memory (64-bit, >= 4 GiB available), real
//   buffers just past INT_MAX bytes: getColor() / setColor() at the far
//   corner and past the INT_MAX offset, and halve() reading source pixels
//   past it. Skipped otherwise (says so).
// =============================================================================

#include <TrussC.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

static vector<LogEventArgs> g_logs;
static size_t countErrors(const string& needle) {
    size_t n = 0;
    for (auto& e : g_logs) {
        if (e.level == LogLevel::Error && e.message.find(needle) != string::npos) ++n;
    }
    return n;
}

static const size_t kSizeMax = numeric_limits<size_t>::max();
static const size_t k32Max = 0xFFFFFFFFu;  // a 32-bit size_t's limit

// --- offset helper -----------------------------------------------------------
static void checkOffset() {
    const size_t corner = internal::pixelOffset(23170, 23170, 23171, 4);
    const uint64_t expected = ((uint64_t)23170 * 23171 + 23170) * 4;  // 2147580960
    check("offset: far corner of 23171x23171x4 is past INT_MAX",
          expected > (uint64_t)INT_MAX && (uint64_t)corner == expected, to_string(corner));
    check("offset: (0, 0) is 0", internal::pixelOffset(0, 0, 23171, 4) == 0);
    check("offset: (x, y) is (y * width + x) * channels",
          internal::pixelOffset(3, 2, 10, 3) == (2 * 10 + 3) * 3);
}

// --- byte count helper ---------------------------------------------------------
static void checkBufferCount() {
    size_t n = 7;
    check("count: negative width is refused",
          !internal::pixelBufferCount(-1, 10, 4, 1, kSizeMax, n));
    check("count: negative height is refused",
          !internal::pixelBufferCount(10, -1, 4, 1, kSizeMax, n));
    check("count: negative channels are refused",
          !internal::pixelBufferCount(10, 10, -4, 1, kSizeMax, n));
    check("count: an empty buffer is 0 elements",
          internal::pixelBufferCount(0, 0, 4, 1, kSizeMax, n) && n == 0);
    check("count: w x h x channels elements",
          internal::pixelBufferCount(640, 480, 3, sizeof(float), kSizeMax, n) && n == 640 * 480 * 3);

    // 32-bit size_t
    check("count: 23171x23171x4 U8 (2.15 GB) fits a 32-bit size_t",
          internal::pixelBufferCount(23171, 23171, 4, 1, k32Max, n) && n == (size_t)23171 * 23171 * 4);
    check("count: 23171x23171x4 F32 (8.6 GB) is refused on a 32-bit size_t",
          !internal::pixelBufferCount(23171, 23171, 4, sizeof(float), k32Max, n));
    check("count: 65535x65537x1 U8 (exactly UINT32_MAX bytes) is accepted",
          internal::pixelBufferCount(65535, 65537, 1, 1, k32Max, n) && n == k32Max);
    check("count: 65536x65536x1 U8 (wraps to 0 in 32 bits) is refused",
          !internal::pixelBufferCount(65536, 65536, 1, 1, k32Max, n));
    check("count: 32768x32768x4 U8 (wraps to 0 in 32 bits) is refused",
          !internal::pixelBufferCount(32768, 32768, 4, 1, k32Max, n));
    check("count: 16384x16384x4 F32 (bytes wrap to 0 in 32 bits) is refused",
          !internal::pixelBufferCount(16384, 16384, 4, sizeof(float), k32Max, n));
    check("count: a refused size leaves the output alone", n == k32Max);

    // 64-bit size_t: three int dimensions can still pass 2^64
    check("count: 2^30 x 2^30 x 16 U8 (2^64 bytes, 0 in a 64-bit size_t) is refused",
          !internal::pixelBufferCount(1 << 30, 1 << 30, 16, 1, kSizeMax, n));
    check("count: INT_MAX^3 F32 is refused",
          !internal::pixelBufferCount(INT_MAX, INT_MAX, INT_MAX, sizeof(float), kSizeMax, n));
}

// --- allocate() refuses a wrapping size ------------------------------------------
static void checkAllocateGuard() {
    // 2^30 * 2^30 * 16 is 2^64 bytes: 0 in a 64-bit size_t, and more than a
    // 32-bit one holds.
    const int big = 1 << 30;
    {
        const size_t before = countErrors("cannot allocate");
        Pixels px;
        px.allocate(big, big, 16);
        check("allocate: 2^30 x 2^30 x 16 is refused, buffer empty",
              !px.isAllocated() && px.getData() == nullptr && px.getWidth() == 0 &&
              px.getHeight() == 0 && px.getChannels() == 0 && px.getTotalBytes() == 0);
        check("allocate: the refusal logs an error", countErrors("cannot allocate") == before + 1);
        check("allocate: getColor / setColor on the refused buffer are no-ops",
              (px.setColor(big - 1, big - 1, Color(1, 1, 1, 1)),
               px.getColor(big - 1, big - 1).a == 0.0f));
    }
    {
        const size_t before = countErrors("cannot allocate");
        Pixels px;
        px.allocate(-1, 10, 4);
        check("allocate: a negative width is refused with an error",
              !px.isAllocated() && px.getData() == nullptr &&
              countErrors("cannot allocate") == before + 1);
    }
    {
        // A refused allocate() replaces what was there (it clears first)
        Pixels px;
        px.allocate(2, 2, 4);
        px.allocate(big, big, 16, PixelFormat::U8);
        check("allocate: a refused re-allocate leaves the buffer empty",
              !px.isAllocated() && px.getData() == nullptr && px.getWidth() == 0);
    }
    {
        // crop() into a size that can't be allocated keeps the source
        Pixels px;
        px.allocate(2, 2, 16);
        px.getData()[0] = 42;
        const size_t before = countErrors("cannot allocate");
        px.crop(0, 0, big, big);
        check("crop: a destination that can't be allocated keeps the source",
              px.isAllocated() && px.getWidth() == 2 && px.getHeight() == 2 &&
              px.getChannels() == 16 && px.getData()[0] == 42 &&
              countErrors("cannot allocate") == before + 1);
    }
    {
        // Image::allocate() stops at the refused Pixels (no texture made)
        Image img;
        img.allocate(big, big, 16);
        check("Image::allocate: a refused size leaves the image empty",
              !img.isAllocated() && img.getWidth() == 0 && !img.getTexture().isAllocated());
    }
}

// --- real buffers past INT_MAX bytes -----------------------------------------------

// Physical memory the large buffers can use; 0 when unknown.
static uint64_t availableMemoryBytes() {
#if defined(__EMSCRIPTEN__)
    return 0;
#elif defined(__linux__)
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    unsigned long long kb = 0;
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1) { found = true; break; }
    }
    fclose(f);
    return found ? (uint64_t)kb * 1024 : 0;
#elif defined(__APPLE__)
    uint64_t total = 0;
    size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) != 0) return 0;
    return total / 2;  // total, not free: count on half of it
#elif defined(_WIN32)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (!GlobalMemoryStatusEx(&st)) return 0;
    return st.ullAvailPhys;
#else
    return 0;
#endif
}

static void checkLargeBuffers() {
    const uint64_t kNeed = 4ull << 30;  // 2.15 GB buffer + 0.54 GB halve() output
    const uint64_t avail = availableMemoryBytes();
    if (sizeof(size_t) < 8 || avail < kNeed) {
        printf("%-72s SKIP  -- %zu-bit size_t, %llu MiB available (needs 64-bit, %llu MiB)\n",
               "large: buffers past INT_MAX bytes", sizeof(size_t) * 8,
               (unsigned long long)(avail >> 20), (unsigned long long)(kNeed >> 20));
        fflush(stdout);
        return;
    }

    // RGBA U8 23171x23171: 2,147,580,964 bytes
    {
        const int W = 23171, H = 23171;
        Pixels px;
        px.allocate(W, H, 4);
        check("large: 23171x23171 RGBA allocates", px.isAllocated() && px.getTotalBytes() == (size_t)W * H * 4);
        if (px.isAllocated()) {
            const unsigned char* d = px.getData();
            const size_t corner = ((size_t)(H - 1) * W + (W - 1)) * 4;
            px.setColor(W - 1, H - 1, Color(1.0f, 0.5f, 0.25f, 0.75f));
            check("large: setColor at the far corner writes past INT_MAX",
                  corner > (size_t)INT_MAX && d[corner] == 255 && d[corner + 1] == 127 &&
                  d[corner + 2] == 63 && d[corner + 3] == 191);
            Color c = px.getColor(W - 1, H - 1);
            check("large: getColor at the far corner reads it back",
                  c.r == 1.0f && c.g == 127 / 255.0f && c.b == 63 / 255.0f && c.a == 191 / 255.0f);
            check("large: the pixel before the far corner is untouched",
                  px.getColor(W - 2, H - 1).a == 0.0f && d[corner - 1] == 0);

            // The first pixel whose offset is past INT_MAX
            const size_t p = ((size_t)INT_MAX + 1) / 4;  // pixel index, offset 2^31
            const int x = (int)(p % W), y = (int)(p / W);
            px.setColor(x, y, Color(0.0f, 1.0f, 0.0f, 1.0f));
            check("large: set/get at offset 2^31",
                  d[p * 4] == 0 && d[p * 4 + 1] == 255 && d[p * 4 + 3] == 255 &&
                  px.getColor(x, y).g == 1.0f && px.getColor(x, y).a == 1.0f);
            check("large: (0, 0) stays clear", px.getColor(0, 0).a == 0.0f);
        }
    }

    // 2-channel U8 32769x32768: 2,147,549,184 bytes. halve() reads source
    // pixels past INT_MAX for the last output pixels (2 channels are linear,
    // so the average is exact and cheap).
    {
        const int W = 32769, H = 32768, CH = 2;
        Pixels px;
        px.allocate(W, H, CH);
        check("large: 32769x32768 2-channel allocates", px.isAllocated());
        if (px.isAllocated()) {
            unsigned char* d = px.getData();
            auto at = [&](int x, int y) { return ((size_t)y * W + x) * CH; };
            // Output (16383, 16383) averages source (32766..32767, 32766..32767)
            d[at(32766, 32766)] = 40;  d[at(32767, 32766)] = 80;
            d[at(32766, 32767)] = 120; d[at(32767, 32767)] = 160;
            d[at(32767, 32767) + 1] = 4;
            px.halve();
            const unsigned char* h = px.getData();
            const size_t last = ((size_t)16383 * 16384 + 16383) * CH;
            check("large: halve() is 16384x16384",
                  px.getWidth() == 16384 && px.getHeight() == 16384 && px.getChannels() == CH);
            check("large: halve() averages source pixels past INT_MAX",
                  at(32767, 32767) > (size_t)INT_MAX && h[last] == 100 && h[last + 1] == 1,
                  to_string(h[last]) + ", " + to_string(h[last + 1]));
            check("large: halve() leaves the first output pixel clear", h[0] == 0 && h[1] == 0);
        }
    }
}

int main() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) { g_logs.push_back(e); });

    printf("sizeof(size_t) = %zu\n", sizeof(size_t));
    checkOffset();
    checkBufferCount();
    checkAllocateGuard();
    checkLargeBuffers();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
