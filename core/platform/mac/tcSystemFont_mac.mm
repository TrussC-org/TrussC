// =============================================================================
// macOS system font lookup — CoreText backend
// =============================================================================

#include "tc/utils/tcSystemFont.h"

#if defined(__APPLE__)

#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#include <climits>
#include <algorithm>

namespace trussc {

namespace {

std::string cfStringToStd(CFStringRef s) {
    if (!s) return "";
    if (const char* fast = CFStringGetCStringPtr(s, kCFStringEncodingUTF8)) {
        return fast;
    }
    CFIndex len = CFStringGetLength(s);
    CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::vector<char> buf((size_t)maxBytes);
    if (CFStringGetCString(s, buf.data(), maxBytes, kCFStringEncodingUTF8)) {
        return std::string(buf.data());
    }
    return "";
}

} // namespace

fs::path systemFontPath(const std::string& name) {
    if (name.empty()) return "";
    CFStringRef cfName = CFStringCreateWithCString(nullptr, name.c_str(), kCFStringEncodingUTF8);
    if (!cfName) return "";

    // CTFontDescriptorCreateWithNameAndSize accepts both PostScript names and
    // family / display names — it walks the system font collection internally.
    CTFontDescriptorRef desc = CTFontDescriptorCreateWithNameAndSize(cfName, 0);
    CFRelease(cfName);
    if (!desc) return "";

    CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
    CFRelease(desc);
    if (!url) return "";

    char buf[PATH_MAX] = {0};
    Boolean ok = CFURLGetFileSystemRepresentation(url, true, (UInt8*)buf, PATH_MAX);
    CFRelease(url);
    return ok ? std::string(buf) : "";
}

std::vector<std::string> listSystemFonts() {
    std::vector<std::string> out;
    CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
    if (!families) return out;

    CFIndex n = CFArrayGetCount(families);
    out.reserve((size_t)n);
    for (CFIndex i = 0; i < n; ++i) {
        CFStringRef s = (CFStringRef)CFArrayGetValueAtIndex(families, i);
        std::string name = cfStringToStd(s);
        if (!name.empty()) out.push_back(std::move(name));
    }
    CFRelease(families);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace trussc

#endif // __APPLE__
