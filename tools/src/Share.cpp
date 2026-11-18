#include "Share.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace share {
using namespace std;
using nlohmann::json;
namespace {
string read(const fs::path& path) {
    ifstream file(path, ios::binary);
    return {istreambuf_iterator<char>(file), {}};
}
void write(const fs::path& path, const string& data) {
    ofstream file(path, ios::binary);
    file << data;
    if (!file) throw runtime_error("cannot write " + path.string());
}
string trim(string text) {
    const auto end = text.find_last_not_of(" \t\r\n");
    if (end == string::npos) return "";
    text.erase(end + 1);
    text.erase(0, text.find_first_not_of(" \t\r\n"));
    return text;
}
string ask(const string& prompt) {
    cout << prompt << ' ' << flush;
    string answer;
    getline(cin, answer);
    answer = trim(answer);
    transform(answer.begin(), answer.end(), answer.begin(), [](unsigned char c) { return tolower(c); });
    return answer;
}
bool yes(const string& prompt) {
    const string answer = ask(prompt);
    return answer == "y" || answer == "yes";
}
const char* internalEnv(const char* name) {
    const char* value = getenv(name);
    return value && *value ? value : nullptr;
}
struct Scratch {
    fs::path path;
    Scratch() {
        const auto stamp = chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            path = fs::temp_directory_path() / ("trusscli-share-" + to_string(stamp) + "-" + to_string(i));
            if (fs::create_directory(path)) return;
        }
        throw runtime_error("cannot create share staging directory");
    }
    ~Scratch() { error_code ec; fs::remove_all(path, ec); }
};
void copyFile(const fs::path& source, const fs::path& target) {
    // A symlink could publish files outside the web output or project.
    if (fs::is_symlink(source)) throw runtime_error("cannot publish symlink: " + source.string());
    fs::create_directories(target.parent_path());
    fs::copy_file(source, target, fs::copy_options::overwrite_existing);
}
bool sharedMemory(string source) {
    // Generated JS can have very long whitespace runs; scan without regex recursion.
    source.erase(remove_if(source.begin(), source.end(), [](unsigned char c) { return isspace(c); }), source.end());
    if (source.find("newSharedArrayBuffer(") != string::npos) return true;
    size_t start = 0;
    while ((start = source.find("newWebAssembly.Memory({", start)) != string::npos) {
        const auto end = source.find('}', start);
        const string options = source.substr(start, end - start);
        for (const string key : {"shared:", "\"shared\":", "'shared':"}) {
            for (const string value : {"true", "!0", "1"}) {
                if (options.find(key + value) != string::npos) return true;
            }
        }
        start = end == string::npos ? source.size() : end + 1;
    }
    return false;
}
void previewBundle(const fs::path& data, const string& name) {
    if (!fs::is_directory(data)) return;
    cout << "Bundled into " << name << ".data (from bin/data):\n";
    if (fs::is_symlink(data)) cout << "  data -> " << fs::read_symlink(data).generic_string() << "\n";
    vector<fs::path> files;
    // Like Emscripten's os.walk, do not follow directory symlinks.
    for (const auto& entry : fs::recursive_directory_iterator(data)) {
        if (entry.is_regular_file() || entry.is_symlink()) files.push_back(entry.path());
    }
    sort(files.begin(), files.end());
    for (const auto& file : files) {
        cout << "  " << file.lexically_relative(data).generic_string();
        if (fs::is_regular_file(file)) cout << "  " << fs::file_size(file) << " bytes";
        if (fs::is_symlink(file)) {
            cout << " -> " << fs::read_symlink(file).generic_string();
            if (fs::is_directory(file)) cout << " (directory; not preloaded)";
        }
        cout << '\n';
    }
}
}

int run(const fs::path& project, const string& name, const string& version,
        bool unshare, const Services& services) try {
    auto command = [&](vector<string> args) { return services.command(args); };
    auto required = [&](vector<string> args, const string& failure) {
        auto result = command(std::move(args));
        // Never echo subprocess output: git/gh errors can contain credential URLs.
        if (result.code != 0) throw runtime_error(failure);
        return trim(result.output);
    };
    auto gitProject = [&](vector<string> args) {
        args.insert(args.begin(), {"git", "-C", project.string()});
        return command(std::move(args));
    };
    if (gitProject({"rev-parse", "--is-inside-work-tree"}).code != 0)
        throw runtime_error("share requires a git repo");
    auto remote = gitProject({"remote", "get-url", "origin"});
    if (remote.code != 0) throw runtime_error("share requires a GitHub origin remote");
    const string origin = trim(remote.output);
    smatch match;
    const regex github(R"(^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([A-Za-z0-9-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$)");
    if (!regex_match(origin, match, github))
        throw runtime_error("origin must be on github.com");
    const string owner = match[1], repo = match[2], slug = owner + "/" + repo;
    // Local fixtures keep the real GitHub URL for validation; only transport changes.
    const string transport = internalEnv("TRUSSC_INTERNAL_SHARE_REMOTE") ?
        internalEnv("TRUSSC_INTERNAL_SHARE_REMOTE") : origin;
    required({"gh", "auth", "status"}, "gh is missing or not logged in; run gh auth login");
    auto visibility = json::parse(required({"gh", "repo", "view", slug, "--json", "visibility"},
                                          "cannot read GitHub repo visibility"));
    if (visibility.value("visibility", "") != "PUBLIC") throw runtime_error("public repo required");
    const string endpoint = "repos/" + slug + "/pages";
    const string url = "https://" + owner + ".github.io/" + repo + "/";
    Scratch scratch;
    const fs::path stage = scratch.path / "site";
    fs::create_directory(stage);
    auto git = [&](vector<string> args) {
        args.insert(args.begin(), {"git", "-C", stage.string()});
        return required(std::move(args), "git share operation failed (check credentials and git identity)");
    };
    git({"init", "-q"});
    // Preserve repo-local identity as well as normal environment/global credentials.
    for (const string key : {"user.name", "user.email"}) {
        auto value = gitProject({"config", "--get", key});
        if (value.code == 0) git({"config", key, trim(value.output)});
    }
    string inspected;
    auto loadOwnedBranch = [&]() {
        const auto refs = command({"git", "ls-remote", "--heads", transport, "refs/heads/gh-pages"});
        if (refs.code != 0) throw runtime_error("cannot inspect remote gh-pages");
        inspected.clear();
        if (trim(refs.output).empty()) return false;
        git({"fetch", "-q", "--depth=1", transport, "refs/heads/gh-pages"});
        inspected = git({"rev-parse", "FETCH_HEAD"});
        auto marker = command({"git", "-C", stage.string(), "cat-file", "-t", "FETCH_HEAD:.trussc-share"});
        if (marker.code != 0 || trim(marker.output) != "blob")
            throw runtime_error("gh-pages looks hand-made: .trussc-share is missing; nothing pushed");
        return true;
    };
    vector<string> manifest;
    auto stageManifest = [&]() {
        vector<string> args{"add", "--force", "--"};
        args.insert(args.end(), manifest.begin(), manifest.end());
        git(std::move(args));
    };
    auto publish = [&]() {
        // commit-tree without -p creates an orphan every time, including the listing pass.
        const string tree = git({"write-tree"});
        const string commit = git({"commit-tree", tree, "-m", "Publish with trusscli share"});
        git({"update-ref", "HEAD", commit});
        const auto pushed = command({"git", "-C", stage.string(), "push",
            "--force-with-lease=refs/heads/gh-pages:" + inspected, transport, "HEAD:refs/heads/gh-pages"});
        if (pushed.code != 0) throw runtime_error("gh-pages changed on GitHub during share; run share again (or check git credentials)");
        inspected = commit;
    };
    if (unshare) {
        if (!loadOwnedBranch()) throw runtime_error("no owned gh-pages branch to unshare");
        git({"checkout", "-q", "FETCH_HEAD", "--", "."});
        git({"rm", "--force", "--ignore-unmatch", "--", "trussc-share.json"});
        publish();
        required({"gh", "repo", "edit", slug, "--remove-topic", "trussc-share"}, "cannot remove listing topic");
        cout << "Listing removed. Pages and gh-pages remain.\n"
             << "To delete Pages manually: gh api -X DELETE " << endpoint << "\n"
             << "To delete the branch manually: git push origin --delete gh-pages\n";
        return 0;
    }
    fs::path web = project / "bin";
    if (const char* fixture = internalEnv("TRUSSC_INTERNAL_SHARE_WEB_DIR")) web = fs::absolute(fixture);
    else if (services.buildWeb() != 0) throw runtime_error("web build failed");
    if (!fs::is_regular_file(web / (name + ".html"))) throw runtime_error("web output HTML is missing");
    // These are the products emitted by trussc_app.cmake's Web target.
    for (const string ext : {".html", ".js", ".wasm", ".data"}) {
        const string file = name + ext;
        if (!fs::is_regular_file(web / file)) continue;
        // Optional feature probes/comments in non-threaded output are allowed.
        if ((ext == ".js" || ext == ".html") && sharedMemory(read(web / file)))
            throw runtime_error("web build needs COOP/COEP headers (pthreads/SharedArrayBuffer); GitHub Pages cannot set them");
        const string published = ext == ".html" ? "index.html" : file;
        copyFile(web / file, stage / published);
        manifest.push_back(published);
    }
    write(stage / ".nojekyll", "");
    write(stage / ".trussc-share", "generator: trusscli share\n");
    manifest.insert(manifest.end(), {".nojekyll", ".trussc-share"});
    string screenshot;
    fs::path candidate;
    if (fs::is_regular_file(project / "screenshot.png")) candidate = project / "screenshot.png";
    else if (fs::is_regular_file(project / "screenshot.jpg")) candidate = project / "screenshot.jpg";
    while (true) {
        bool captured = false;
        if (candidate.empty()) {
            candidate = scratch.path / "capture.png";
            string error;
            if (const char* fixture = internalEnv("TRUSSC_INTERNAL_SHARE_CAPTURE")) {
                if (fs::is_regular_file(fixture)) { copyFile(fixture, candidate); captured = true; }
                else error = "test capture failed";
            } else captured = services.capture(candidate, error);
            if (!captured) {
                cout << "Screenshot failed: " << error << "\n";
                ask("Continue without a screenshot [skip]:");
                break;
            }
        }
        cout << "Screenshot: " << candidate.string() << "\n";
        if (!internalEnv("TRUSSC_INTERNAL_SHARE_NO_VIEWER")) services.view(candidate);
        const string answer = ask("Use this screenshot? [y / retake / skip]");
        if (answer == "retake") { candidate.clear(); continue; }
        if (answer == "y" || answer == "yes") {
            if (captured) {
                copyFile(candidate, project / "screenshot.png");
                candidate = project / "screenshot.png";
            }
            screenshot = candidate.filename().string();
            copyFile(candidate, stage / screenshot);
            manifest.push_back(screenshot);
        }
        break;
    }
    stageManifest();
    // Read the index: this is exactly the tree publish() commits.
    istringstream entries(git({"ls-files", "-z"}));
    uintmax_t total = 0;
    string file;
    while (getline(entries, file, '\0')) {
        const auto size = stoull(git({"cat-file", "-s", ":" + file}));
        cout << file << "  " << size << " bytes\n";
        total += size;
    }
    cout << "Total: " << total << " bytes\n";
    if (fs::is_regular_file(stage / (name + ".data"))) previewBundle(project / "bin" / "data", name);
    if (!yes("Publish these files to GitHub Pages at " + url + " ? [y/N]")) {
        cout << "Publish canceled; nothing pushed.\n";
        return 0;
    }
    loadOwnedBranch();
    publish();
    auto pages = command({"gh", "api", endpoint});
    if (pages.code != 0) {
        // Only create for a missing endpoint; auth/server errors must not be treated as absence.
        if (pages.output.find("404") == string::npos) throw runtime_error("cannot check Pages configuration");
        required({"gh", "api", "-X", "POST", endpoint, "-f", "source[branch]=gh-pages", "-f", "source[path]=/"},
                 "published gh-pages but could not enable Pages");
    } else {
        const auto config = json::parse(pages.output);
        const auto source = config.value("source", json::object());
        const string served = config.value("build_type", "") == "workflow" ? "workflow" :
            (source.is_object() ? source.value("branch", "unknown") + ":" + source.value("path", "unknown") : "unknown");
        if (served != "gh-pages:/")
            throw runtime_error("Pages is configured to serve " + served +
                "; change it to gh-pages:/ in the repo settings, or run: gh api -X PUT " + endpoint +
                " -f build_type=legacy -f 'source[branch]=gh-pages' -f 'source[path]=/'");
    }
    cout << "Pages: " << url << "\nThe first deploy takes about a minute.\n";
    if (yes("List this app on trussc.org/share? [y/N]")) {
        const auto details = json::parse(required({"gh", "repo", "view", slug, "--json", "description"},
                                                 "cannot read repo description"));
        json marker{{"schema", 1}, {"generator", "trusscli share"}, {"title", name},
                    {"description", details.value("description", json()).is_string() ? details["description"] : json("")},
                    {"author", owner}, {"trussc_version", version}};
        if (!screenshot.empty()) marker["screenshot"] = screenshot;
        write(stage / "trussc-share.json", marker.dump(2) + "\n");
        git({"add", "--force", "--", "trussc-share.json"});
        publish();
        required({"gh", "repo", "edit", slug, "--add-topic", "trussc-share"}, "cannot add listing topic");
    } else {
        // A repeat share with 'no' also withdraws any previous listing.
        required({"gh", "repo", "edit", slug, "--remove-topic", "trussc-share"}, "cannot remove old listing topic");
    }
    return 0;
} catch (const exception& error) {
    cerr << "Error: " << error.what() << "\n";
    return 1;
}
} // namespace share
