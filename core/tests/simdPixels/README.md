# Pixel conversion tests

`allCoreTests simdPixels` compares the standalone `tc/utils/tcSimd.h`
functions with their scalar references. It covers the specified odd counts,
1080p, independent unaligned source/destination offsets, guard bytes,
in-place swizzles, preserved/forced alpha, round trips and BT.601 black/white.
YUYV requires complete four-byte pairs even when the output count is odd.
The swizzle is symmetric, so RGBA→BGRA uses the same dispatch kernel.

The module does not require `TrussC.h`. Web and unsupported architectures
use the scalar implementation (`TRUSSC_SIMD_SCALAR=1` can force it).

Opt-in benchmark: `allCoreTests simdPixels --bench` or
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

Part 2 adds separate rows for the macOS and iOS grabber/player/recorder and
Windows player staging/source-reader paths (both source stride signs). The
original platform row loops are retained in `src/originalPixels.h`; the Apple
replacement row loops have SDK-free copies in `src/sitePixels.h`, while Windows
executes the production `tcVideoPlayerPixels.h` helpers. Normal tests also check
odd widths around vector boundaries, independent unaligned pointers, input and
output padding, preserved/forced alpha, cropped player columns/untouched rows,
and negative source-reader stride against those original loops. Apple recorder
rows retain their existing top-down order. Native checks (#1408) kept tcSimd at
the Apple grabbers and both Windows player paths. Apple recorder loops remain
scalar. #1513 restores per-row SIMD in the Apple video
players. `apple_video_bgra` and `apple_video_bgra_4k` measure their exact original
scalar loop against the SIMD row call at 1920x1080 and 3840x2160, with 64 bytes
of source padding and tightly packed output. The normal test also checks both
Apple players with smaller/larger destination dimensions, odd widths, byte-level
source padding, unaligned pointers, preserved alpha and retained margins.

Windows source-reader tests also compare offset apertures against the original
cropped extraction loop: 642x362 visible in 656x368 coded, X/Y offsets (7,3)
and (14,6), both stride signs, row padding, and unaligned buffers. Smaller crop
widths cover vector boundaries. The two `crop_642x362` benchmark rows use the
(7,3) aperture and check equality before timing, like the full-frame rows.
