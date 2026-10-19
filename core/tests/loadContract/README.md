# loadContract

Checks the core loading entry: relative files resolve under a data root in a
Japanese-named folder while the working directory is elsewhere; file loaders
share one missing-file sentence; SoundBuffer, SoundStream and VideoPlayer clear
existing names after failure, and SoundBuffer clears names for memory loads.
Font keeps data-file-first lookup followed by the system font name. Failed
system lookup returns the Font-specific sentence without the shared missing-file log.

The Linux run uses the null audio backend. With `DISPLAY` set (Xvfb is enough),
it also checks Font lookup and IES loading, checks Image/Texture against the actual GPU size limit, and proves Pixels
can decode above that limit. Image size rejections must log exactly one Error,
including dimensions and a resolved path for file loads. It checks VideoPlayer reloads using the existing
HAP fixture. Other desktop platforms can request `--gpu-check` explicitly.

A standalone Web build also checks the missing Font result and Image size/error
logging, then runs AAC loading from `setup()` without a user gesture:
PCM and the recorded name must be ready when load returns; an invalid AAC must
fail and clear the name. The browser sets `document.title` and
`document.body.dataset.loadContract` to `loadContract PASS` / `PASS` or
`loadContract FAIL` / `FAIL` after the assertions.

Windows verification still requires loading image, sound, video and font files
from a Japanese-named folder on Windows itself. Saving and addon model loaders
belong to later parts of #761.

For the additional Web URL checks, serve `font.ttf` (a TrueType font) and
`clip.webm` (a 16x16 VP8 video) beside the generated HTML, then open it with
`?url-checks`. The same setup callback checks Font/Video URL success, first-frame
readiness and missing-URL failure. No external server is needed.

