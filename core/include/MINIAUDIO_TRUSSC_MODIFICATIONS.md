# miniaudio provenance

`miniaudio.h` is the unmodified miniaudio v0.11.26 header, byte-identical to
the upstream `0.11.26` release. Source: https://github.com/mackron/miniaudio/tree/0.11.26.
No TrussC patches remain.
The converter and embedded codec format-check hooks (vendor M1/M2) were removed.

SHA-256: `6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e`

`tc/sound/tcAudio_impl.cpp` configures the upstream
`MA_DR_WAV_MAX_SAMPLE_RATE` macro to `UINT32_MAX`, the WAV field's range,
instead of dr_wav's default 384 kHz policy. This does not modify the vendor file.
Run `audioDecodeBounds`, `audioDiagnostics` and the core sweep after updates.
