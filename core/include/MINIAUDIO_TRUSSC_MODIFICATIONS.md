# TrussC Modifications to miniaudio

This file documents modifications made to `miniaudio.h` for TrussC.
When updating from upstream, these changes need to be re-applied.
Search for `MA_TRUSSC_VALIDATE_AUDIO_` to find the modified sections.

**Upstream base:** miniaudio v0.11.25 (the version in the vendored header).

---

## miniaudio.h

### TrussC patches (2)

1. Converter source-format admission (`MA_TRUSSC_VALIDATE_AUDIO_FORMAT`).
2. Embedded WAV/FLAC/MP3 source and frame-format admission
   (`MA_TRUSSC_VALIDATE_AUDIO_FRAME`).

### Audio format and frame admission

- `ma_decoder__init_data_converter` invokes the opt-in
  `MA_TRUSSC_VALIDATE_AUDIO_FORMAT(decoder, channels, rate)` before converter
  allocation, so an external source format cannot dictate unchecked work.
- The embedded WAV reader checks its format before translated-header allocation.
  FLAC initialization and frame headers, and MP3 frame decoding invoke the
  opt-in `MA_TRUSSC_VALIDATE_AUDIO_FRAME(callbacks, channels, rate,
  originalChannels, originalRate)` before format-driven allocation/copying.
  Nonzero original values also reject mid-stream format changes.
- `tc/sound/tcAudio_impl.cpp` defines both macros while compiling miniaudio,
  then undefines them. Only decoders using TrussC's budget allocation callbacks
  are checked. Other consumers and ordinary miniaudio allocations keep upstream
  behavior when the macros are absent. No miniaudio public API is added. The
  Bound comments mark the admission points. Allocation callbacks remain needed
  for streaming working-set admission and clean allocation errors; full loads
  have no PCM/decoder quota. Neither format hook is quota-only.

## How to Update miniaudio

1. Replace the header with the chosen upstream revision and update this base.
2. Re-apply the converter check after obtaining the backend format and before
   creating the converter. Re-apply the WAV, FLAC and MP3 checks before their
   format-driven allocations/frame copies; preserve their early failure paths.
3. Keep the macro definitions and budget ownership checks in `tcAudio_impl.cpp`.
4. Run `audioDecodeBounds` and `audioDiagnostics`. In `audioDecodeBounds`,
   `miniaudio converter rejects source format before allocating` uses a custom
   backend with no frame hook: it fails if the converter hook is lost.
   `changed MP3 layout rejects whole full-buffer unit` fails if the MP3 frame
   hook is lost. `FLAC metadata rejects before decoded-sample allocation`
   forces the FLAC backend with the existing allocator seam and checks its
   peak stays below the decoded-sample allocation size: the converter's later
   rejection cannot hide a lost FLAC metadata hook. `FLAC frame rate change
   rejects through frame hook and keeps previous PCM` generates valid-CRC
   constant-subframe frames at 48 kHz then 44.1 kHz and checks the hook's
   rejection counter as well as transactional failure. Each FLAC check fails
   when only its own hook is disabled in a scratch copy. WAV source-format
   and stream-format checks cover the other entry paths. Run the full core
   sweep after a vendor update.
