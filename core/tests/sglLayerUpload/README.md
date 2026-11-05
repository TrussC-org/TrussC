# sglLayerUpload

Standalone C test using `SOKOL_DUMMY_BACKEND`, with sokol validation enabled
also in Release builds. It uses a plain `main()` and does not link TrussC.

The historical regression uploaded the entire recorded vertex set for every
layer, multiplying upload bytes by the layer count and exhausting GPU storage.
The e7a48ed6 integration uses write-transient buffers: unchanged layer draws
share one upload; changed vertex sets and context resets select a separate
buffer within the device frame. Buffers are reused after `sg_commit()`.

The test checks upload bytes through trace hooks, buffer capacity, re-upload
on the next device frame, distinct buffers after a same-frame reset, and zero
sokol validation errors. Capacity-only dummy probes also check that doubled CPU
capacity never creates a negative backend size and oversized requests fail
without allocating large storage. See modification L10 in
`core/include/sokol/TRUSSC_MODIFICATIONS.md`.

Run through the core-test build, or build this standalone CMake project and
run its `sglLayerUpload` executable. No display is required.

For pixels, run `allCoreTests textureUploadGuard --gpu-check` under Xvfb/Mesa
on Linux. It covers multiple sgl flushes across nested Fbo suspend/resume and
immediate texture updates. `renderRegression` provides broader record/compare
coverage. Metal, D3D11 and GLES3 still need their real-device checks after an
upstream update.
