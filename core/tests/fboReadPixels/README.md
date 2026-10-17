# Fbo RGBA8 readback (#875)

Run `allCoreTests fboReadPixels --gpu-check` with a display on GLCORE, GLES3,
D3D11 and Metal. Without the flag, only the CPU conversion checks run.

The GPU checks render negative, HDR and mid-grey values into RGBA16F and
RGBA32F, then check every RGBA8 byte against independent expected values.
They also cover every other renderable TextureFormat, channel expansion,
row order, an odd width, destination bounds and full-precision float reads.
A tall fixture crosses multiple staging chunks and a partial final chunk. On
GLCORE/GLES3, a pending unrelated GL error must neither fail a valid read nor
skip its vertical flip.

Unsupported optional attachment formats (including RGBA32F) print SKIP.
RGBA16F is required: missing render capability fails the GPU lane, so the
half-float regression must actually run. CPU conversions must match exactly;
desktop GL's native conversion has an allowance of one byte value for rounding.
