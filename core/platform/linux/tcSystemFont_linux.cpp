// =============================================================================
// Linux system font lookup — fontconfig backend
// =============================================================================

#include "tc/utils/tcSystemFont.h"

#if defined(__linux__)

#include <fontconfig/fontconfig.h>
#include <algorithm>
#include <set>

namespace trussc {

namespace {

// Initialize fontconfig once. FcInit is idempotent but cheap to call.
bool ensureFc() {
    static bool initOk = (FcInit() == FcTrue);
    return initOk;
}

} // namespace

std::string systemFontPath(const std::string& name) {
    if (name.empty() || !ensureFc()) return "";

    FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>(name.c_str()));
    if (!pat) return "";

    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);

    std::string out;
    if (match) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            out = reinterpret_cast<const char*>(file);
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    return out;
}

std::vector<std::string> listSystemFonts() {
    std::vector<std::string> out;
    if (!ensureFc()) return out;

    FcPattern*   pat  = FcPatternCreate();
    FcObjectSet* prop = FcObjectSetBuild(FC_FAMILY, nullptr);
    FcFontSet*   set  = FcFontList(nullptr, pat, prop);

    if (set) {
        std::set<std::string> dedup;
        for (int i = 0; i < set->nfont; ++i) {
            FcChar8* fam = nullptr;
            if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &fam) == FcResultMatch && fam) {
                dedup.emplace(reinterpret_cast<const char*>(fam));
            }
        }
        out.assign(dedup.begin(), dedup.end());
        FcFontSetDestroy(set);
    }
    FcObjectSetDestroy(prop);
    FcPatternDestroy(pat);
    return out;
}

} // namespace trussc

#endif // __linux__
