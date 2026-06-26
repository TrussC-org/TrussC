# Coverage wishlist — interop accessors that deserve a wrapped public API

These symbols are currently **excluded** from the curated reference because they
return raw interop handles (sokol `sg_*`) — see `coverage.excluded_return_prefixes`
in `api-definition.yaml`. The exclusion keeps `sg_*` types out of the user-facing
docs, but some of these are genuinely useful and *should* be public once given a
properly-wrapped (non-sokol) form. Track candidates here; add the wrapped method
to core, then document it normally (the raw `sg_*` accessor stays excluded).

## Candidates for a wrapped public API

| symbol | returns | why it'd be useful | wrap idea |
|---|---|---|---|
| `Fbo::getColorImage()` | `sg_image` | read/share an FBO's rendered color attachment | expose as a `Texture` (already a wrapper type) — e.g. `Fbo::getTexture()` |
| `Mesh::getGpuVertexBuffer()` / `getGpuIndexBuffer()` | `sg_buffer` | hand a mesh's GPU buffers to custom shader/instancing code | a small `GpuBuffer` wrapper, or document only if a use-case lands |
| `Texture::getPixelFormat()` | `sg_pixel_format` | query a texture's format | return the public `TextureFormat` enum instead (`Texture::getFormat()`) |
| `toSokolFormat(TextureFormat)` | `sg_pixel_format` | TextureFormat → sokol; already public, used by apps | keep as-is for interop, or leave excluded (it's an interop bridge by definition) |

## Excluded as pure interop (no wrap planned)
`Texture::getView/getViewForMip/getAttachmentView/getAttachmentViewForMip/getCubemapFaceAttachmentView/getImage/getSampler`,
`Fbo::getTextureView/getSampler`, `Font::getSampler`, `IesProfile::getView/getSampler`
— these are sokol-binding plumbing for advanced/addon rendering; no user-facing need identified.
