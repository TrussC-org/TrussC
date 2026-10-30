# MCP capture and allocation checks

The own-binary test injects one-shot allocation failures in pixel/JSON construction
and deferred main/worker stages, then checks that another request succeeds.
Linux uses the existing framebuffer link wrappers to save a 3840×2160 screenshot
through the MCP queue, production readback and PNG encoder without a display.
It also analyzes a 2304×2304 local image through the standard Pixels loader.
Temporary PNG files are removed after the test.

Follow-up regressions fail each deferred partition reserve before any entries
move, retain pending producers/promises, retry, and assert numeric/string IDs on
allocation errors. Linux malloc wrappers fail filtered-pixel, line-storage and final-PNG allocations;
operator new fails the C++ output copy after encoding. Compressor-internal growth
failures are excluded because stb_image_write can abort there under real memory
exhaustion. Its final-buffer failure also leaks the compressed temporary upstream.
Uniform/random PNGs preserve decoded pixel bytes. Production HTTP workers survive failures during initial parsing and
response writing (outside upstream's routing catch).
