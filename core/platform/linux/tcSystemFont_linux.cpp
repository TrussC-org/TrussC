// =============================================================================
// Linux system font lookup — fontconfig backend
// =============================================================================

#include "tc/utils/tcSystemFont.h"

#if defined(__linux__)

#include <fontconfig/fontconfig.h>
#include <algorithm>
#include <cstring>
#include <set>

namespace trussc {

namespace {

// fontconfig is happy to fall back to "any font" when the requested name
// doesn't match. That's the opposite of what callers expect from
// systemFontPath() — they want "" so Font::load can fall through to its
// path-on-disk branch. Verify the match's family / fullname / postscript
// name contains the original query (case-insensitive substring), and
// treat anything else as "not found".
bool matchLooksLikeRequest(FcPattern* match, const std::string& wanted) {
    if (wanted.empty()) return true;
    std::string wantLower(wanted.size(), 0);
    std::transform(wanted.begin(), wanted.end(), wantLower.begin(), ::tolower);

    auto containsWanted = [&](const char* prop) {
        for (int i = 0; ; ++i) {
            FcChar8* s = nullptr;
            if (FcPatternGetString(match, prop, i, &s) != FcResultMatch) break;
            if (!s) continue;
            std::string have((const char*)s);
            std::transform(have.begin(), have.end(), have.begin(), ::tolower);
            if (have.find(wantLower) != std::string::npos) return true;
        }
        return false;
    };

    return containsWanted(FC_FAMILY)
        || containsWanted(FC_FULLNAME)
        || containsWanted(FC_POSTSCRIPT_NAME);
}

} // namespace

fs::path systemFontPath(const std::string& name) {
    if (name.empty()) return "";

    // FcConfigGetCurrent() lazily calls FcInit() on first use, so we don't
    // need a separate init step. fontconfig caches the config thereafter.
    FcConfig* config = FcConfigGetCurrent();
    if (!config) return "";

    // FcNameParse handles the full fontconfig name grammar
    // ("Noto Sans CJK JP:style=Bold" etc.) so callers can pass either a
    // bare family name or a more specific pattern.
    FcPattern* pat = FcNameParse((const FcChar8*)name.c_str());
    if (!pat) return "";
    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(config, pat, &result);
    FcPatternDestroy(pat);

    std::string out;
    if (match && result == FcResultMatch && matchLooksLikeRequest(match, name)) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            out.assign((const char*)file);
        }
    }
    if (match) FcPatternDestroy(match);
    return out;
}

std::vector<std::string> listSystemFonts() {
    std::vector<std::string> out;

    FcConfig* config = FcConfigGetCurrent();
    if (!config) return out;

    FcPattern* pat = FcPatternCreate();
    FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, (char*)0);
    FcFontSet* fs = FcFontList(config, pat, os);
    FcPatternDestroy(pat);
    FcObjectSetDestroy(os);
    if (!fs) return out;

    // FC_FAMILY can have multiple localized values per font; dedupe across
    // the whole set so the caller gets each family once.
    std::set<std::string> seen;
    for (int i = 0; i < fs->nfont; ++i) {
        for (int j = 0; ; ++j) {
            FcChar8* family = nullptr;
            if (FcPatternGetString(fs->fonts[i], FC_FAMILY, j, &family) != FcResultMatch) break;
            if (!family) continue;
            std::string name((const char*)family);
            if (!name.empty() && seen.insert(name).second) {
                out.push_back(std::move(name));
            }
        }
    }
    FcFontSetDestroy(fs);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace trussc

#endif // __linux__
