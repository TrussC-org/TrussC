# Pixel conversion tests

`allCoreTests simdPixels` compares the standalone `tc/utils/tcSimd.h`
functions with their scalar references. It covers the specified odd counts,
1080p, independent unaligned source/destination offsets, guard bytes,
in-place swizzles, preserved/forced alpha, round trips and BT.601 black/white.
YUYV requires complete four-byte pairs even when the output count is odd.
The swizzle is symmetric, so RGBA→BGRA uses the same dispatch kernel.

The module does not require `TrussC.h`. Web and unsupported architectures
use the scalar implementation (`TRUSSC_SIMD_SCALAR=1` can force it).

Opt-in 1080p benchmark: `allCoreTests simdPixels --bench` or
`simdPixels --bench` (Release build). The normal run is unchanged.
Original origin/main Linux YUYV, Windows grabber (both pitch signs) and recorder
loops are checked byte-for-byte against the replacements before timing.
`swizzle_kernel` measures the shared BGRA/RGBA kernel; `yuyv_kernel` measures
YUYV. Grabber rows include 64 bytes of input padding; recorder rows reverse.
Each site warms up for three frames per path, then interleaves nine pairs of
20-frame before/after batches. Output reports each path's median milliseconds
per frame and their ratio, plus compiler and dispatched Highway target.
There are no timing assertions. Check machine load before measuring and keep
all runs; see [DEVICE_CHECK.md](../../../DEVICE_CHECK.md) for native checks.
