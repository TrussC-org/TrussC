#include "TrussCVersion.h"
#include "AddonVersions.h"
#include "ProcessCapture.h"
#include "GeneratedFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <cmath>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <memory>
#include <sstream>
#include <stdexcept>

namespace trusscVersion {
namespace fs = std::filesystem;
using Json = nlohmann::json;

static void rejectOldLayout(const Spec& spec) {
    throw OldLayoutError("TrussC " + spec.ref +
        " predates the core/ layout; trusscli supports v0.5.0 and later. Run `trusscli use v0.5.0` (or newer).");
}

static void checkLayout(const Spec& spec, const fs::path& root) {
    if (!fs::is_regular_file(root / "core/cmake/trussc_app.cmake") && fs::is_directory(root / "trussc"))
        rejectOldLayout(spec);
}

static bool namePart(const std::string& part) {
    return !part.empty() && part.front() != '.' && part.front() != '-' &&
        part.back() != '.' && part.find("..") == std::string::npos &&
        std::all_of(part.begin(), part.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        });
}

Spec parse(const std::string& text) {
    Spec spec;
    auto at = text.find('@');
    spec.ref = text;
    if (at != std::string::npos) {
        auto repo = text.substr(0, at);
        auto slash = repo.find('/');
        if (slash == std::string::npos || !namePart(repo.substr(0, slash)) ||
            !namePart(repo.substr(slash + 1))) {
            throw std::runtime_error("invalid TrussC version: expected [owner/repo@]ref");
        }
        spec.owner = repo.substr(0, slash);
        spec.repo = repo.substr(slash + 1);
        spec.ref = text.substr(at + 1);
    }
    // Git refs may contain slash-separated components. Each stays within the
    // cache root; Git itself checks its remaining ref syntax during resolution.
    std::istringstream parts(spec.ref);
    std::string part;
    if (spec.ref.empty() || spec.ref.back() == '/') {
        throw std::runtime_error("invalid TrussC version: empty ref");
    }
    while (std::getline(parts, part, '/')) {
        bool forbidden = std::any_of(part.begin(), part.end(), [](unsigned char c) {
            return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c >= 128 || c == '-' ||
                     c == '_' || c == '.' || c == '+');
        });
        if (part.empty() || part.front() == '.' || part.back() == '.' ||
            spec.ref.front() == '-' || part.find("..") != std::string::npos || forbidden ||
            (part.size() >= 5 && part.substr(part.size() - 5) == ".lock")) {
            throw std::runtime_error("invalid TrussC version: malformed ref");
        }
    }
    spec.commit = spec.ref.size() == 40 && std::all_of(spec.ref.begin(), spec.ref.end(),
        [](unsigned char c) { return (c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
    return spec;
}

static Json presets(const fs::path& project) {
    std::ifstream file(project / "CMakePresets.json");
    return file ? Json::parse(file) : Json::object();
}

std::string localPath(const fs::path& project) {
    Json data;
    try { data = presets(project); }
    catch (const Json::parse_error&) { return {}; } // Existing update repairs malformed presets.
    if (!data.contains("vendor") || !data["vendor"].contains("trussc") ||
        !data["vendor"]["trussc"].contains("localPath")) return {};
    auto value = data["vendor"]["trussc"]["localPath"];
    if (!value.is_string() || value.get<std::string>().empty()) {
        throw std::runtime_error("vendor.trussc.localPath must be a nonempty path string");
    }
    return value.get<std::string>();
}

void setLocalPath(const fs::path& project, const std::string& path) {
    auto data = presets(project);
    if (path.empty()) {
        if (!data.contains("vendor") || !data["vendor"].is_object() ||
            !data["vendor"].contains("trussc") || !data["vendor"]["trussc"].is_object()) return;
        data["vendor"]["trussc"].erase("localPath");
    } else {
        auto root = fs::canonical(path);
        if (!fs::exists(root / "core/cmake/trussc_app.cmake")) {
            throw std::runtime_error("local TrussC path has no core/cmake/trussc_app.cmake");
        }
        if (!data["vendor"].is_object()) data["vendor"] = Json::object();
        if (!data["vendor"]["trussc"].is_object()) data["vendor"]["trussc"] = Json::object();
        data["vendor"]["trussc"]["localPath"] = root.generic_string();
    }
    writeGeneratedFile((project / "CMakePresets.json").string(), data.dump(2));
}

struct GitResult {
    int first;
    std::string second;
    std::string diagnostic;
};

static GitResult git(std::vector<std::string> args) {
    args.insert(args.begin(), "git");
    auto closeFile = [](FILE* file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(closeFile)> errors(std::tmpfile(), closeFile);
    if (!errors) throw std::runtime_error("cannot capture Git diagnostics");
    GitResult result{};
    result.first = runCaptured(args, [&](const char* bytes, size_t size) {
        result.second.append(bytes, size);
    }, errors.get());
    std::rewind(errors.get());
    char buffer[4096];
    while (auto count = std::fread(buffer, 1, sizeof(buffer), errors.get())) {
        result.diagnostic.append(buffer, count);
    }
    return result;
}

static std::string checkedGit(std::vector<std::string> args, bool objectId = false) {
    auto result = git(std::move(args));
    if (result.first != 0) throw std::runtime_error("TrussC version Git command failed: " + result.diagnostic + result.second);
    while (!result.second.empty() && (result.second.back() == '\n' || result.second.back() == '\r')) {
        result.second.pop_back();
    }
    if (objectId && (result.second.size() != 40 ||
        !std::all_of(result.second.begin(), result.second.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }))) {
        throw std::runtime_error("TrussC version Git command returned an invalid object ID: " + result.diagnostic + result.second);
    }
    return result.second;
}

std::string commitAt(const fs::path& root) {
    if (fs::exists(root / ".git")) {
        return checkedGit({"-C", root.string(), "rev-parse", "--verify", "HEAD^{commit}"}, true);
    }
    std::ifstream file(root / ".git_archival.txt", std::ios::binary);
    // Bound (archive metadata needs only a short describe line): read at most 4 KiB plus one overflow byte before parsing.
    char metadata[4097];
    file.read(metadata, sizeof(metadata));
    if (file.gcount() > 4096) return "unknown";
    std::istringstream lines(std::string(metadata, static_cast<size_t>(file.gcount())));
    std::string line;
    while (std::getline(lines, line)) {
        if (line.compare(0, 9, "describe:") == 0) {
            auto start = line.find_first_not_of(" \t", 9);
            if (start == std::string::npos || line.find("$Format:") != std::string::npos) break;
            auto end = line.find_last_not_of(" \t\r");
            return line.substr(start, end - start + 1);
        }
    }
    return "unknown";
}

static fs::path cacheRoot() {
    if (const char* overridePath = std::getenv("TRUSSC_CACHE_DIR")) return fs::absolute(overridePath);
#ifdef _WIN32
    const char* homePath = std::getenv("USERPROFILE");
#else
    const char* homePath = std::getenv("HOME");
#endif
    if (!homePath || !*homePath) throw std::runtime_error("cannot find home directory; set TRUSSC_CACHE_DIR");
    return fs::path(homePath) / ".trussc";
}

static void checkCachePath(const fs::path& path) {
    auto cache = cacheRoot().lexically_normal();
    auto relative = path.lexically_normal().lexically_relative(cache);
    auto reject = [&]() {
        throw std::runtime_error("TrussC cache path must stay inside the cache and contain only real directories: " + path.string());
    };
    if (relative.empty() || relative.is_absolute()) reject();
    auto current = cache;
    for (const auto& part : relative) {
        if (part == "..") reject();
        current /= part;
        auto status = fs::symlink_status(current);
        if (fs::is_symlink(status) || (fs::exists(status) && !fs::is_directory(status))) reject();
    }
    auto canonicalCache = fs::weakly_canonical(cache);
    auto canonicalPath = fs::weakly_canonical(path);
    auto cacheIt = canonicalCache.begin();
    auto pathIt = canonicalPath.begin();
    for (; cacheIt != canonicalCache.end(); ++cacheIt, ++pathIt) {
        if (pathIt == canonicalPath.end() || *cacheIt != *pathIt) reject();
    }
}

static void checkExistingWorktree(const Spec& spec, const fs::path& bare, const fs::path& destination, bool addon) {
    auto listing = checkedGit({"--git-dir=" + bare.string(), "worktree", "list", "--porcelain", "-z"});
    std::istringstream fields(listing);
    std::string field;
    std::string otherRef = "unregistered directory";
    auto versionRoot = cacheRoot() / (addon ? "addons" : "versions") / spec.owner / spec.repo;
    while (std::getline(fields, field, '\0')) {
        if (field.compare(0, 9, "worktree ") != 0) continue;
        auto worktree = fs::path(field.substr(9));
        if (fs::weakly_canonical(worktree) == fs::weakly_canonical(destination)) return;
        auto relative = worktree.lexically_relative(destination);
        auto reverse = destination.lexically_relative(worktree);
        if ((!relative.empty() && *relative.begin() != "..") ||
            (!reverse.empty() && *reverse.begin() != "..")) {
            otherRef = worktree.lexically_relative(versionRoot).generic_string();
        }
    }
    throw std::runtime_error("cannot acquire TrussC ref '" + spec.ref + "': destination is not its exact registered worktree root; conflicts with '" + otherRef + "': " + destination.string());
}

// Detect Git once per invocation. Tests can exercise the same no-Git path.
static bool useTarball() {
    const char* transport = std::getenv("TRUSSC_FETCH");
    if (transport && std::string(transport) == "tarball") return true;
    static const bool available = [] {
        const char* searchPath = std::getenv("PATH");
        if (!searchPath) return false;
#ifdef _WIN32
        constexpr char separator = ';';
        const char* executable = "git.exe";
#else
        constexpr char separator = ':';
        const char* executable = "git";
#endif
        std::istringstream directories(searchPath);
        std::string directory;
        while (std::getline(directories, directory, separator)) {
            std::error_code error;
            if (fs::is_regular_file(fs::path(directory) / executable, error)) return git({"--version"}).first == 0;
        }
        return false;
    }();
    return !available;
}

static bool addonDirectory(const fs::path& root) {
    return fs::is_regular_file(root / "addon.json") || fs::is_regular_file(root / "CMakeLists.txt") ||
           fs::is_directory(root / "src");
}

static auto archiveCommand = [](const std::vector<std::string>& args) {
    std::string output;
    bool exceeded = false;
    // Bound (untrusted archive listings): avoid retaining unlimited tool output.
    constexpr size_t outputLimit = 16 * 1024 * 1024;
    auto status = runCaptured(args, [&](const char* bytes, size_t size) {
        if (size > outputLimit - output.size()) exceeded = true;
        else if (!exceeded) output.append(bytes, size);
    }, nullptr, &exceeded);
    if (exceeded) throw std::runtime_error("TrussC archive listing is too large");
    return std::make_pair(status, output);
};

static auto encode = [](const std::string& text) {
    std::string encoded;
    const char* digits = "0123456789ABCDEF";
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/') encoded += c;
        else { encoded += '%'; encoded += digits[c >> 4]; encoded += digits[c & 15]; }
    }
    return encoded;
};

static void extractArchive(const fs::path& archive, const fs::path& tree) {
    auto names = archiveCommand({"tar", "-P", "-tzf", archive.string()});
    auto details = archiveCommand({"tar", "-P", "--numeric-owner", "-tvzf", archive.string()});
    if (names.first != 0 || details.first != 0) {
        throw std::runtime_error("cannot read TrussC archive: " + names.second + details.second);
    }
    auto safePath = [](std::string name) {
        // Reject Windows separators/drives too, even on Unix. Tar's escaped
        // names are rejected rather than interpreted differently by platforms.
        if (name.empty() || name.front() == '/' || name.find_first_of("\\:\r\n\t") != std::string::npos) return false;
        std::istringstream parts(name);
        std::string part;
        while (std::getline(parts, part, '/')) {
            if (part.empty() || part.back() == '.' || part.back() == ' ') return false;
            std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            });
            if (part == ".git") return false;
        }
        return true;
    };
    std::istringstream nameLines(names.second), detailLines(details.second);
    std::string name, detail, top;
    // Bound (2 GiB leaves ample room for TrussC sources): reject larger expanded totals before extraction.
    constexpr uint64_t expandedLimit = uint64_t{2} * 1024 * 1024 * 1024;
    uint64_t expandedSize = 0;
    while (std::getline(nameLines, name)) {
        if (!std::getline(detailLines, detail) || detail.empty() || !safePath(name)) {
            throw std::runtime_error("unsafe TrussC archive path");
        }
        std::istringstream fields(detail);
        std::string permissions, owner, sizeText;
        fields >> permissions >> owner;
        if (owner.find('/') == std::string::npos) {
            // BSD tar lists a link count, user and group; GNU tar uses user/group.
            std::string user, group;
            fields >> user >> group;
        }
        fields >> sizeText;
        uint64_t memberSize = 0;
        auto parsed = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), memberSize);
        if (sizeText.empty() || parsed.ec != std::errc{} || parsed.ptr != sizeText.data() + sizeText.size()) {
            throw std::runtime_error("invalid TrussC archive member size");
        }
        if (memberSize > expandedLimit - expandedSize) throw std::runtime_error("TrussC archive expanded size exceeds 2 GiB");
        expandedSize += memberSize;
        auto slash = name.find('/');
        auto folder = name.substr(0, slash);
        if (top.empty()) top = folder;
        if (folder != top || slash == std::string::npos) throw std::runtime_error("TrussC archive must have one top-level folder");
        if (detail.front() == 'l') {
            auto marker = detail.find(" " + name + " -> ");
            if (marker == std::string::npos ||
                detail.find(" " + name + " -> ", marker + 1) != std::string::npos ||
                !safePath(detail.substr(marker + name.size() + 5))) {
                throw std::runtime_error("unsafe TrussC archive link");
            }
            // With no '..' or absolute targets, even chains of links stay in the tree.
        } else if (detail.front() != '-' && detail.front() != 'd') {
            // Git archives contain directories, regular files and symbolic links;
            // device nodes and hard links are not source-tree entries.
            throw std::runtime_error("unsupported TrussC archive entry");
        }
    }
    if (top.empty() || std::getline(detailLines, detail)) throw std::runtime_error("invalid TrussC archive listing");
    auto extracted = archiveCommand({"tar", "-xzf", archive.string(), "--strip-components=1", "-C", tree.string()});
    if (extracted.first != 0) throw std::runtime_error("TrussC archive extraction failed: " + extracted.second);
}

// All curl/tar execution stays in this transport seam. No shell is involved.
static void fetchTarball(const Spec& spec, const fs::path& destination, const std::string& base, bool addon) {
    checkCachePath(destination);
    auto versionRoot = cacheRoot() / (addon ? "addons" : "versions") / spec.owner / spec.repo;
    // A slash ref must not replace a different ref's tree (or its parent).
    for (auto parent = destination.parent_path(); parent != versionRoot.parent_path(); parent = parent.parent_path()) {
        if (fs::exists(parent / ".git_archival.txt") || fs::exists(parent / ".git")) {
            throw std::runtime_error("TrussC ref conflicts with an existing cached version: " + parent.string());
        }
    }
    if (fs::exists(destination) && (!fs::is_regular_file(destination / ".git_archival.txt") ||
                                  fs::exists(destination / ".git"))) {
        throw std::runtime_error("TrussC ref conflicts with an existing cache directory: " + destination.string());
    }
    fs::create_directories(destination.parent_path());
    // create_directory reserves a private sibling; never reuse someone else's scratch tree.
    fs::path scratch;
    for (unsigned attempt = 0; ; ++attempt) {
        scratch = destination.parent_path() / (".trussc-download-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
        if (fs::create_directory(scratch)) break;
    }
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{scratch};
    auto archive = scratch / "archive.tar.gz";
    auto tree = scratch / "tree";
    fs::create_directory(tree);
    auto download = [&](const std::string& ref) {
        // Bound (remote acquisition): cap transfer size and duration for update/use.
        return archiveCommand({"curl", "--location", "--fail", "--silent", "--show-error",
                    "--max-filesize", "1073741824", "--max-time", "300", "--output", archive.string(),
                    "--url", base + "/archive/" + encode(ref) + ".tar.gz"});
    };
    // Explicit tag URL wins even when a branch has the same name. The short URL
    // is also used by file:// fixtures and resolves branches/commits on GitHub.
    auto downloaded = spec.commit ? download(spec.ref) : download("refs/tags/" + spec.ref);
    if (!spec.commit && downloaded.first != 0) downloaded = download(spec.ref);
    if (downloaded.first != 0) throw std::runtime_error("TrussC archive download failed: " + downloaded.second);
    extractArchive(archive, tree);
    if (!addon) checkLayout(spec, tree);
    if (addon) {
        if (!addonDirectory(tree)) {
            throw std::runtime_error("addon archive is missing addon.json, CMakeLists.txt or src/");
        }
        // GitHub addon archives need not carry TrussC's export-subst metadata.
        std::ofstream marker(tree / ".git_archival.txt");
        marker << "describe: " << spec.ref << "\n";
    } else if (!fs::is_regular_file(tree / "core/cmake/trussc_app.cmake") ||
        !fs::is_regular_file(tree / ".git_archival.txt")) {
        throw std::runtime_error("TrussC archive is missing core/cmake/trussc_app.cmake or .git_archival.txt");
    }
    checkCachePath(destination);
    auto marker = destination / ".last-used";
    auto newMarkerStatus = fs::symlink_status(tree / ".last-used");
    if (fs::is_regular_file(fs::symlink_status(marker)) &&
        (!fs::exists(newMarkerStatus) || fs::is_regular_file(newMarkerStatus))) {
        std::error_code error;
        fs::copy_file(marker, tree / ".last-used", fs::copy_options::overwrite_existing, error);
        if (!error) fs::last_write_time(tree / ".last-used", fs::last_write_time(marker), error);
    }
    auto previous = scratch / "previous";
    if (fs::exists(destination)) fs::rename(destination, previous);
    try { fs::rename(tree, destination); }
    catch (...) {
        if (fs::exists(previous)) fs::rename(previous, destination);
        throw;
    }
}

// All transport and worktree mutations enter here. The URL override supports file://.
static void fetchVersion(const Spec& spec, const fs::path& destination, bool addon, const std::string& sourceUrl) {
    auto bare = cacheRoot() / "repos" / spec.owner / (spec.repo + ".git");
    const char* baseOverride = std::getenv("TRUSSC_GITHUB_BASE_URL");
    std::string base = baseOverride ? baseOverride : "https://github.com";
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (useTarball()) {
        auto archiveSource = base + "/" + spec.owner + "/" + spec.repo;
        if (!baseOverride && !sourceUrl.empty()) {
            archiveSource = sourceUrl;
            while (!archiveSource.empty() && archiveSource.back() == '/') archiveSource.pop_back();
            if (archiveSource.size() >= 4 && archiveSource.substr(archiveSource.size() - 4) == ".git") {
                archiveSource.resize(archiveSource.size() - 4);
            }
        }
        fetchTarball(spec, destination, archiveSource, addon);
        return;
    }
    auto url = !baseOverride && !sourceUrl.empty() ? sourceUrl : base + "/" + spec.owner + "/" + spec.repo + ".git";
    checkCachePath(destination);
    checkCachePath(bare);
    fs::create_directories(bare.parent_path());
    if (!fs::exists(bare)) checkedGit({"clone", "--bare", "--", url, bare.string()});
    // Explicit namespaces make tags win over identically named branches.
    checkedGit({"--git-dir=" + bare.string(), "fetch", "--force", "--prune", url,
                "+refs/heads/*:refs/heads/*", "+refs/tags/*:refs/tags/*"});
    std::string revision = spec.ref;
    if (addon && spec.ref == "HEAD") {
        checkedGit({"--git-dir=" + bare.string(), "fetch", url, "HEAD"});
        revision = "FETCH_HEAD";
    } else if (!spec.commit) {
        auto tag = git({"--git-dir=" + bare.string(), "rev-parse", "--verify",
                        "refs/tags/" + spec.ref + "^{commit}"});
        revision = tag.first == 0 ? "refs/tags/" + spec.ref : "refs/heads/" + spec.ref;
    }
    // Servers may allow a reachable commit that is not a branch/tag tip.
    if (spec.commit && git({"--git-dir=" + bare.string(), "cat-file", "-e", revision + "^{commit}"}).first != 0) {
        checkedGit({"--git-dir=" + bare.string(), "fetch", url, revision});
    }
    auto commit = checkedGit({"--git-dir=" + bare.string(), "rev-parse", "--verify", revision + "^{commit}"}, true);
    // Inspect the acquired Git tree before changing or publishing a worktree.
    if (!addon && git({"--git-dir=" + bare.string(), "cat-file", "-t", commit + ":core/cmake/trussc_app.cmake"}).second.rfind("blob", 0) != 0 &&
        git({"--git-dir=" + bare.string(), "cat-file", "-t", commit + ":trussc"}).second.rfind("tree", 0) == 0) {
        rejectOldLayout(spec);
    }
    if (fs::exists(destination)) {
        checkExistingWorktree(spec, bare, destination, addon);
        checkedGit({"-C", destination.string(), "checkout", "--detach", commit});
    } else {
        fs::create_directories(destination.parent_path());
        checkedGit({"--git-dir=" + bare.string(), "worktree", "prune"});
        checkedGit({"--git-dir=" + bare.string(), "worktree", "add", "--detach", destination.string(), commit});
    }
}

std::string readBounded(const fs::path& path, size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file || !fs::is_regular_file(path)) throw std::runtime_error("cannot read " + path.string());
    std::string text(limit + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (static_cast<size_t>(file.gcount()) > limit) throw std::runtime_error(path.filename().string() + " exceeds the " + std::to_string(limit) + "-byte limit");
    text.resize(static_cast<size_t>(file.gcount()));
    return text;
}

static std::string today() {
    auto now = std::time(nullptr);
    auto local = *std::localtime(&now);
    std::ostringstream text;
    text << std::put_time(&local, "%Y-%m-%d");
    return text.str();
}

static std::string lastUsed(const fs::path& root) {
    auto marker = root / ".last-used";
    if (!fs::is_regular_file(fs::symlink_status(marker))) return "-";
    std::ifstream file(marker);
    // Bound (usage date): a date line is 11 bytes; read 12 to detect excess data.
    char text[12]{};
    file.read(text, sizeof(text));
    std::string date(text, static_cast<size_t>(file.gcount()));
    if (!date.empty() && date.back() == '\n') date.pop_back();
    return date.size() == 10 ? date : "-";
}

static void markUsed(const fs::path& root) {
    try {
        auto date = today();
        if (lastUsed(root) == date) return;
        auto marker = root / ".last-used";
        auto status = fs::symlink_status(marker);
        if (fs::exists(status) && !fs::is_regular_file(status)) {
            throw std::runtime_error(".last-used is not a regular file");
        }
        std::ofstream file(marker);
        file << date << '\n';
        file.close();
        if (!file) throw std::runtime_error("cannot write .last-used");
    } catch (const std::exception& error) {
        std::cerr << "Warning: cannot record cache usage for " << root.string() << ": " << error.what() << '\n';
    }
}

void markCachedUsed(const fs::path& path) {
    if (path.empty()) return;
    auto relative = fs::absolute(path).lexically_normal().lexically_relative(cacheRoot().lexically_normal());
    if (relative.empty() || (*relative.begin() != "versions" && *relative.begin() != "addons")) return;
    try {
        checkCachePath(path);
        if (!fs::is_regular_file(fs::symlink_status(path / ".git")) &&
            !fs::is_regular_file(fs::symlink_status(path / ".git_archival.txt"))) return;
        markUsed(path);
    } catch (const std::exception& error) {
        std::cerr << "Warning: cannot record cache usage for " << path.string() << ": " << error.what() << '\n';
    }
}

// Recursive iterators do not follow directory symlinks. symlink_status also
// prevents file_size (and the Windows permission change) from following links.
static uintmax_t cacheSize(const fs::path& path) {
    uintmax_t size = 0;
    if (!fs::exists(path)) return size;
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (fs::is_regular_file(entry.symlink_status())) size += entry.file_size();
    }
    return size;
}

static std::string formatCacheSize(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    const char* units[] = {"KB", "MB", "GB"};
    double size = static_cast<double>(bytes) / 1024;
    int unit = 0;
    while (size >= 1024 && unit < 2) {
        size /= 1024;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << size << ' ' << units[unit];
    return output.str();
}

static void removeCacheTree(const fs::path& path) {
    checkCachePath(path);
#ifdef _WIN32
    // Git object files are read-only on Windows. Do not change symlink targets.
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (!fs::is_symlink(entry.symlink_status())) {
            fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::add);
        }
    }
    fs::permissions(path, fs::perms::owner_write, fs::perm_options::add);
#endif
    fs::remove_all(path);
}

void removeCachedVersion(const fs::path& path, const Spec& spec) {
    checkCachePath(path);
    bool worktree = fs::is_regular_file(fs::symlink_status(path / ".git"));
    auto bare = cacheRoot() / "repos" / spec.owner / (spec.repo + ".git");
    if (worktree) checkCachePath(bare);
    removeCacheTree(path);
    if (worktree && fs::exists(bare)) {
        checkedGit({"--git-dir=" + bare.string(), "worktree", "prune", "--expire", "now"});
    }
}

struct CacheEntry {
    fs::path path;
    Spec spec;
    bool addon;
    bool worktree;
    uintmax_t size;
    std::string date;
};

static std::vector<CacheEntry> cacheEntries() {
    std::vector<CacheEntry> entries;
    for (bool addon : {false, true}) {
        auto base = cacheRoot() / (addon ? "addons" : "versions");
        checkCachePath(base);
        if (!fs::exists(base)) continue;
        for (auto it = fs::recursive_directory_iterator(base); it != fs::recursive_directory_iterator(); ++it) {
            if (!fs::is_directory(it->symlink_status()) || it.depth() < 2) continue;
            auto path = it->path();
            checkCachePath(path);
            bool worktree = fs::is_regular_file(fs::symlink_status(path / ".git"));
            if (!worktree && !fs::is_regular_file(fs::symlink_status(path / ".git_archival.txt"))) continue;
            auto relative = path.lexically_relative(base);
            auto part = relative.begin();
            auto owner = (part++)->string();
            auto repo = (part++)->string();
            fs::path ref;
            for (; part != relative.end(); ++part) ref /= *part;
            auto spec = parse(owner + "/" + repo + "@" + ref.generic_string());
            entries.push_back({path, spec, addon, worktree, cacheSize(path), lastUsed(path)});
            it.disable_recursion_pending();
        }
    }
    std::sort(entries.begin(), entries.end(), [](const CacheEntry& a, const CacheEntry& b) { return a.path < b.path; });
    return entries;
}

static bool oldDate(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    std::tm local{};
    std::istringstream input(date);
    input >> std::get_time(&local, "%Y-%m-%d");
    if (input.fail()) return false;
    local.tm_hour = 12;
    local.tm_isdst = -1;
    auto stamp = std::mktime(&local);
    std::ostringstream normalized;
    normalized << std::put_time(&local, "%Y-%m-%d");
    if (stamp == std::time_t(-1) || normalized.str() != date) return false;
    auto now = std::time(nullptr);
    auto current = *std::localtime(&now);
    current.tm_hour = 12;
    current.tm_min = current.tm_sec = 0;
    current.tm_isdst = -1;
    // Round calendar days across daylight-saving transitions.
    return std::llround(std::difftime(std::mktime(&current), stamp) / 86400.0) >= 90;
}

static bool confirmCacheRemoval() {
#ifdef _WIN32
    bool terminal = _isatty(_fileno(stdin));
#else
    bool terminal = isatty(fileno(stdin));
#endif
    if (!terminal) {
        std::cout << "Not removed. Run this command interactively in a terminal to confirm.\n";
        return false;
    }
    std::cout << "Remove these cached files? [y/N] " << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    return answer == "y" || answer == "Y";
}

int cacheCommand(const std::vector<std::string>& args) {
    bool known = !args.empty() && (args[0] == "list" || args[0] == "clear" || args[0] == "autoremove" || args[0] == "remove");
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help" ||
        (known && args.size() == 2 && (args[1] == "--help" || args[1] == "-h"))) {
        std::cout << "Usage: trusscli cache "
            << (args.size() == 2 ? args[0] : "[list|clear|autoremove|remove]") << "\n\n"
            << "Manage shared TrussC and external addon downloads. Local folders are not cached.\n\n"
            "Commands:\n"
            "  list        List specs, worktree/tarball, size and last-used date (- if unknown)\n"
            "  remove <spec>          Remove one TrussC version without confirmation\n"
            "  remove --addon <spec>  Remove one external addon version without confirmation\n"
            "  clear       Show total size and remove the whole cache after one y/N prompt\n"
            "  autoremove  List and remove versions last used 90 or more days ago (one y/N prompt)\n\n"
            "Addon rows are marked 'addon'; other rows are marked 'TrussC'.\n"
            "remove uses specs as for use or addon add; qualify the owner if an addon is ambiguous.\n"
            "Versions without .last-used are never auto-removed.\n"
            "clear and autoremove require terminal input; the default answer is No.\n"
            "  -h, --help  Show this help\n";
        return 0;
    }
    if (!args.empty() && args[0] == "remove") {
        bool addon = args.size() == 3 && args[1] == "--addon";
        if (!addon && args.size() != 2) {
            throw std::runtime_error("expected cache remove [--addon] <spec>; run `trusscli cache --help`");
        }
        const auto& text = args.back();
        Spec spec;
        if (addon) {
            auto parsed = addonVersions::parse(text);
            spec.owner = parsed.owner;
            spec.repo = parsed.name;
            spec.ref = parsed.ref.empty() ? "HEAD" : parsed.ref;
        } else {
            spec = parse(text);
        }
        auto entries = cacheEntries();
        std::vector<CacheEntry> matches;
        for (const auto& entry : entries) {
            if (entry.addon == addon && entry.spec.repo == spec.repo && entry.spec.ref == spec.ref &&
                (spec.owner.empty() || entry.spec.owner == spec.owner)) matches.push_back(entry);
        }
        if (matches.empty()) throw std::runtime_error("spec is not in the cache: " + text + "; run `trusscli cache list`");
        if (matches.size() > 1) throw std::runtime_error("ambiguous cached addon: " + text + "; use owner/repo@ref from `trusscli cache list`");
        removeCachedVersion(matches[0].path, matches[0].spec);
        std::cout << "Removed " << (addon ? "addon " : "TrussC ") << text << '\n';
        return 0;
    }
    if (args.size() != 1 || !known) {
        throw std::runtime_error("unknown cache command; run `trusscli cache --help`");
    }
    auto root = cacheRoot();
    checkCachePath(root);
    if (args[0] == "clear") {
        std::cout << "Total: " << formatCacheSize(cacheSize(root)) << '\n';
        if (confirmCacheRemoval() && fs::exists(root)) {
            for (const auto& entry : fs::directory_iterator(root)) {
                if (fs::is_directory(entry.symlink_status())) removeCacheTree(entry.path());
                else {
#ifdef _WIN32
                    if (!fs::is_symlink(entry.symlink_status())) {
                        fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::add);
                    }
#endif
                    fs::remove(entry.path());
                }
            }
        }
        return 0;
    }
    auto entries = cacheEntries();
    uintmax_t total = 0;
    std::vector<CacheEntry> selected;
    for (const auto& entry : entries) {
        if (args[0] == "autoremove" && !oldDate(entry.date)) continue;
        std::cout << (entry.addon ? "addon " : "TrussC ")
            << entry.spec.owner << '/' << entry.spec.repo << '@' << entry.spec.ref
            << ' ' << (entry.worktree ? "worktree" : "tarball") << ' ' << formatCacheSize(entry.size)
            << ' ' << entry.date << '\n';
        total += entry.size;
        selected.push_back(entry);
    }
    std::cout << "Total: " << formatCacheSize(total) << (args[0] == "list" ? " (versions; bare repos excluded)\n" : "\n");
    if (args[0] == "autoremove" && !selected.empty() && confirmCacheRemoval()) {
        for (const auto& entry : selected) removeCachedVersion(entry.path, entry.spec);
    }
    return 0;
}

std::string acquire(const Spec& spec, bool fetch, bool addon, const std::string& sourceUrl) {
    // Validate at the shared boundary, including specs assembled from registry metadata.
    parse(spec.owner + "/" + spec.repo + "@" + spec.ref);
    if (!sourceUrl.empty() && sourceUrl.front() == '-') throw std::runtime_error("invalid addon repository URL");
    auto root = cacheRoot() / (addon ? "addons" : "versions") / spec.owner / spec.repo / spec.ref;
    checkCachePath(root);
    if (fetch) {
        fetchVersion(spec, root, addon, sourceUrl);
    }
    else if (fs::exists(root)) {
        if (fs::exists(root / ".git")) {
            checkExistingWorktree(spec, cacheRoot() / "repos" / spec.owner / (spec.repo + ".git"), root, addon);
        } else {
            if (!fs::is_regular_file(root / ".git_archival.txt")) {
                throw std::runtime_error("version is unavailable; run `trusscli update`: " + spec.ref);
            }
            auto versionRoot = cacheRoot() / (addon ? "addons" : "versions") / spec.owner / spec.repo;
            for (auto parent = root.parent_path(); parent != versionRoot.parent_path(); parent = parent.parent_path()) {
                if (fs::exists(parent / ".git") || fs::exists(parent / ".git_archival.txt")) {
                    throw std::runtime_error("version is not an exact acquired archive root; run `trusscli update`: " + spec.ref);
                }
            }
        }
    }
    if (!addon) checkLayout(spec, root);
    if (addon ? !addonDirectory(root) : !fs::is_regular_file(root / "core/cmake/trussc_app.cmake")) {
        throw std::runtime_error(std::string(addon ? "addon" : "TrussC") + " version is unavailable; run `trusscli update`: " + spec.owner + "/" + spec.repo + "@" + spec.ref);
    }
    if (fetch) markUsed(root);
    return root.string();
}

std::string resolve(const fs::path& project, bool fetch) {
    auto local = localPath(project);
    if (!local.empty()) {
        auto root = fs::path(local);
        if (root.is_relative()) root = project / root;
        if (!fs::exists(root / "core/cmake/trussc_app.cmake")) {
            throw std::runtime_error("local TrussC path is missing: " + root.string());
        }
        return fs::absolute(root).string();
    }
    auto versionFile = project / ".trussc-version";
    if (!fs::exists(versionFile)) return {};
    if (!fs::is_regular_file(versionFile)) {
        throw std::runtime_error(".trussc-version must be a regular file");
    }
    // Bound (project version): cap the version selection at 4 KiB.
    auto text = readBounded(versionFile, 4096);
    auto begin = text.find_first_not_of(" \t\r\n");
    auto end = text.find_last_not_of(" \t\r\n");
    auto spec = parse(begin == std::string::npos ? "" : text.substr(begin, end - begin + 1));
    return acquire(spec, fetch, false, "");
}
}
