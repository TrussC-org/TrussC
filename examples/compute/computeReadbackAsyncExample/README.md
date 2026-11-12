# computeReadbackAsyncExample

Counts values >= 64 on the GPU and displays 64 using `readAsync()` and `result.get<uint32_t>(0)`.

[`read()`](../computeReadbackExample) is native only and blocks; `readAsync()` works everywhere, including WebGPU, with a callback in a later update.
