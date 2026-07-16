// =============================================================================
// Web (Emscripten) system font lookup — no-op.
// Browsers don't expose locally installed fonts to WASM (privacy / sandbox).
// Web code paths use URL inputs (TC_FONT_* defaults point at CDN URLs that
// Font::load fetches via emscripten_fetch), so this stub returns empty.
// =============================================================================

#include "tc/utils/tcSystemFont.h"

#if defined(__EMSCRIPTEN__)

namespace trussc {

fs::path systemFontPath(const std::string& /*name*/) {
    return "";
}

std::vector<std::string> listSystemFonts() {
    return {};
}

} // namespace trussc

#endif // __EMSCRIPTEN__
