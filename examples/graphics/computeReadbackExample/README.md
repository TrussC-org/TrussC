# computeReadbackExample

Counts values >= 64 on the GPU and displays 64 using `read()`.

`read()` is native only and blocks; [`readAsync()`](../computeReadbackAsyncExample) works everywhere, including WebGPU, with a callback in a later update.

On Web this sample displays “Synchronous readback is unavailable on Web”.
