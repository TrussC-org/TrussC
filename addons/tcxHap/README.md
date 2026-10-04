# tcxHap

HAP video playback addon. Add `tcxHap` to your project's `addons.make`.

## GPU requirements

HAP movies store BC-compressed textures, such as BC1 for Hap and BC3 for
Hap Alpha / Hap Q. `HapPlayer` requires GPU sampling support for the movie's
BC format. Without it, loading fails with
`Compressed texture format not supported on this GPU`.

The tested iOS Simulator GPU lacks BC texture support, so HAP playback fails
there even when the example builds. Playback on iOS devices has not been
verified; check BC support on the actual device before relying on it.
