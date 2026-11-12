# computeReadbackAsyncExample

Adds 1..100 on the GPU, one number per thread, and displays 5050 using `readAsync()` and `result.get<uint32_t>(0)`.

[`read()`](../computeReadbackExample) is native only and blocks; `readAsync()` works everywhere, including WebGPU, with a callback in a later update.
