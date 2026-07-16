// =============================================================================
// Android system font lookup — stub.
// /system/fonts/ has a fixed layout but mapping family / PostScript names to
// files requires parsing /system/etc/fonts.xml or using NDK AAssetManager-based
// font enumeration (API 29+). Not implemented yet; current TC_FONT_* defaults
// fall through to Font::load's path branch.
// =============================================================================

#include "tc/utils/tcSystemFont.h"

#if defined(__ANDROID__)

namespace trussc {

fs::path systemFontPath(const std::string& /*name*/) {
    return "";
}

std::vector<std::string> listSystemFonts() {
    return {};
}

} // namespace trussc

#endif // __ANDROID__
