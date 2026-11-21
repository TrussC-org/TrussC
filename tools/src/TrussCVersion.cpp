#include "TrussCVersion.h"
#include "ProcessCapture.h"
#include "GeneratedFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace trusscVersion {
namespace fs = std::filesystem;
using Json = nlohmann::json;

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

static void checkExistingWorktree(const Spec& spec, const fs::path& bare, const fs::path& destination) {
    auto listing = checkedGit({"--git-dir=" + bare.string(), "worktree", "list", "--porcelain", "-z"});
    std::istringstream fields(listing);
    std::string field;
    std::string otherRef = "unregistered directory";
    auto versionRoot = cacheRoot() / "versions" / spec.owner / spec.repo;
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

// All curl/tar execution stays in this transport seam. No shell is involved.
static void fetchTarball(const Spec& spec, const fs::path& destination, const std::string& base) {
    checkCachePath(destination);
    auto versionRoot = cacheRoot() / "versions" / spec.owner / spec.repo;
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
    auto run = [](const std::vector<std::string>& args) {
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
    auto archive = scratch / "archive.tar.gz";
    auto tree = scratch / "tree";
    fs::create_directory(tree);
    auto encode = [](const std::string& text) {
        std::string encoded;
        const char* digits = "0123456789ABCDEF";
        for (unsigned char c : text) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/') encoded += c;
            else { encoded += '%'; encoded += digits[c >> 4]; encoded += digits[c & 15]; }
        }
        return encoded;
    };
    auto download = [&](const std::string& ref) {
        // Bound (remote acquisition): cap transfer size and duration for update/use.
        return run({"curl", "--location", "--fail", "--silent", "--show-error",
                    "--max-filesize", "1073741824", "--max-time", "300", "--output", archive.string(),
                    "--url", base + "/" + spec.owner + "/" + spec.repo + "/archive/" + encode(ref) + ".tar.gz"});
    };
    // Explicit tag URL wins even when a branch has the same name. The short URL
    // is also used by file:// fixtures and resolves branches/commits on GitHub.
    auto downloaded = spec.commit ? download(spec.ref) : download("refs/tags/" + spec.ref);
    if (!spec.commit && downloaded.first != 0) downloaded = download(spec.ref);
    if (downloaded.first != 0) throw std::runtime_error("TrussC archive download failed: " + downloaded.second);
    auto names = run({"tar", "-P", "-tzf", archive.string()});
    auto details = run({"tar", "-P", "-tvzf", archive.string()});
    if (names.first != 0 || details.first != 0) {
        throw std::runtime_error("cannot read TrussC archive: " + names.second + details.second);
    }
    auto safePath = [](const std::string& name) {
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
    auto extracted = run({"tar", "-xzf", archive.string(), "--strip-components=1", "-C", tree.string()});
    if (extracted.first != 0) throw std::runtime_error("TrussC archive extraction failed: " + extracted.second);
    if (!fs::is_regular_file(tree / "core/cmake/trussc_app.cmake") ||
        !fs::is_regular_file(tree / ".git_archival.txt")) {
        throw std::runtime_error("TrussC archive is missing core/cmake/trussc_app.cmake or .git_archival.txt");
    }
    checkCachePath(destination);
    auto previous = scratch / "previous";
    if (fs::exists(destination)) fs::rename(destination, previous);
    try { fs::rename(tree, destination); }
    catch (...) {
        if (fs::exists(previous)) fs::rename(previous, destination);
        throw;
    }
}

// All transport and worktree mutations enter here. The URL override supports file://.
static void fetchVersion(const Spec& spec, const fs::path& destination) {
    auto bare = cacheRoot() / "repos" / spec.owner / (spec.repo + ".git");
    const char* baseOverride = std::getenv("TRUSSC_GITHUB_BASE_URL");
    std::string base = baseOverride ? baseOverride : "https://github.com";
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (useTarball()) { fetchTarball(spec, destination, base); return; }
    auto url = base + "/" + spec.owner + "/" + spec.repo + ".git";
    checkCachePath(destination);
    checkCachePath(bare);
    fs::create_directories(bare.parent_path());
    if (!fs::exists(bare)) checkedGit({"clone", "--bare", "--", url, bare.string()});
    // Explicit namespaces make tags win over identically named branches.
    checkedGit({"--git-dir=" + bare.string(), "fetch", "--force", "--prune", url,
                "+refs/heads/*:refs/heads/*", "+refs/tags/*:refs/tags/*"});
    std::string revision = spec.ref;
    if (!spec.commit) {
        auto tag = git({"--git-dir=" + bare.string(), "rev-parse", "--verify",
                        "refs/tags/" + spec.ref + "^{commit}"});
        revision = tag.first == 0 ? "refs/tags/" + spec.ref : "refs/heads/" + spec.ref;
    }
    // Servers may allow a reachable commit that is not a branch/tag tip.
    if (spec.commit && git({"--git-dir=" + bare.string(), "cat-file", "-e", revision + "^{commit}"}).first != 0) {
        checkedGit({"--git-dir=" + bare.string(), "fetch", url, revision});
    }
    auto commit = checkedGit({"--git-dir=" + bare.string(), "rev-parse", "--verify", revision + "^{commit}"}, true);
    if (fs::exists(destination)) {
        checkExistingWorktree(spec, bare, destination);
        checkedGit({"-C", destination.string(), "checkout", "--detach", commit});
    } else {
        fs::create_directories(destination.parent_path());
        checkedGit({"--git-dir=" + bare.string(), "worktree", "prune"});
        checkedGit({"--git-dir=" + bare.string(), "worktree", "add", "--detach", destination.string(), commit});
    }
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
    // Bound (project input): version specs must fit in a small manifest before parsing.
    constexpr size_t versionLimit = 4096;
    std::ifstream file(versionFile, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read .trussc-version");
    char buffer[versionLimit + 1];
    file.read(buffer, sizeof(buffer));
    if (file.gcount() > static_cast<std::streamsize>(versionLimit)) {
        throw std::runtime_error(".trussc-version exceeds the 4096-byte limit");
    }
    auto text = std::string(buffer, static_cast<size_t>(file.gcount()));
    auto begin = text.find_first_not_of(" \t\r\n");
    auto end = text.find_last_not_of(" \t\r\n");
    auto spec = parse(begin == std::string::npos ? "" : text.substr(begin, end - begin + 1));
    auto root = cacheRoot() / "versions" / spec.owner / spec.repo / spec.ref;
    checkCachePath(root);
    if (fetch) fetchVersion(spec, root);
    if (!fs::exists(root / "core/cmake/trussc_app.cmake")) {
        throw std::runtime_error("TrussC version is unavailable; run `trusscli update`: " + text);
    }
    return root.string();
}
}
