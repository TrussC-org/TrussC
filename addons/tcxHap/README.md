# tcxHap

HAP video playback addon. Add `tcxHap` to your project's `addons.make`.

## GPU requirements

HAP movies store BC-compressed textures, such as BC1 for Hap and BC3 for
Hap Alpha / Hap Q. `HapPlayer` requires GPU sampling support for the movie's
BC format. Without it, loading fails with
`Compressed texture format not supported on this GPU`.

On iOS, TrussC's Metal backend enables BC texture formats when the GPU
reports BC support. Hap1 and Hap Q playback with correct colours was checked
on an iPhone 16 in the #645 experiment. Where BC is not supported, for example
on the Simulator, loading fails cleanly with the message above, without a crash.
