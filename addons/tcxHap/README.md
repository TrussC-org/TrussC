# tcxHap

HAP video playback addon. Add `tcxHap` to your project's `addons.make`.

## GPU requirements

HAP movies store BC-compressed textures, such as BC1 for Hap and BC3 for
Hap Alpha / Hap Q. `HapPlayer` requires GPU sampling support for the movie's
BC format. Without it, loading fails with
`Compressed texture format not supported on this GPU`.

TrussC's Metal backend (the vendored sokol_gfx) enables BC texture formats
only on macOS, so HAP playback fails on iOS, both on the Simulator and on
devices (checked on an iPhone 16, 2026-10-06). See #645.
