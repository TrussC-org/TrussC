# computeReadbackExample

Adds 1..100 on the GPU, one number per thread, and displays 5050 using `read()`.

`read()` is native only and blocks; [`readAsync()`](../computeReadbackAsyncExample) works everywhere, including WebGPU, with a callback in a later update.

On Web this sample displays “Synchronous readback is unavailable on Web”.
