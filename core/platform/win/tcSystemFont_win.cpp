// =============================================================================
// Windows system font lookup — DirectWrite backend
// =============================================================================

#include "tc/utils/tcSystemFont.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <set>
#include <vector>
#include <string>

namespace trussc {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string wideToUtf8(const wchar_t* w, int wlen) {
    if (!w || wlen <= 0) return "";
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, wlen, s.data(), n, nullptr, nullptr);
    return s;
}

ComPtr<IDWriteFactory> getFactory() {
    static ComPtr<IDWriteFactory> factory;
    if (!factory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    }
    return factory;
}

std::string fontFilePath(IDWriteFont* font) {
    if (!font) return "";

    ComPtr<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(&face)) || !face) return "";

    UINT32 fileCount = 0;
    if (FAILED(face->GetFiles(&fileCount, nullptr)) || fileCount == 0) return "";

    std::vector<ComPtr<IDWriteFontFile>> files(fileCount);
    if (FAILED(face->GetFiles(&fileCount, reinterpret_cast<IDWriteFontFile**>(files.data())))) return "";

    const void* refKey = nullptr;
    UINT32 refKeySize = 0;
    if (FAILED(files[0]->GetReferenceKey(&refKey, &refKeySize))) return "";

    ComPtr<IDWriteFontFileLoader> loader;
    if (FAILED(files[0]->GetLoader(&loader)) || !loader) return "";

    ComPtr<IDWriteLocalFontFileLoader> localLoader;
    if (FAILED(loader.As(&localLoader)) || !localLoader) return "";

    UINT32 pathLen = 0;
    if (FAILED(localLoader->GetFilePathLengthFromKey(refKey, refKeySize, &pathLen))) return "";

    std::wstring wpath(pathLen + 1, L'\0');
    if (FAILED(localLoader->GetFilePathFromKey(refKey, refKeySize, wpath.data(), pathLen + 1))) return "";

    return wideToUtf8(wpath.c_str(), (int)pathLen);
}

} // namespace

fs::path systemFontPath(const std::string& name) {
    if (name.empty()) return "";
    auto factory = getFactory();
    if (!factory) return "";

    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(factory->GetSystemFontCollection(&coll, FALSE)) || !coll) return "";

    std::wstring wname = utf8ToWide(name);

    // Strategy 1: family name lookup
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(coll->FindFamilyName(wname.c_str(), &index, &exists)) && exists) {
        ComPtr<IDWriteFontFamily> family;
        if (SUCCEEDED(coll->GetFontFamily(index, &family))) {
            ComPtr<IDWriteFont> font;
            if (SUCCEEDED(family->GetFirstMatchingFont(
                    DWRITE_FONT_WEIGHT_REGULAR,
                    DWRITE_FONT_STRETCH_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, &font))) {
                std::string path = fontFilePath(font.Get());
                if (!path.empty()) return path;
            }
        }
    }

    // Strategy 2: PostScript name lookup (iterate all fonts)
    UINT32 familyCount = coll->GetFontFamilyCount();
    for (UINT32 fi = 0; fi < familyCount; fi++) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(coll->GetFontFamily(fi, &family))) continue;

        UINT32 fontCount = family->GetFontCount();
        for (UINT32 gi = 0; gi < fontCount; gi++) {
            ComPtr<IDWriteFont> font;
            if (FAILED(family->GetFont(gi, &font))) continue;

            ComPtr<IDWriteLocalizedStrings> psNames;
            BOOL psExists = FALSE;
            if (FAILED(font->GetInformationalStrings(
                    DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME,
                    &psNames, &psExists)) || !psExists) {
                continue;
            }

            UINT32 count = psNames->GetCount();
            for (UINT32 si = 0; si < count; si++) {
                UINT32 len = 0;
                psNames->GetStringLength(si, &len);
                std::wstring psName(len + 1, 0);
                psNames->GetString(si, psName.data(), len + 1);
                psName.resize(len);

                if (_wcsicmp(psName.c_str(), wname.c_str()) == 0) {
                    std::string path = fontFilePath(font.Get());
                    if (!path.empty()) return path;
                }
            }
        }
    }

    return "";
}

std::vector<std::string> listSystemFonts() {
    std::vector<std::string> out;
    auto factory = getFactory();
    if (!factory) return out;

    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(factory->GetSystemFontCollection(&coll, FALSE)) || !coll) return out;

    UINT32 count = coll->GetFontFamilyCount();
    std::set<std::string> dedup;
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(coll->GetFontFamily(i, &family))) continue;

        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(&names)) || !names) continue;

        UINT32 idx = 0;
        BOOL exists = FALSE;
        if (FAILED(names->FindLocaleName(L"en-us", &idx, &exists)) || !exists) {
            idx = 0;
        }

        UINT32 len = 0;
        if (FAILED(names->GetStringLength(idx, &len))) continue;
        std::wstring w(len + 1, L'\0');
        if (FAILED(names->GetString(idx, w.data(), len + 1))) continue;
        dedup.emplace(wideToUtf8(w.c_str(), (int)len));
    }
    out.assign(dedup.begin(), dedup.end());
    return out;
}

} // namespace trussc

#endif // _WIN32
