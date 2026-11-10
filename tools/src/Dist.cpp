#include "Dist.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

namespace dist {

namespace {

using nlohmann::json;

// fs::path <-> UTF-8, '/'-separated
std::string toUtf8(const fs::path& path) {
#if defined(__cpp_char8_t)
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
#else
    return path.generic_u8string();
#endif
}

fs::path fromUtf8(const std::string& text) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(text.begin(), text.end()));
#else
    return fs::u8path(text);
#endif
}

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return static_cast<bool>(file) || file.eof();
}

bool writeFile(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << text;
    return static_cast<bool>(file);
}

// A path inside the package: relative, no "..", '/'-separated, no trailing '/'.
bool cleanPackagePath(std::string path, std::string& out) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(path);
    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") return false;
        parts.push_back(part);
    }
    if (parts.empty() || path.front() == '/' || (path.size() > 1 && path[1] == ':')) return false;
    out.clear();
    for (const auto& p : parts) {
        if (!out.empty()) out += '/';
        out += p;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Binary headers
// -----------------------------------------------------------------------------

uint16_t le16(const std::string& d, size_t o) {
    return static_cast<uint16_t>(static_cast<unsigned char>(d[o]) |
                                 (static_cast<unsigned char>(d[o + 1]) << 8));
}
uint32_t le32(const std::string& d, size_t o) {
    return static_cast<uint32_t>(le16(d, o)) | (static_cast<uint32_t>(le16(d, o + 2)) << 16);
}
uint32_t be32(const std::string& d, size_t o) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(d[o])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(d[o + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(d[o + 2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(d[o + 3]));
}

struct PeSection { uint32_t virtualAddress, virtualSize, rawOffset, rawSize; };

void readPe(const std::string& d, BinaryInfo& info) {
    if (d.size() < 0x40) return;
    const uint32_t pe = le32(d, 0x3C);
    if (pe > d.size() - 24 || d.compare(pe, 4, std::string("PE\0\0", 4)) != 0) return;
    const uint16_t machine = le16(d, pe + 4);
    const uint16_t sectionCount = le16(d, pe + 6);
    const uint16_t optionalSize = le16(d, pe + 20);
    const size_t optional = pe + 24;
    if (optional + optionalSize > d.size() || optionalSize < 2) return;
    info.valid = true;
    info.format = "pe";
    switch (machine) {
        case 0x8664: info.arch = "x64"; break;
        case 0xAA64: info.arch = "arm64"; break;
        case 0x014C: info.arch = "x86"; break;
        default: break;
    }
    const uint16_t magic = le16(d, optional);
    const size_t countAt = magic == 0x20B ? 108 : 92;     // NumberOfRvaAndSizes
    const size_t directories = magic == 0x20B ? 112 : 96;
    if (optionalSize < directories) return;
    // The data directories (8 bytes each) are read only as far as the optional
    // header holds them; the header itself was checked to lie inside the file.
    // NumberOfRvaAndSizes alone is not trusted: a truncated or damaged file can
    // claim more than there is.
    const uint32_t directoryCount = (std::min)(le32(d, optional + countAt),
                                               static_cast<uint32_t>((optionalSize - directories) / 8));

    std::vector<PeSection> sections;
    const size_t table = optional + optionalSize;
    for (uint16_t i = 0; i < sectionCount; ++i) {
        const size_t s = table + static_cast<size_t>(i) * 40;
        if (s + 40 > d.size()) break;
        sections.push_back({le32(d, s + 12), le32(d, s + 8), le32(d, s + 20), le32(d, s + 16)});
    }
    // A file offset for an RVA, only where the section has data in the file
    // (its raw size; the rest of virtualSize is zero-filled memory, not file).
    auto offsetOf = [&](uint32_t rva, size_t& out) {
        for (const auto& s : sections) {
            if (rva >= s.virtualAddress && rva - s.virtualAddress < s.rawSize) {
                out = static_cast<size_t>(s.rawOffset) + (rva - s.virtualAddress);
                return out < d.size();
            }
        }
        return false;
    };
    auto stringAt = [&](uint32_t rva) {
        size_t at = 0;
        if (!offsetOf(rva, at)) return std::string();
        const size_t end = d.find('\0', at);
        if (end == std::string::npos || end - at > 260) return std::string();
        return lower(d.substr(at, end - at));
    };
    std::set<std::string> seen;
    auto add = [&](const std::string& name) {
        if (!name.empty() && seen.insert(name).second) info.imports.push_back(name);
    };
    // Directory 1: imports (20-byte descriptors, Name at +12, ends with zeros).
    if (directoryCount > 1) {
        size_t at = 0;
        const uint32_t rva = le32(d, optional + directories + 8);
        if (rva && offsetOf(rva, at)) {
            for (; at + 20 <= d.size(); at += 20) {
                const uint32_t nameRva = le32(d, at + 12);
                if (!nameRva && !le32(d, at) && !le32(d, at + 16)) break;
                add(stringAt(nameRva));
            }
        }
    }
    // Directory 13: delay-load imports (32-byte descriptors, RVA-based ones only).
    if (directoryCount > 13) {
        size_t at = 0;
        const uint32_t rva = le32(d, optional + directories + 13 * 8);
        if (rva && offsetOf(rva, at)) {
            for (; at + 32 <= d.size(); at += 32) {
                const uint32_t nameRva = le32(d, at + 4);
                if (!nameRva) break;
                if (le32(d, at) & 1) add(stringAt(nameRva));
            }
        }
    }
}

void readElf(const std::string& d, BinaryInfo& info) {
    if (d.size() < 20) return;
    info.valid = true;
    info.format = "elf";
    const bool little = d[5] == 1;
    const uint16_t machine = little ? le16(d, 18)
        : static_cast<uint16_t>((static_cast<unsigned char>(d[18]) << 8) | static_cast<unsigned char>(d[19]));
    switch (machine) {
        case 62: info.arch = "x64"; break;
        case 183: info.arch = "arm64"; break;
        case 3: info.arch = "x86"; break;
        case 40: info.arch = "arm"; break;
        default: break;
    }
}

void readMachO(const std::string& d, BinaryInfo& info) {
    if (d.size() < 8) return;
    info.valid = true;
    info.format = "macho";
    if (be32(d, 0) == 0xCAFEBABE) {
        info.arch = "universal";
        return;
    }
    const uint32_t cpu = le32(d, 4);
    if (cpu == 0x01000007) info.arch = "x64";
    else if (cpu == 0x0100000C) info.arch = "arm64";
}

bool isRuntimeImport(const std::string& dll) {
    for (const char* prefix : {"msvcp140", "vcruntime140", "concrt140", "vccorlib140"})
        if (startsWith(dll, prefix)) return true;
    return false;
}

// Version folder names like "14.44.35112" -> comparable numbers; other names sort first.
std::vector<int> versionKey(const std::string& name) {
    std::vector<int> key;
    std::istringstream stream(name);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (part.empty() || !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); }))
            return {};
        key.push_back(std::stoi(part));
    }
    return key;
}

bool isNoticeName(const std::string& name) {
    const std::string l = lower(name);
    for (const char* prefix : {"license", "licence", "licenses", "copying", "notice"})
        if (startsWith(l, prefix)) return true;
    return false;
}

std::string padRight(const std::string& text, size_t width) {
    return text.size() >= width ? text + "  " : text + std::string(width - text.size(), ' ');
}

std::string xmlEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

// Sets the <string> value of <key>key</key> in an XML plist, adding the key
// at the end of the top-level dict when it is missing.
bool setPlistString(std::string& plist, const std::string& key, const std::string& value) {
    const std::string tag = "<key>" + key + "</key>";
    const std::string escaped = xmlEscape(value);
    const size_t at = plist.find(tag);
    if (at == std::string::npos) {
        const size_t end = plist.rfind("</dict>");
        if (end == std::string::npos) return false;
        plist.insert(end, "\t" + tag + "\n\t<string>" + escaped + "</string>\n");
        return true;
    }
    const size_t open = plist.find("<string>", at + tag.size());
    const size_t close = open == std::string::npos ? open : plist.find("</string>", open);
    if (open == std::string::npos || close == std::string::npos) return false;
    plist.replace(open + 8, close - open - 8, escaped);
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// dist.json
// -----------------------------------------------------------------------------

bool parseConfig(const std::string& text, Config& out, std::vector<std::string>& warnings,
                 std::string& error) {
    out = Config{};
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded()) { error = "dist.json is not valid JSON"; return false; }
    if (!root.is_object()) { error = "dist.json must be a JSON object"; return false; }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string& key = it.key();
        const json& value = it.value();
        auto expectString = [&](std::string& target) {
            if (!value.is_string()) { error = "dist.json: \"" + key + "\" must be a string"; return false; }
            target = value.get<std::string>();
            return true;
        };
        if (key == "name") {
            if (!expectString(out.name)) return false;
        } else if (key == "version") {
            if (!expectString(out.version)) return false;
        } else if (key == "author") {
            if (!expectString(out.author)) return false;
        } else if (key == "eulaFile") {
            if (!expectString(out.eulaFile)) return false;
        } else if (key == "include") {
            if (!value.is_array()) { error = "dist.json: \"include\" must be an array"; return false; }
            for (const auto& entry : value) {
                if (!entry.is_object() || !entry.contains("from") || !entry["from"].is_string() ||
                    !entry.contains("to") || !entry["to"].is_string()) {
                    error = "dist.json: each \"include\" entry needs \"from\" and \"to\" strings";
                    return false;
                }
                out.include.push_back({entry["from"].get<std::string>(), entry["to"].get<std::string>()});
            }
        } else if (key == "exclude") {
            if (!value.is_array()) { error = "dist.json: \"exclude\" must be an array"; return false; }
            for (const auto& entry : value) {
                if (!entry.is_string()) { error = "dist.json: \"exclude\" entries must be strings"; return false; }
                out.exclude.push_back(entry.get<std::string>());
            }
        } else if (key == "licenses") {
            if (!value.is_array()) { error = "dist.json: \"licenses\" must be an array"; return false; }
            for (const auto& entry : value) {
                if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() ||
                    !entry.contains("file") || !entry["file"].is_string()) {
                    error = "dist.json: each \"licenses\" entry needs \"name\" and \"file\" strings";
                    return false;
                }
                out.licenses.push_back({entry["name"].get<std::string>(), entry["file"].get<std::string>()});
            }
        } else if (key == "installer") {
            if (!value.is_object()) { error = "dist.json: \"installer\" must be an object"; return false; }
            for (auto option = value.begin(); option != value.end(); ++option) {
                bool* target = nullptr;
                if (option.key() == "showEula") target = &out.installer.showEula;
                else if (option.key() == "showNotices") target = &out.installer.showNotices;
                if (!target) {
                    warnings.push_back("dist.json: unknown key \"installer." + option.key() + "\" ignored");
                    continue;
                }
                if (!option.value().is_boolean()) {
                    error = "dist.json: \"installer." + option.key() + "\" must be true or false";
                    return false;
                }
                *target = option.value().get<bool>();
            }
        } else {
            warnings.push_back("dist.json: unknown key \"" + key + "\" ignored");
        }
    }
    return true;
}

std::string copyrightLine(const std::string& author) {
    if (author.empty()) return "";
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return "\xC2\xA9 " + std::to_string(local.tm_year + 1900) + " " + author;  // ©
}

std::string configTemplate(const std::string& name, const std::string& version,
                           const std::string& author) {
    // Written by hand: nlohmann would sort the keys, not keep the documented order.
    std::string text = "{\n";
    text += "  \"name\": " + json(name).dump() + ",\n";
    text += "  \"version\": " + json(version).dump() + ",\n";
    text += "  \"author\": " + json(author).dump() + ",\n";
    // Empty: EULA.txt in the project folder ships when it exists. A file named
    // here must exist, so naming EULA.txt would fail apps without one.
    text += "  \"eulaFile\": \"\",\n";
    text += "  \"include\": [],\n";
    text += "  \"exclude\": [],\n";
    text += "  \"licenses\": [],\n";
    text += "  \"installer\": {\n";
    text += "    \"showEula\": false,\n";
    text += "    \"showNotices\": false\n";
    text += "  }\n";
    text += "}\n";
    return text;
}

bool normalizeVersion(const std::string& input, std::string& out) {
    std::string v = input;
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    const auto key = versionKey(v);
    if (key.empty() || key.size() > 4) return false;
    for (char c : v) if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.')) return false;
    for (int part : key) if (part > 65535) return false;  // Windows version fields are 16-bit
    out = v;
    return true;
}

std::string fileSafeName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (c < 0x20 || std::strchr("<>:\"/\\|?*", c)) continue;
        out += c == ' ' ? '-' : static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == '-')) out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(0, 1);
    return out.empty() ? "app" : out;
}

// -----------------------------------------------------------------------------
// exclude patterns
// -----------------------------------------------------------------------------

namespace {

std::vector<std::string> splitSegments(const std::string& path) {
    std::vector<std::string> segments;
    std::string segment;
    std::istringstream stream(path);
    while (std::getline(stream, segment, '/'))
        if (!segment.empty()) segments.push_back(segment);
    return segments;
}

// '*' and '?' within one segment.
bool segmentMatch(const char* pattern, const char* text) {
    if (*pattern == '\0') return *text == '\0';
    if (*pattern == '*') {
        for (const char* t = text;; ++t) {
            if (segmentMatch(pattern + 1, t)) return true;
            if (*t == '\0') return false;
        }
    }
    if (*text == '\0') return false;
    return (*pattern == '?' || *pattern == *text) && segmentMatch(pattern + 1, text + 1);
}

// '**' matches zero or more whole segments.
bool segmentsMatch(const std::vector<std::string>& pattern, size_t p,
                   const std::vector<std::string>& path, size_t s) {
    if (p == pattern.size()) return s == path.size();
    if (pattern[p] == "**") {
        for (size_t skip = s; skip <= path.size(); ++skip)
            if (segmentsMatch(pattern, p + 1, path, skip)) return true;
        return false;
    }
    return s < path.size() && segmentMatch(pattern[p].c_str(), path[s].c_str()) &&
           segmentsMatch(pattern, p + 1, path, s + 1);
}

} // namespace

bool globMatch(const std::string& pattern, const std::string& path) {
    return segmentsMatch(splitSegments(pattern), 0, splitSegments(path), 0);
}

bool isExcluded(const std::vector<std::string>& patterns, const std::string& path) {
    // The path itself and each of its parent folders ("data/raw" excludes "data/raw/a.png").
    std::vector<std::string> candidates{path};
    for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
        candidates.push_back(path.substr(0, slash));
    for (std::string pattern : patterns) {
        std::replace(pattern.begin(), pattern.end(), '\\', '/');
        if (splitSegments(pattern).empty()) continue;
        for (const auto& candidate : candidates)
            if (globMatch(pattern, candidate)) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Executables
// -----------------------------------------------------------------------------

BinaryInfo readBinaryInfo(const fs::path& file) {
    BinaryInfo info;
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size < 4 || size > (1ull << 31)) return info;
    std::string data;
    if (!readFile(file, data) || data.size() < 4) return info;
    if (data[0] == 'M' && data[1] == 'Z') readPe(data, info);
    else if (data.compare(0, 4, "\x7F" "ELF") == 0) readElf(data, info);
    else {
        const uint32_t magic = le32(data, 0);
        if (magic == 0xFEEDFACF || magic == 0xFEEDFACE || be32(data, 0) == 0xCAFEBABE) readMachO(data, info);
    }
    return info;
}

bool isDebugCrt(const std::string& dll) {
    for (const char* name : {"vcruntime140d.dll", "vcruntime140_1d.dll", "msvcp140d.dll",
                             "msvcp140_1d.dll", "msvcp140_2d.dll", "msvcp140d_atomic_wait.dll",
                             "msvcp140d_codecvt_ids.dll", "ucrtbased.dll", "concrt140d.dll",
                             "vccorlib140d.dll"})
        if (dll == name) return true;
    return false;
}

bool isMediaFoundation(const std::string& dll) {
    return dll == "mfplat.dll" || dll == "mfreadwrite.dll" || dll == "mf.dll" || dll == "mfuuid.dll";
}

fs::path findVcRuntimeDir(const fs::path& vsInstallPath, const std::string& arch) {
    std::error_code ec;
    const fs::path msvc = vsInstallPath / "VC" / "Redist" / "MSVC";
    std::vector<std::pair<std::vector<int>, fs::path>> versions;
    for (const auto& entry : fs::directory_iterator(msvc, ec)) {
        if (!entry.is_directory(ec)) continue;
        auto key = versionKey(entry.path().filename().string());
        if (!key.empty()) versions.emplace_back(std::move(key), entry.path());
    }
    std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& version : versions) {
        for (const auto& entry : fs::directory_iterator(version.second / arch, ec)) {
            const std::string name = entry.path().filename().string();
            if (entry.is_directory(ec) && startsWith(name, "Microsoft.VC") && endsWith(name, ".CRT"))
                return entry.path();
        }
    }
    return {};
}

// -----------------------------------------------------------------------------
// Licenses
// -----------------------------------------------------------------------------

std::vector<fs::path> findNoticeFiles(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && isNoticeName(entry.path().filename().string()))
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<LicenseSource> collectLicenses(const fs::path& tcRoot,
                                           const std::vector<std::pair<std::string, fs::path>>& addons,
                                           const std::vector<LicenseEntry>& extra,
                                           const fs::path& projectDir) {
    std::vector<LicenseSource> sources;
    std::error_code ec;

    LicenseSource core;
    core.component = "TrussC";
    core.declared = "MIT + third-party";
    const fs::path coreFile = tcRoot / "docs" / "LICENSE.md";
    std::string coreText;
    if (fs::is_regular_file(coreFile, ec)) {
        core.files.push_back(coreFile);
        readFile(coreFile, coreText);
    } else {
        core.note = "docs/LICENSE.md not found in the TrussC folder";
    }
    sources.push_back(core);
    // Table rows ending in "| <addon> |" (the Addon column of "Addon Dependencies").
    auto listedInCore = [&](const std::string& addon) {
        std::istringstream lines(coreText);
        std::string line;
        while (std::getline(lines, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (endsWith(line, "| " + addon + " |")) return true;
        }
        return false;
    };

    for (const auto& [name, dir] : addons) {
        LicenseSource source;
        source.component = name;
        std::string text;
        if (readFile(dir / "addon.json", text)) {
            const json meta = json::parse(text, nullptr, false);
            if (meta.is_object() && meta.contains("license") && meta["license"].is_string())
                source.declared = meta["license"].get<std::string>();
        }
        source.files = findNoticeFiles(dir);
        source.inCoreList = listedInCore(name);
        if (source.files.empty()) {
            source.note = source.inCoreList
                ? "dependencies listed in licenses/TrussC/LICENSE.md"
                : "no notice file: check its dependencies yourself";
        }
        sources.push_back(source);
    }

    for (const auto& entry : extra) {
        LicenseSource source;
        source.component = entry.name;
        fs::path file = fromUtf8(entry.file);
        if (file.is_relative()) file = projectDir / file;
        if (fs::is_regular_file(file, ec)) source.files.push_back(file);
        else source.note = "file not found: " + entry.file;
        sources.push_back(source);
    }
    return sources;
}

std::string formatLicenseReport(const std::vector<LicenseSource>& sources) {
    size_t nameWidth = 12, declaredWidth = 8;
    for (const auto& s : sources) {
        nameWidth = (std::max)(nameWidth, s.component.size() + 2);
        declaredWidth = (std::max)(declaredWidth, s.declared.size() + 2);
    }
    std::string out = "Licenses collected (trusscli collects what it finds; checking them is up to you):\n";
    for (const auto& s : sources) {
        const std::string head = "  " + padRight(s.component, nameWidth) +
                                 padRight(s.declared.empty() ? "-" : s.declared, declaredWidth);
        if (s.files.empty()) {
            out += head + "(" + s.note + ")\n";
            continue;
        }
        for (size_t i = 0; i < s.files.size(); ++i) {
            out += (i == 0 ? head : std::string(head.size(), ' ')) + "licenses/" +
                   fileSafeName(s.component) + "/" + toUtf8(s.files[i].filename()) + "\n";
        }
    }
    return out;
}

// -----------------------------------------------------------------------------
// Packaging
// -----------------------------------------------------------------------------

const char* platformName(Platform platform) {
    switch (platform) {
        case Platform::Windows: return "windows";
        case Platform::MacOS: return "macos";
        case Platform::Linux: return "linux";
    }
    return "unknown";
}

LinuxDistro parseOsRelease(const std::string& text) {
    LinuxDistro distro;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        const std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        if (key == "ID") distro.id = value;
        else if (key == "VERSION_ID") distro.versionId = value;
        else if (key == "ID_LIKE") {
            std::istringstream words(value);
            std::string word;
            while (words >> word) distro.idLike.push_back(word);
        }
    }
    return distro;
}

std::string distroTag(const LinuxDistro& distro) {
    std::string tag;
    for (char c : lower(distro.id + distro.versionId)) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_') tag += c;
    }
    return distro.id.empty() || tag.empty() ? "linux" : tag;
}

bool makePlan(const PlanInput& in, Plan& plan, std::string& error) {
    plan = Plan{};
    std::error_code ec;
    const Config& config = in.config;
    if (!fs::is_directory(in.binDir, ec)) {
        error = "no bin/ folder; build the project first";
        return false;
    }

    // --- The built executable / bundle ---
    fs::path executable;
    auto findSingle = [&](const std::string& extension, bool directory) {
        fs::path found;
        int count = 0;
        for (const auto& entry : fs::directory_iterator(in.binDir, ec)) {
            if (lower(entry.path().extension().string()) != extension) continue;
            if (directory ? !entry.is_directory(ec) : !entry.is_regular_file(ec)) continue;
            found = entry.path();
            ++count;
        }
        return count == 1 ? found : fs::path();
    };
    if (in.platform == Platform::Windows) {
        executable = in.binDir / (in.projectName + ".exe");
        if (!fs::is_regular_file(executable, ec)) executable = findSingle(".exe", false);
    } else if (in.platform == Platform::MacOS) {
        plan.bundle = in.binDir / (in.projectName + ".app");
        if (!fs::is_directory(plan.bundle, ec)) plan.bundle = findSingle(".app", true);
        if (!plan.bundle.empty()) {
            plan.bundleName = toUtf8(plan.bundle.filename());
            executable = plan.bundle / "Contents" / "MacOS" / plan.bundle.stem();
        }
    } else {
        executable = in.binDir / in.projectName;
    }
    if (executable.empty() || !fs::is_regular_file(executable, ec)) {
        error = "no built " + std::string(in.platform == Platform::MacOS ? "app bundle" : "executable") +
                " for \"" + in.projectName + "\" in bin/; build the project first";
        return false;
    }
    const BinaryInfo exeInfo = readBinaryInfo(executable);
    plan.arch = exeInfo.arch.empty() ? "unknown" : exeInfo.arch;
    if (in.platform != Platform::MacOS) plan.executableName = toUtf8(executable.filename());

    const std::string name = fileSafeName(config.name);
    const std::string platform = in.platformTag.empty() ? platformName(in.platform) : in.platformTag;
    plan.folderName = name + "-" + config.version + "-" + platform + "-" + plan.arch;
    plan.packageDir = in.outputDir / fromUtf8(plan.folderName);

    std::set<std::string> targets;
    auto addFile = [&](const fs::path& source, const std::string& logical, const std::string& target,
                       const std::string& link = {}) {
        if (isExcluded(config.exclude, logical)) {
            plan.excluded.push_back(logical);
            return;
        }
        if (!targets.insert(lower(target)).second) {
            plan.warnings.push_back(target + " comes from two places; kept the first");
            return;
        }
        plan.files.push_back({source, target, link});
    };
    // Where a logical path ("data/a.png", an include "to") lands in the package.
    const std::string resources = plan.bundleName + "/Contents/Resources/";
    auto targetOf = [&](const std::string& logical) {
        return in.platform == Platform::MacOS ? resources + logical : logical;
    };

    // Every file and symlink under `root`, with its path relative to `root`.
    // Paths are taken lexically: fs::relative() would resolve a symlink to its
    // target, so libfoo.so.1 -> libfoo.so.1.2 would become a second copy of
    // libfoo.so.1.2 and the name the executable loads would be lost. A symlink
    // is kept as one (`link`) when its target is relative and stays under
    // `root`; any other symlink is an error, since the package would not hold
    // what it points to. Symlinked folders are not entered. `packagePrefix` is
    // where `root` lands in the package, for the "include" hint of that error
    // (nullptr: no hint, as for the macOS bundle).
    auto walk = [&](const fs::path& root, const char* packagePrefix, const auto& visit) -> bool {
        for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) {
                error = "cannot read " + toUtf8(root) + ": " + ec.message();
                return false;
            }
            const fs::path path = it->path();
            const std::string rel = toUtf8(path.lexically_relative(root));
            if (it->is_symlink(ec)) {
                const fs::path pointsTo = fs::read_symlink(path, ec);
                const fs::path inside =
                    (path.parent_path() / pointsTo).lexically_normal().lexically_relative(root.lexically_normal());
                if (ec || pointsTo.empty() || pointsTo.is_absolute() || inside.empty() ||
                    *inside.begin() == "..") {
                    error = "the symlink " + toUtf8(path) + " -> " + toUtf8(pointsTo) +
                            " points outside " + toUtf8(root) +
                            "; only relative links to files in the same folder tree are packaged";
                    if (packagePrefix) {
                        const fs::path from = pointsTo.is_absolute()
                                                  ? pointsTo
                                                  : (path.parent_path() / pointsTo).lexically_normal();
                        error += ".\nCopy the files in, or list them in \"include\" in dist.json (they are copied):\n"
                                 "  {\"from\": " + json(toUtf8(from)).dump() + ", \"to\": " +
                                 json(std::string(packagePrefix) + rel).dump() + "}";
                    }
                    return false;
                }
                if (!fs::exists(path, ec)) {
                    error = "the symlink " + toUtf8(path) + " -> " + toUtf8(pointsTo) + " points to nothing";
                    return false;
                }
                visit(path, rel, toUtf8(pointsTo));
            } else if (it->is_regular_file(ec)) {
                visit(path, rel, std::string());
            }
        }
        if (ec) {
            error = "cannot read " + toUtf8(root) + ": " + ec.message();
            return false;
        }
        return true;
    };

    // --- bin/ ---
    if (in.platform == Platform::MacOS) {
        // The bundle is copied whole with its links (framework Versions/Current
        // and the like). Each one has to stay inside the bundle: a link out of
        // it would be copied as a link, and writing bin/data into the copy would
        // then reach the folder it points to.
        if (!walk(plan.bundle, nullptr, [](const fs::path&, const std::string&, const std::string&) {}))
            return false;
        const fs::path data = in.binDir / "data";
        if (fs::is_directory(data, ec)) {
            const bool ok = walk(data, "data/", [&](const fs::path& path, const std::string& rel, const std::string& link) {
                const std::string name = path.filename().string();
                if (name == ".DS_Store" || name == ".gitkeep") return;
                const std::string logical = "data/" + rel;
                addFile(path, logical, targetOf(logical), link);
            });
            if (!ok) return false;
        }
    } else {
        const std::string exeRel = toUtf8(executable.lexically_relative(in.binDir));
        if (isExcluded(config.exclude, exeRel)) {
            error = "exclude matches the executable " + exeRel;
            return false;
        }
        const bool ok = walk(in.binDir, "", [&](const fs::path& path, const std::string& rel, const std::string& link) {
            const std::string fileName = lower(path.filename().string());
            const std::string ext = lower(path.extension().string());
            if (fileName == ".ds_store" || fileName == "thumbs.db" || fileName == ".gitkeep") return;
            if (in.platform == Platform::Windows && (ext == ".ilk" || ext == ".exp" || ext == ".lib")) return;
            if (in.platform == Platform::Windows && ext == ".pdb") {
                plan.symbols.push_back({path, rel, link});
                return;
            }
            addFile(path, rel, rel, link);
        });
        if (!ok) return false;
    }

    // --- include ---
    for (const auto& entry : config.include) {
        fs::path from = fromUtf8(entry.from);
        if (from.is_relative()) from = in.projectDir / from;
        std::string to;
        if (!cleanPackagePath(entry.to, to)) {
            error = "include \"to\" must be a relative path inside the package: \"" + entry.to + "\"";
            return false;
        }
        if (fs::is_directory(from, ec)) {
            const std::string prefix = to + "/";
            const bool ok = walk(from, prefix.c_str(), [&](const fs::path& path, const std::string& rel, const std::string& link) {
                const std::string logical = to + "/" + rel;
                addFile(path, logical, targetOf(logical), link);
            });
            if (!ok) return false;
        } else if (fs::is_regular_file(from, ec)) {
            const std::string logical = endsWith(entry.to, "/") ? to + "/" + toUtf8(from.filename()) : to;
            addFile(from, logical, targetOf(logical));
        } else {
            error = "include \"from\" not found: " + entry.from;
            return false;
        }
    }

    // --- EULA: the package root, beside the bundle on macOS ---
    // eulaFile if set (must exist), else EULA.txt in the project folder if present.
    {
        fs::path eula = fromUtf8(config.eulaFile.empty() ? std::string(kDefaultEula) : config.eulaFile);
        if (eula.is_relative()) eula = in.projectDir / eula;
        if (fs::is_regular_file(eula, ec)) {
            if (!targets.insert("eula.txt").second) {
                error = "EULA.txt is also in bin/ or an include entry; keep one";
                return false;
            }
            plan.files.push_back({eula, "EULA.txt"});
            plan.eulaSource = eula;
        } else if (!config.eulaFile.empty()) {
            error = "dist.json \"eulaFile\" not found: " + config.eulaFile;
            return false;
        }
    }

    // --- Windows runtime ---
    if (in.platform == Platform::Windows) {
        std::set<std::string> imports;
        std::vector<std::string> debugCrt, mediaFoundation;
        auto scan = [&](const fs::path& file) {
            const BinaryInfo info = readBinaryInfo(file);
            for (const auto& dll : info.imports) {
                imports.insert(dll);
                if (isDebugCrt(dll)) debugCrt.push_back(toUtf8(file.filename()) + " -> " + dll);
                if (isMediaFoundation(dll)) mediaFoundation.push_back(dll);
            }
        };
        for (const auto& item : plan.files) {
            const std::string ext = lower(item.source.extension().string());
            if (ext == ".exe" || ext == ".dll") scan(item.source);
        }
        if (!debugCrt.empty()) {
            error = "this is a Debug build (imports the debug C++ runtime, which may not be "
                    "redistributed): " + debugCrt.front() + ". Package a RelWithDebInfo or Release build.";
            return false;
        }
        if (!mediaFoundation.empty()) {
            plan.warnings.push_back("imports Media Foundation (" + mediaFoundation.front() +
                                    "), which Windows N / KN editions lack: the app will not start "
                                    "there unless the Media Feature Pack is installed");
        }
        bool needsRuntime = false;
        for (const auto& dll : imports) needsRuntime = needsRuntime || isRuntimeImport(dll);
        if (needsRuntime && in.vcRuntimeDir.empty()) {
            plan.warnings.push_back("Visual Studio's VC++ runtime folder was not found: the target "
                                    "PC needs the Visual C++ Redistributable installed");
        } else if (needsRuntime) {
            // Copy the runtime DLLs imported by the package, then those they import.
            for (bool added = true; added;) {
                added = false;
                for (const auto& entry : fs::directory_iterator(in.vcRuntimeDir, ec)) {
                    const std::string dll = lower(entry.path().filename().string());
                    if (!imports.count(dll) || targets.count(dll)) continue;
                    targets.insert(dll);
                    plan.files.push_back({entry.path(), toUtf8(entry.path().filename())});
                    plan.runtimeDlls.push_back(toUtf8(entry.path().filename()));
                    for (const auto& next : readBinaryInfo(entry.path()).imports) imports.insert(next);
                    added = true;
                }
            }
        }
    }

    if (!plan.symbols.empty())
        plan.symbolsDir = in.outputDir / fromUtf8(plan.folderName + "-symbols");
    if (in.platform == Platform::Linux) plan.launcherName = in.projectName + ".sh";
    for (const auto& source : in.licenses)
        if (source.files.empty() && startsWith(source.note, "file not found"))
            plan.warnings.push_back("dist.json licenses \"" + source.component + "\": " + source.note);
    return true;
}

namespace {

// True when writing or removing `target` stays inside `root`: `target` is
// lexically under `root`, and none of the folders between them that exist
// yet, nor `target` itself, is a symlink that could lead somewhere else.
bool staysInside(const fs::path& root, const fs::path& target, std::string& error) {
    const fs::path rel = target.lexically_normal().lexically_relative(root.lexically_normal());
    if (rel.empty() || *rel.begin() == ".." || rel.is_absolute()) {
        error = "refusing to write outside " + toUtf8(root) + ": " + toUtf8(target);
        return false;
    }
    fs::path at = root;
    for (const auto& part : rel) {
        at /= part;
        std::error_code ec;
        const fs::file_status status = fs::symlink_status(at, ec);
        if (ec || !fs::exists(status)) return true;  // the rest is created by us
        if (fs::is_symlink(status)) {
            error = "refusing to write through the symlink " + toUtf8(at);
            return false;
        }
    }
    return true;
}

} // namespace

bool writePackage(const PlanInput& in, const Plan& plan, std::string& error) {
    std::error_code ec;
    // Everything below is written inside the output folder, so it has to be a
    // real absolute path: an empty one would make the package folder relative
    // to the current directory, and replacing it would delete a folder there.
    if (in.outputDir.empty() || !in.outputDir.is_absolute()) {
        error = "the output folder must be an absolute path (got \"" + toUtf8(in.outputDir) + "\")";
        return false;
    }
    // Only ever replace the two folders this plan names, inside the output folder.
    for (const fs::path& dir : {plan.packageDir, plan.symbolsDir}) {
        if (dir.empty()) continue;
        if (dir.parent_path() != in.outputDir) {
            error = "refusing to replace a folder outside the output folder: " + toUtf8(dir);
            return false;
        }
        fs::remove_all(dir, ec);
        if (ec) { error = "cannot remove " + toUtf8(dir) + ": " + ec.message(); return false; }
    }
    fs::create_directories(plan.packageDir, ec);
    if (ec) { error = "cannot create " + toUtf8(plan.packageDir) + ": " + ec.message(); return false; }

    if (!plan.bundle.empty()) {
        const fs::path bundle = plan.packageDir / fromUtf8(plan.bundleName);
        // Links are copied as links; makePlan has checked that they all stay
        // inside the bundle.
        fs::copy(plan.bundle, bundle, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) { error = "cannot copy " + toUtf8(plan.bundle) + ": " + ec.message(); return false; }
        // Replaced by bin/data below. Never through a link: Contents/Resources
        // as a link would make this remove a folder outside the package.
        const fs::path data = bundle / "Contents" / "Resources" / "data";
        if (!staysInside(plan.packageDir, data, error)) return false;
        fs::remove_all(data, ec);
        if (ec) { error = "cannot remove " + toUtf8(data) + ": " + ec.message(); return false; }
    }

    auto copyItems = [&](const std::vector<CopyItem>& items, const fs::path& root) {
        for (const auto& item : items) {
            const fs::path target = root / fromUtf8(item.target);
            if (!staysInside(root, target, error)) return false;
            fs::create_directories(target.parent_path(), ec);
            if (!item.link.empty() && in.platform != Platform::Windows) {
                // The same relative link: its target is in the package too.
                fs::remove(target, ec);
                fs::create_symlink(fromUtf8(item.link), target, ec);
                if (ec) {
                    error = "cannot create the link " + toUtf8(target) + ": " + ec.message();
                    return false;
                }
                continue;
            }
            // Windows needs a privilege to create links: a copy under the link's name.
            fs::copy_file(item.source, target, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "cannot copy " + toUtf8(item.source) + ": " + ec.message();
                return false;
            }
        }
        return true;
    };
    if (!copyItems(plan.files, plan.packageDir)) return false;
    if (!plan.symbolsDir.empty()) {
        fs::create_directories(plan.symbolsDir, ec);
        if (!copyItems(plan.symbols, plan.symbolsDir)) return false;
    }

    // --- licenses/ and THIRD_PARTY_NOTICES.txt ---
    std::string notices = "Third-party notices for " + in.config.name + " " + in.config.version + "\n";
    if (!in.config.author.empty()) notices += copyrightLine(in.config.author) + "\n";
    notices += "Collected by trusscli dist from the files the components ship.\n";
    const fs::path licenses = plan.packageDir / "licenses";
    for (const auto& source : in.licenses) {
        notices += "\n" + std::string(78, '=') + "\n" + source.component;
        if (!source.declared.empty()) notices += " (" + source.declared + ")";
        notices += "\n" + std::string(78, '=') + "\n";
        if (source.files.empty()) notices += "(" + source.note + ")\n";
        const fs::path dir = licenses / fromUtf8(fileSafeName(source.component));
        for (const auto& file : source.files) {
            if (!staysInside(plan.packageDir, dir / file.filename(), error)) return false;
            fs::create_directories(dir, ec);
            fs::copy_file(file, dir / file.filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "cannot copy the license file " + toUtf8(file) + ": " + ec.message();
                return false;
            }
            std::string text;
            if (readFile(file, text)) {
                notices += "\n--- " + toUtf8(file.filename()) + " ---\n" + text;
                if (!text.empty() && text.back() != '\n') notices += '\n';
            }
        }
    }
    if (!plan.runtimeDlls.empty()) {
        std::string note = "Microsoft Visual C++ runtime\n\nThese files are the Microsoft Visual C++ "
                           "runtime, copied next to the application (app-local deployment) from the "
                           "Visual Studio installation that built it:\n\n";
        for (const auto& dll : plan.runtimeDlls) note += "  " + dll + "\n";
        note += "\nSource folder: " + toUtf8(in.vcRuntimeDir) + "\n"
                "They are covered by the license terms of that Visual Studio installation.\n";
        const fs::path dir = licenses / "Microsoft-VC-Runtime";
        if (!staysInside(plan.packageDir, dir / "README.txt", error)) return false;
        fs::create_directories(dir, ec);
        if (!writeFile(dir / "README.txt", note)) {
            error = "cannot write " + toUtf8(dir / "README.txt");
            return false;
        }
        notices += "\n" + std::string(78, '=') + "\n" + note;
    }
    if (!staysInside(plan.packageDir, plan.packageDir / "THIRD_PARTY_NOTICES.txt", error)) return false;
    if (!writeFile(plan.packageDir / "THIRD_PARTY_NOTICES.txt", notices)) {
        error = "cannot write THIRD_PARTY_NOTICES.txt";
        return false;
    }

    // --- macOS bundle version ---
    if (!plan.bundle.empty()) {
        const fs::path plistPath = plan.packageDir / fromUtf8(plan.bundleName) / "Contents" / "Info.plist";
        std::string plist;
        if (!staysInside(plan.packageDir, plistPath, error)) return false;
        if (readFile(plistPath, plist) && !startsWith(plist, "bplist")) {
            setPlistString(plist, "CFBundleShortVersionString", in.config.version);
            setPlistString(plist, "CFBundleVersion", in.config.version);
            if (!in.config.author.empty())
                setPlistString(plist, "NSHumanReadableCopyright", copyrightLine(in.config.author));
            if (!writeFile(plistPath, plist)) {
                error = "cannot write " + toUtf8(plistPath);
                return false;
            }
        }
    }

    // --- Linux launcher ---
    if (!plan.launcherName.empty()) {
        const fs::path launcher = plan.packageDir / fromUtf8(plan.launcherName);
        const std::string script =
            "#!/bin/sh\n"
            "# Starts " + in.projectName + " with the libraries next to it.\n"
            "# readlink -f: also when started through a link (/usr/bin/<package>).\n"
            "here=\"$(cd \"$(dirname \"$(readlink -f \"$0\")\")\" && pwd)\"\n"
            "LD_LIBRARY_PATH=\"$here${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\" exec \"$here/" +
            in.projectName + "\" \"$@\"\n";
        if (!staysInside(plan.packageDir, launcher, error)) return false;
        if (!writeFile(launcher, script)) {
            error = "cannot write " + plan.launcherName;
            return false;
        }
        fs::permissions(launcher,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
    }
    return true;
}

std::string formatPlan(const Plan& plan) {
    std::string out = plan.folderName + "/\n";
    if (!plan.bundleName.empty()) out += "  " + plan.bundleName + "/  (the whole bundle)\n";
    std::vector<std::string> files;
    for (const auto& item : plan.files)
        files.push_back(item.target + (item.link.empty() ? "" : "  -> " + item.link));
    std::sort(files.begin(), files.end());
    for (const auto& f : files) out += "  " + f + "\n";
    out += "  licenses/, THIRD_PARTY_NOTICES.txt  (always included)\n";
    if (!plan.launcherName.empty()) out += "  " + plan.launcherName + "  (launcher)\n";
    if (!plan.symbols.empty()) {
        out += plan.folderName + "-symbols/\n";
        for (const auto& item : plan.symbols) out += "  " + item.target + "\n";
    }
    if (!plan.excluded.empty()) {
        out += "Left out by exclude:\n";
        for (const auto& e : plan.excluded) out += "  " + e + "\n";
    }
    return out;
}

// -----------------------------------------------------------------------------
// Windows installer (NSIS)
// -----------------------------------------------------------------------------

std::vector<std::string> listPackageFiles(const fs::path& packageDir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(packageDir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec)) files.push_back(toUtf8(it->path().lexically_relative(packageDir)));
    }
    std::sort(files.begin(), files.end());
    return files;
}

namespace {

// An NSIS string literal: "$" -> "$$", '"' -> '$\"'.
std::string nsisQuote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '$') out += "$$";
        else if (c == '"') out += "$\\\"";
        else if (c == '\r' || c == '\n') out += ' ';
        else out += c;
    }
    return out + "\"";
}

std::string windowsPath(const fs::path& path) {
    std::string text = toUtf8(path);
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

// A folder / shortcut name: the display name without characters Windows rejects.
std::string displaySafeName(const std::string& name) {
    std::string out;
    for (unsigned char c : name)
        if (c >= 0x20 && !std::strchr("<>:\"/\\|?*", c)) out += static_cast<char>(c);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(0, 1);
    return out.empty() ? fileSafeName(name) : out;
}

} // namespace

std::string nsisScript(const NsisInput& in) {
    const std::string name = displaySafeName(in.name);
    const std::string uninstallKey =
        "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + fileSafeName(in.name);
    std::string version4 = in.version;  // VIProductVersion needs four numbers
    for (size_t parts = std::count(version4.begin(), version4.end(), '.') + 1; parts < 4; ++parts)
        version4 += ".0";
    auto q = nsisQuote;
    // A path under an NSIS variable ($INSTDIR, $SMPROGRAMS, ...): the variable
    // stays a variable, the rest is escaped.
    auto under = [&](const char* variable, const std::string& rest) {
        return std::string("\"") + variable + "\\" + q(rest).substr(1);
    };

    std::string s;
    s += "; Generated by trusscli dist. Build with: makensis /INPUTCHARSET UTF8 <this file>\n";
    s += "Unicode true\n";
    s += "ManifestDPIAware true\n";
    s += "SetCompressor /SOLID zlib\n";  // zlib/libpng like the rest of NSIS
    s += "RequestExecutionLevel admin\n";
    s += "!include \"MUI2.nsh\"\n\n";
    s += "Name " + q(in.name) + "\n";
    // Bottom line of every installer page; " " because an empty text would
    // bring back NSIS's own "Nullsoft Install System" line.
    s += "BrandingText " + (in.author.empty() ? std::string("\" \"") : q(copyrightLine(in.author))) + "\n";
    s += "OutFile " + q(windowsPath(in.setupExe)) + "\n";
    s += std::string("InstallDir \"") + (in.arch == "x86" ? "$PROGRAMFILES" : "$PROGRAMFILES64") +
         "\\" + q(name).substr(1) + "\n";
    s += "InstallDirRegKey HKLM " + q(uninstallKey) + " \"InstallLocation\"\n";
    s += "VIProductVersion " + q(version4) + "\n";
    s += "VIAddVersionKey \"ProductName\" " + q(in.name) + "\n";
    s += "VIAddVersionKey \"ProductVersion\" " + q(in.version) + "\n";
    s += "VIAddVersionKey \"FileVersion\" " + q(in.version) + "\n";
    s += "VIAddVersionKey \"FileDescription\" " + q(in.name + " Setup") + "\n";
    if (!in.author.empty()) {
        s += "VIAddVersionKey \"CompanyName\" " + q(in.author) + "\n";
        s += "VIAddVersionKey \"LegalCopyright\" " + q(copyrightLine(in.author)) + "\n";
    }
    s += "\n!define MUI_ABORTWARNING\n";
    if (!in.eulaPage.empty())
        s += "!insertmacro MUI_PAGE_LICENSE " + q(windowsPath(in.eulaPage)) + "\n";
    if (!in.noticesPage.empty()) {
        // A license page with a plain Next button: shown, not agreed to.
        s += "!define MUI_PAGE_HEADER_TEXT \"Third-party notices\"\n";
        s += "!define MUI_PAGE_HEADER_SUBTEXT \"Software included in this application\"\n";
        s += "!define MUI_LICENSEPAGE_TEXT_TOP \"These components are part of the application.\"\n";
        s += "!define MUI_LICENSEPAGE_TEXT_BOTTOM \"Click Next to continue.\"\n";
        s += "!define MUI_LICENSEPAGE_BUTTON \"$(^NextBtn)\"\n";
        s += "!insertmacro MUI_PAGE_LICENSE " + q(windowsPath(in.noticesPage)) + "\n";
    }
    s += "!define MUI_COMPONENTSPAGE_NODESC\n";
    s += "!insertmacro MUI_PAGE_COMPONENTS\n";
    s += "!insertmacro MUI_PAGE_DIRECTORY\n";
    s += "!insertmacro MUI_PAGE_INSTFILES\n";
    s += "!define MUI_FINISHPAGE_RUN " + under("$INSTDIR", in.executable) + "\n";
    s += "!insertmacro MUI_PAGE_FINISH\n";
    s += "!insertmacro MUI_UNPAGE_CONFIRM\n";
    s += "!insertmacro MUI_UNPAGE_INSTFILES\n";
    s += "!insertmacro MUI_LANGUAGE \"English\"\n\n";
    const std::string shortcutName = name + ".lnk";

    // "-" hides the required section; "/o" starts a checkbox unchecked.
    s += "Section \"-Install\"\n";
    s += "  SetShellVarContext all\n";
    s += "  SetOutPath \"$INSTDIR\"\n";
    s += "  File /r " + q(windowsPath(in.packageDir) + "\\*.*") + "\n";
    s += "  WriteUninstaller \"$INSTDIR\\Uninstall.exe\"\n";
    s += "  WriteRegStr HKLM " + q(uninstallKey) + " \"DisplayName\" " + q(in.name) + "\n";
    s += "  WriteRegStr HKLM " + q(uninstallKey) + " \"DisplayVersion\" " + q(in.version) + "\n";
    if (!in.author.empty())
        s += "  WriteRegStr HKLM " + q(uninstallKey) + " \"Publisher\" " + q(in.author) + "\n";
    s += "  WriteRegStr HKLM " + q(uninstallKey) + " \"InstallLocation\" \"$INSTDIR\"\n";
    s += "  WriteRegStr HKLM " + q(uninstallKey) + " \"DisplayIcon\" " + under("$INSTDIR", in.executable) + "\n";
    s += "  WriteRegStr HKLM " + q(uninstallKey) + " \"UninstallString\" \"$\\\"$INSTDIR\\Uninstall.exe$\\\"\"\n";
    s += "  WriteRegDWORD HKLM " + q(uninstallKey) + " \"NoModify\" 1\n";
    s += "  WriteRegDWORD HKLM " + q(uninstallKey) + " \"NoRepair\" 1\n";
    s += "SectionEnd\n\n";

    s += "Section \"Start menu and desktop shortcuts\"\n";
    s += "  SetShellVarContext all\n";
    s += "  CreateDirectory " + under("$SMPROGRAMS", name) + "\n";
    s += "  CreateShortcut " + under("$SMPROGRAMS", name + "\\" + shortcutName) + " " + under("$INSTDIR", in.executable) + "\n";
    s += "  CreateShortcut " + under("$DESKTOP", shortcutName) + " " + under("$INSTDIR", in.executable) + "\n";
    s += "SectionEnd\n\n";

    s += "Section /o " + q("Start " + in.name + " when Windows starts") + "\n";
    s += "  SetShellVarContext all\n";
    s += "  CreateShortcut " + under("$SMSTARTUP", shortcutName) + " " + under("$INSTDIR", in.executable) + "\n";
    s += "SectionEnd\n\n";

    // Uninstall exactly what was installed; folders only once empty.
    std::set<std::string> folders;
    s += "Section \"Uninstall\"\n";
    s += "  SetShellVarContext all\n";
    for (const auto& file : in.files) {
        std::string path = file;
        std::replace(path.begin(), path.end(), '/', '\\');
        s += "  Delete " + under("$INSTDIR", path) + "\n";
        for (size_t slash = path.find('\\'); slash != std::string::npos; slash = path.find('\\', slash + 1))
            folders.insert(path.substr(0, slash));
    }
    s += "  Delete \"$INSTDIR\\Uninstall.exe\"\n";
    std::vector<std::string> deepestFirst(folders.begin(), folders.end());
    std::sort(deepestFirst.begin(), deepestFirst.end(), [](const std::string& a, const std::string& b) {
        return std::count(a.begin(), a.end(), '\\') > std::count(b.begin(), b.end(), '\\') ||
               (std::count(a.begin(), a.end(), '\\') == std::count(b.begin(), b.end(), '\\') && a > b);
    });
    for (const auto& folder : deepestFirst) s += "  RMDir " + under("$INSTDIR", folder) + "\n";
    s += "  RMDir \"$INSTDIR\"\n";
    // Whatever was checked at install time: deleting a missing file is a no-op.
    s += "  Delete " + under("$SMPROGRAMS", name + "\\" + shortcutName) + "\n";
    s += "  RMDir " + under("$SMPROGRAMS", name) + "\n";
    s += "  Delete " + under("$DESKTOP", shortcutName) + "\n";
    s += "  Delete " + under("$SMSTARTUP", shortcutName) + "\n";
    s += "  DeleteRegKey HKLM " + q(uninstallKey) + "\n";
    s += "SectionEnd\n";
    return s;
}

bool writeWithBom(const fs::path& source, const fs::path& target) {
    std::string text;
    if (!readFile(source, text)) return false;
    const bool hasBom = startsWith(text, "\xEF\xBB\xBF") || startsWith(text, "\xFF\xFE");
    return writeFile(target, hasBom ? text : "\xEF\xBB\xBF" + text);
}

// -----------------------------------------------------------------------------
// Linux packages
// -----------------------------------------------------------------------------

LinuxPackageFormat packageFormatFor(const LinuxDistro& distro) {
    auto formatOf = [](const std::string& id) {
        if (id == "arch") return LinuxPackageFormat::Pacman;
        if (id == "debian" || id == "ubuntu") return LinuxPackageFormat::Deb;
        return LinuxPackageFormat::None;
    };
    LinuxPackageFormat format = formatOf(distro.id);
    for (size_t i = 0; format == LinuxPackageFormat::None && i < distro.idLike.size(); ++i)
        format = formatOf(distro.idLike[i]);
    return format;
}

std::string linuxPackageName(const std::string& name) {
    std::string out;
    for (char c : lower(name)) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (c == '+' || c == '.' || c == '_' || c == '-') { if (!out.empty()) out += c; }
        else if (c == ' ' && !out.empty()) out += '-';
    }
    while (!out.empty() && !std::isalnum(static_cast<unsigned char>(out.back())) && out.back() != '+')
        out.pop_back();
    return out.empty() ? "app" : out;
}

std::string pacmanArch(const std::string& arch) {
    if (arch == "x64") return "x86_64";
    if (arch == "arm64") return "aarch64";
    if (arch == "x86") return "i686";
    return arch;
}

std::vector<std::string> parseNeeded(const std::string& objdumpOutput) {
    std::vector<std::string> out;
    std::istringstream lines(objdumpOutput);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string key, value;
        if (words >> key >> value && key == "NEEDED") out.push_back(value);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> parseLdd(const std::string& lddOutput) {
    // "\tlibz.so.1 => /usr/lib/libz.so.1 (0x...)"
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream lines(lddOutput);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string name, arrow, path;
        if (!(words >> name >> arrow >> path) || arrow != "=>" || path.empty() || path[0] != '/') continue;
        out.emplace_back(name, path);
    }
    return out;
}

namespace {

// One line, for a key=value file.
std::string oneLine(std::string text) {
    for (char& c : text) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return text;
}

// A .desktop Exec argument: quoted when it has characters the spec reserves.
std::string desktopExecArg(const std::string& path) {
    if (path.find_first_of(" \t\"'\\><~|&;$*?#()`") == std::string::npos) return path;
    std::string out = "\"";
    for (char c : path) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// A bash word in single quotes.
std::string shellQuote(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

} // namespace

std::string desktopEntry(const DesktopEntryInput& input) {
    return "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=" + oneLine(input.name) + "\n"
           "Exec=" + desktopExecArg(oneLine(input.exec)) + "\n"
           "Icon=" + oneLine(input.icon) + "\n"
           "Terminal=false\n" +
           (input.wmClass.empty() ? std::string() : "StartupWMClass=" + oneLine(input.wmClass) + "\n");
}

std::string pkgbuild(const PacmanInput& in) {
    const std::string opt = "/opt/" + in.pkgname;
    std::string s;
    s += "# Generated by trusscli dist --installer; packages the dist folder as it is.\n";
    s += "pkgname=" + in.pkgname + "\n";
    s += "pkgver=" + in.version + "\n";
    s += "pkgrel=1\n";
    s += "pkgdesc=" + shellQuote(oneLine(in.description)) + "\n";
    s += "arch=(" + shellQuote(in.arch) + ")\n";
    s += "license=('custom')\n";
    s += "depends=(";
    for (size_t i = 0; i < in.depends.size(); ++i) s += (i ? " " : "") + shellQuote(in.depends[i]);
    s += ")\n";
    // No -debug package: there are no sources to go with it.
    s += "options=('!debug')\n";
    s += "\n";
    s += "package() {\n";
    s += "  install -d \"$pkgdir" + opt + "\"\n";
    s += "  cp -a " + shellQuote(toUtf8(in.packageDir) + "/.") + " \"$pkgdir" + opt + "/\"\n";
    s += "  install -d \"$pkgdir/usr/bin\"\n";
    s += "  ln -s " + shellQuote(opt + "/" + in.launcher) + " \"$pkgdir/usr/bin/" + in.pkgname + "\"\n";
    s += "  install -Dm644 " + shellQuote(toUtf8(in.desktopFile)) +
         " \"$pkgdir/usr/share/applications/" + in.pkgname + ".desktop\"\n";
    if (!in.icon.empty()) {
        s += "  install -Dm644 " + shellQuote(toUtf8(in.icon)) +
             " \"$pkgdir/usr/share/pixmaps/" + in.pkgname + ".png\"\n";
    }
    // Where pacman users look for a package's licenses.
    s += "  install -d \"$pkgdir/usr/share/licenses\"\n";
    s += "  ln -s " + shellQuote(opt + "/licenses") + " \"$pkgdir/usr/share/licenses/" + in.pkgname + "\"\n";
    s += "}\n";
    return s;
}

std::string debArch(const std::string& arch) {
    if (arch == "x64") return "amd64";
    if (arch == "x86") return "i386";
    return arch;  // "arm64" is the same in both
}

std::string parseShlibdeps(const std::string& output) {
    std::istringstream lines(output);
    std::string line;
    const std::string key = "shlibs:Depends=";
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (startsWith(line, key)) return line.substr(key.size());
    }
    return {};
}

std::string debControl(const DebInput& in) {
    std::string s;
    s += "Package: " + in.package + "\n";
    s += "Version: " + in.version + "\n";
    s += "Architecture: " + in.arch + "\n";
    s += "Maintainer: " + oneLine(in.maintainer) + "\n";
    s += "Installed-Size: " + std::to_string(in.installedSizeKiB) + "\n";
    if (!in.depends.empty()) s += "Depends: " + oneLine(in.depends) + "\n";
    s += "Section: misc\n";
    s += "Priority: optional\n";
    s += "Description: " + oneLine(in.description) + "\n";
    return s;
}

} // namespace dist
