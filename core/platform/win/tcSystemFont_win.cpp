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

// UTF-8 → UTF-16 (wide) for DirectWrite APIs.
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// UTF-16 (wide) → UTF-8 for returning strings.
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

// Get the on-disk file path backing the first font in `family`.
std::string firstFontFilePath(IDWriteFontFamily* family) {
    if (!family) return "";

    ComPtr<IDWriteFont> font;
    if (FAILED(family->GetFont(0, &font)) || !font) return "";

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

std::string systemFontPath(const std::string& name) {
    if (name.empty()) return "";
    auto factory = getFactory();
    if (!factory) return "";

    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(factory->GetSystemFontCollection(&coll, FALSE)) || !coll) return "";

    std::wstring wname = utf8ToWide(name);
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(coll->FindFamilyName(wname.c_str(), &index, &exists)) || !exists) {
        return "";
    }

    ComPtr<IDWriteFontFamily> family;
    if (FAILED(coll->GetFontFamily(index, &family))) return "";

    return firstFontFilePath(family.Get());
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

        // Prefer en-us; fall back to index 0.
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
