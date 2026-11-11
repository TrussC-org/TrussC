# ofxMidi helper ports

Source URL: https://github.com/danomatika/ofxMidi (ofxMidiClock.h and
ofxMidiTimecode.h). Version / exact upstream commit: **not recorded**.
Archive SHA-256: **not recorded**. TrussC import: `65fe10a3`.
These are owned C++ ports, not verbatim upstream files. The exact import-time
adaptations cannot be reconstructed without the original helper sources.
The BSD notice is retained in [OFXMIDI_LICENSE.txt](OFXMIDI_LICENSE.txt);
upstream copyright years were not recorded with the port.

Later changes: `d5d7a95c` checks quarter-frame message length; `13af5e86`
moves the helpers into `tcx::midi` with compatibility aliases; `8bf5bd21`
documents those aliases. Keep that external-input check and the public API.
This reconciliation adds attribution only, without changing helper behavior.

| Port file | SHA-256 (working-tree bytes, including notice link) |
| --- | --- |
| `src/tcxMidiClock.h` | `250df5d0eef9b5f4c22017818bab5298edf60be7d3e508b300c9a0c518ba3910` |
| `src/tcxMidiTimecode.h` | `b1c195cbd4e925be54924c6108a6e21719f60df8a6b5a2a6a1b7e4e646bfe698` |

Recover and record the exact upstream revision/notice on the next update;
do not infer it from the TrussC import date. Run the addon MIDI tests.
