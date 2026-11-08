# StorageBuffer CPU readback feasibility (#1281)

This is a follow-up design note, with no readback implementation or promised API.
The owner needs compute-only results on the CPU. GPU initialization still requires
a windowed runtime; adding readback would not make current headless apps compute-capable.
The assessment below comes from the pinned `core/include/sokol/sokol_gfx.h`.
No sokol patch is needed for the native handles or copies described here.

## Proposed API shape

Prefer an asynchronous callback everywhere, returning owned bytes and an error:

```cpp
// Sketch only, not implemented. Callback runs on the main thread.
buffer.readAsync([](ReadbackResult result) {
    if (result.ok()) consume(result.bytes());
});
```

A native-only synchronous `bool read(void* destination, size_t bytes)` could be a
small later convenience. It would suspend drawing and wait for GPU completion,
so it must explicitly document stalls and would be unavailable on browser WebGPU.
An async-everywhere shape avoids changing program structure when moving to Web.
Snapshot the requested range at the call, retain GPU/staging resources until
completion, deliver errors on device loss, and cancel safely when the app closes.
Callbacks should not borrow the original StorageBuffer or application memory.
Polling completion belongs in the existing main-thread update path. Do not wait
on the browser main thread, including by wrapping mapAsync in a blocking loop.

## Backend feasibility

| Backend | Existing native query and route | Estimate, including tests |
| --- | --- | --- |
| Metal | `sg_mtl_query_buffer_info(buffer)` gives `buf[active_slot]`. Query storage mode. Shared memory can expose `contents` after preceding commands complete. Managed storage also needs a blit `synchronizeResource` before CPU reads. Private memory (for example an injected buffer) needs a blit into a new shared MTLBuffer. | 1–2 days native sync; 2–3 days async with main-thread delivery and lifetime tests. |
| D3D11 | `sg_d3d11_query_buffer_info(buffer).buf`, `sg_d3d11_device()` and `sg_d3d11_device_context()`. Create a same-sized staging buffer with `D3D11_USAGE_STAGING`, `D3D11_CPU_ACCESS_READ`, no bind flags, then `CopyResource` and `Map(D3D11_MAP_READ)`/copy/`Unmap`. | 1–2 days sync; 2–3 days async using an event query before mapping. |
| GL / GLES | `sg_gl_query_buffer_info(buffer)` gives `buf[active_slot]`. After flushing TrussC recordings, bind this buffer; GL 4.3+ can use `glGetBufferSubData`, GLES 3.1 can use `glMapBufferRange(GL_MAP_READ_BIT)` and `glUnmapBuffer`. Issue `glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT)` after compute writes; use a fence and flush/poll for async completion. | 1–2 days sync across GL/GLES; 2–3 days async with a staging copy and fence, plus a Pi device check. |
| WebGPU | `sg_wgpu_query_buffer_info(buffer).buf`, `sg_wgpu_device()`, `sg_wgpu_queue()` and `sg_wgpu_command_encoder()`. Copy into a native buffer created with `COPY_DST | MAP_READ`, submit the encoder, then `mapAsync(READ)`; copy mapped bytes into owned memory and unmap. | 2–3 days for async browser integration, callback/error handling and tests; add device/browser validation time. |

The current Metal buffer allocator selects shared or managed storage, not private
storage, and `sg_mtl_buffer_info.active_slot` identifies any renamed resource.
Never infer CPU visibility from the handle alone. Encode a shared-buffer blit on
the same queue after the compute prefix. Completion handlers can queue delivery
to the main thread; a synchronous implementation waits only after submission.
The existing `sg_tc_mtl_command_buffer`/`sg_tc_mtl_flush` TrussC seams can order
against the current command buffer without introducing another sokol patch.

D3D11 `Map` can block until the copy is complete. An async version can issue an
event query and poll `GetData` before mapping. End active passes/unbind conflicting
views through sokol and restore its state cache when mixing native calls.

GL/GLES readback must restore the native binding and reset sokol's state cache.
Direct reads/maps may stall; for async results, copy the requested range into an
owned staging GL buffer before placing the fence, so later compute writes cannot
change the requested snapshot. Check mapping failure and failed waits, and delete
staging buffers and fences only after their work completes.

WebGPU storage cannot also be mapped for reading. More critically, the current
StorageBuffer creates storage with `STORAGE | COPY_DST`, not `COPY_SRC`; WebGPU
cannot add usage flags after creation. A follow-up must add sokol's existing
`.usage.copy_src` at allocation (or define an explicit allocation option) before
copying out. Native-handle queries do not bypass that requirement. Map offsets
must be 8-byte aligned and mapped sizes multiples of 4. Submit through the normal
frame boundary so sokol retains encoder ownership; a request can encode its copy
immediately between passes but begin mapping after submission. A callback can
arrive later than the next frame. Handle rejected map requests, device loss,
shutdown and resource lifetime without keeping the main thread blocked.

## Main risks and verification

The main risks are reading before submitted compute completes, capturing a later
write instead of the requested snapshot, stalls from synchronous reads, native
state leaking into later draws, and callbacks outliving application objects.
Byte ranges need overflow-safe bounds checks and shader/CPU layout agreement.
Cross-window work shares a device frame and queue. An async API needs a clear
shutdown/cancellation contract; avoid adding a general GPU job scheduler.

Test known integer/float patterns, a compute → read → later-write snapshot,
partial ranges and alignment errors, destruction after a read request, shutdown,
and graphics after readback. Verify Metal on a Mac (shared and managed/private
routes where available), D3D11 on Windows, GLES on Pi 5, and WebGPU in a browser.
These estimates are independent follow-up slices; implementing all backends in
one two-day change would be too large.
