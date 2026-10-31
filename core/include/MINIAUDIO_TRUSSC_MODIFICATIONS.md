# miniaudio provenance

`miniaudio.h` is the unmodified miniaudio v0.11.25 header, byte-identical to
TrussC's original v0.11.25 import (`ea2060ce`). No TrussC patches remain.
The converter and embedded codec format-check hooks (vendor M1/M2) were removed.

SHA-256: `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89`

`tc/sound/tcAudio_impl.cpp` configures the upstream
`MA_DR_WAV_MAX_SAMPLE_RATE` macro to `UINT32_MAX`, the WAV field's range,
instead of dr_wav's default 384 kHz policy. This does not modify the vendor file.
Run `audioDecodeBounds`, `audioDiagnostics` and the core sweep after updates.
