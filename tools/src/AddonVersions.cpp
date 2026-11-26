#include "OperationLog.h"
#include "AddonVersions.h"
#include "TrussCVersion.h"
#include "GeneratedFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <charconv>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace addonVersions {
namespace fs = std::filesystem;
using Json = nlohmann::json;

Spec parse(const std::string& text) {
    Spec result;
    auto at = text.find('@');
    auto name = text.substr(0, at);
    auto slash = name.find('/');
    result.owner = slash == std::string::npos ? "" : name.substr(0, slash);
    result.name = slash == std::string::npos ? name : name.substr(slash + 1);
    result.ref = at == std::string::npos ? "" : text.substr(at + 1);
    // Reuse the version parser for both name components and ref containment.
    trusscVersion::parse((result.owner.empty() ? "placeholder" : result.owner) + "/" + result.name + "@" +
                        (at == std::string::npos ? "HEAD" : result.ref));
    if (slash != std::string::npos && result.owner.empty()) throw std::runtime_error("invalid addon owner");
    return result;
}

static Json manifest(const fs::path& path) {
    // Bound (untrusted manifests): cap each file at 16 MiB before JSON parsing.
    return fs::exists(path) ? Json::parse(trusscVersion::readBounded(path, 16 * 1024 * 1024)) : Json::object();
}

static Json presets(const fs::path& project) {
    try {
        auto data = manifest(project / "CMakePresets.json");
        return data.is_object() ? data : Json::object();
    } catch (const Json::parse_error&) {
        return Json::object(); // Preserve update's existing malformed-preset repair.
    }
}

static Json& vendor(Json& data) {
    if (!data["vendor"].is_object()) data["vendor"] = Json::object();
    if (!data["vendor"]["trussc"].is_object()) data["vendor"]["trussc"] = Json::object();
    return data["vendor"]["trussc"];
}

void setLocal(const fs::path& project, const std::string& name, const std::string& path) {
    auto spec = parse(name);
    if (!spec.owner.empty() || !spec.ref.empty()) throw std::runtime_error("add-local expects an unqualified addon name");
    auto data = presets(project);
    auto& local = vendor(data)["addonLocalPaths"];
    if (path.empty()) { if (local.is_object()) local.erase(name); }
    else {
        auto root = fs::canonical(path);
        if (!fs::is_directory(root)) throw std::runtime_error("local addon path must be a directory");
        local[name] = root.generic_string();
    }
    writeGeneratedFile((project / "CMakePresets.json").string(), data.dump(2));
}

void saveResolved(const fs::path& project, const Paths& paths) {
    auto data = presets(project);
    vendor(data)["addonPaths"] = paths;
    vendor(data)["addonOrder"] = paths.order;
    writeGeneratedFile((project / "CMakePresets.json").string(), data.dump(2));
}

using Version = std::array<unsigned long, 3>;
static bool version(const std::string& text, Version& value, unsigned& count) {
    value = {};
    count = 0;
    size_t offset = !text.empty() && text.front() == 'v' ? 1 : 0;
    while (offset < text.size() && count < 3) {
        auto dot = text.find('.', offset);
        auto end = dot == std::string::npos ? text.size() : dot;
        auto parsed = std::from_chars(text.data() + offset, text.data() + end, value[count]);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + end) return false;
        ++count;
        if (dot == std::string::npos) return true;
        offset = dot + 1;
    }
    return false;
}

// Common addon range syntax: comparisons, comma/space AND, ||, ^, ~ and x/*.
// Unknown versions or syntax get an explicit diagnostic rather than a false mismatch.
static void checkRange(const std::string& name, std::string selected, const std::string& range) {
    Version current{};
    unsigned count;
    if (!version(selected, current, count)) return;
    bool accepted = false, understood = true;
    std::istringstream alternatives(range);
    std::string alternative;
    while (std::getline(alternatives, alternative, '|')) {
        if (alternative.empty()) continue;
        std::replace(alternative.begin(), alternative.end(), ',', ' ');
        std::istringstream terms(alternative);
        std::string term;
        bool matches = true;
        while (terms >> term) {
            if (term == "*" || term == "x" || term == "X") continue;
            auto first = term.find_first_not_of("<>=^~");
            auto op = term.substr(0, first);
            if (first == std::string::npos) {
                std::string number;
                if (!(terms >> number)) { understood = false; break; }
                term += number;
                first = op.size();
            }
            auto number = term.substr(first);
            auto wildcard = number.find_first_of("xX*");
            if (wildcard != std::string::npos) {
                if (wildcard == 0) continue;
                if (number[wildcard - 1] != '.') { understood = false; break; }
                number.resize(wildcard - 1);
            }
            Version bound{};
            unsigned parts;
            if (!version(number, bound, parts)) { understood = false; break; }
            if (op == ">=") matches &= current >= bound;
            else if (op == ">") matches &= current > bound;
            else if (op == "<=") matches &= current <= bound;
            else if (op == "<") matches &= current < bound;
            else if (op == "^" || op == "~") {
                auto upper = bound;
                unsigned index = op == "~" ? (parts > 1 ? 1 : 0) :
                    (bound[0] != 0 || parts == 1 ? 0 : (bound[1] != 0 || parts == 2 ? 1 : 2));
                ++upper[index];
                for (unsigned i = index + 1; i < 3; ++i) upper[i] = 0;
                matches &= current >= bound && current < upper;
            } else if (op.empty() || op == "=") {
                for (unsigned i = 0; i < parts; ++i) matches &= current[i] == bound[i];
            } else { understood = false; break; }
        }
        accepted |= matches;
    }
    if (!understood) projectOps::diagnostic(name + " has an unsupported trussc_version range '" + range + "'", projectOps::MessageKind::Warning);
    else if (!accepted) projectOps::diagnostic(name + " requires TrussC " + range + "; project selects " + selected, projectOps::MessageKind::Warning);
}

Paths resolve(const fs::path& project, const fs::path& trussc, bool fetch, const Lookup& lookup, const std::vector<std::string>* selection) {
    size_t graphWork = 0;
    auto countWork = [&] {
        // Bound (untrusted dependency graph): cap total nodes and edges at 4096.
        if (++graphWork > 4096) throw std::runtime_error("addon dependency graph exceeds the 4096 node/edge limit");
    };
    std::map<std::string, Spec> explicitSpecs;
    std::vector<Spec> pending;
    auto make = project / "addons.make";
    if (selection || fs::exists(make)) {
        std::string contents;
        if (selection) { for (const auto& line : *selection) contents += line + "\n"; }
        // Bound (project selection): cap addons.make at 16 MiB.
        else contents = trusscVersion::readBounded(make, 16 * 1024 * 1024);
        std::istringstream lines(contents);
        std::string line;
        while (std::getline(lines, line)) {
            auto start = line.find_first_not_of(" \t\r");
            if (start == std::string::npos || line[start] == '#') continue;
            auto end = line.find_last_not_of(" \t\r");
            auto spec = parse(line.substr(start, end - start + 1));
            countWork();
            if (!explicitSpecs.emplace(spec.name, spec).second) throw std::runtime_error("duplicate addon in addons.make: " + spec.name);
        }
    }
    auto data = presets(project);
    Json local = Json::object();
    Json savedPaths = Json::object();
    if (data.contains("vendor") && data["vendor"].is_object() && data["vendor"].contains("trussc") && data["vendor"]["trussc"].is_object()) {
        local = data["vendor"]["trussc"].value("addonLocalPaths", Json::object());
        savedPaths = data["vendor"]["trussc"].value("addonPaths", Json::object());
    }
    Paths result;
    bool external = false;
    std::map<std::string, Spec> chosen;
    std::string selected;
    if (fs::exists(project / ".trussc-version")) {
        // Bound (project version): cap the version selection at 4 KiB.
        auto text = trusscVersion::readBounded(project / ".trussc-version", 4096);
        auto start = text.find_first_not_of(" \t\r\n");
        auto end = text.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) selected = trusscVersion::parse(text.substr(start, end - start + 1)).ref;
    }
    std::map<std::string, Source> sources;
    std::map<std::string, std::set<std::string>> edges;
    auto enqueue = [&](Spec spec) {
        auto projectSpec = explicitSpecs.find(spec.name);
        if (projectSpec != explicitSpecs.end()) {
            auto defaultRef = [](const std::string& ref) { return ref.empty() ? "HEAD" : ref; };
            if (defaultRef(projectSpec->second.ref) != defaultRef(spec.ref)) {
                projectOps::diagnostic("dependency " + spec.name + "@" + (spec.ref.empty() ? "HEAD" : spec.ref)
                    + " conflicts with project " + projectSpec->second.name + "@"
                    + (projectSpec->second.ref.empty() ? "HEAD" : projectSpec->second.ref) + "; project wins", projectOps::MessageKind::Warning);
            }
            spec = projectSpec->second;
        }
        auto installed = trussc / "addons" / spec.name;
        Source source;
        if (!local.contains(spec.name) && (!spec.owner.empty() || !spec.ref.empty() ||
            !fs::is_directory(installed) || fs::exists(installed / ".git"))) {
            source = lookup(spec, fetch);
        }
        Spec identity = spec;
        if (identity.owner.empty()) identity.owner = source.owner;
        if (identity.ref.empty()) identity.ref = "HEAD";
        auto previous = chosen.find(spec.name);
        if (previous != chosen.end()) {
            if (previous->second.owner != identity.owner || previous->second.ref != identity.ref) {
                throw std::runtime_error("conflicting addon dependencies for " + spec.name + "; select a ref in addons.make");
            }
            return;
        }
        if (projectSpec == explicitSpecs.end()) countWork();
        chosen[spec.name] = identity;
        sources[spec.name] = source;
        pending.push_back(spec);
    };
    for (const auto& entry : explicitSpecs) enqueue(entry.second);
    for (size_t index = 0; index < pending.size(); ++index) {
        auto spec = pending[index];
        auto installed = trussc / "addons" / spec.name;
        bool bundled = fs::is_directory(installed) && !fs::exists(installed / ".git");
        if (bundled && !spec.ref.empty()) throw std::runtime_error("bundled addon " + spec.name + " cannot use @ref; its version follows TrussC");
        fs::path path;
        if (local.contains(spec.name)) {
            if (!local[spec.name].is_string()) throw std::runtime_error("addon local path must be a string");
            path = local[spec.name].get<std::string>();
            if (path.is_relative()) path = project / path;
            if (!fs::is_directory(path)) throw std::runtime_error("local addon path is missing: " + path.string());
        } else if (bundled || (spec.ref.empty() && spec.owner.empty() && fs::is_directory(installed))) {
            path = installed;
        } else {
            auto source = sources.at(spec.name);
            auto owner = spec.owner.empty() ? source.owner : spec.owner;
            if (owner.empty()) throw std::runtime_error("unknown owner for addon " + spec.name + "; qualify owner/name or run `trusscli update`");
            auto versionSpec = trusscVersion::parse(owner + "/" + spec.name + "@" + (spec.ref.empty() ? "HEAD" : spec.ref));
            // Dependency manifests retain their author's short ref. Builds use
            // the full ID saved by update, without resolving that prefix again.
            if (!fetch && versionSpec.shortCommit && savedPaths.contains(spec.name) && savedPaths[spec.name].is_string()) {
                auto ref = fs::path(savedPaths[spec.name].get<std::string>()).filename().string();
                auto prefix = spec.ref;
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) {
                    return c >= 'A' && c <= 'F' ? c + ('a' - 'A') : c;
                });
                if (ref.size() == 40 && ref.rfind(prefix, 0) == 0 && trusscVersion::parse(ref).commit) {
                    versionSpec = trusscVersion::parse(owner + "/" + spec.name + "@" + ref);
                }
            }
            path = trusscVersion::acquire(versionSpec, fetch, true, source.url);
            const auto ref = path.filename().string();
            if (fetch && versionSpec.shortCommit && ref.size() == 40 && trusscVersion::parse(ref).commit) {
                result.resolvedPins[spec.name] = ref;
            }
        }
        path = fs::absolute(path).lexically_normal();
        external |= !spec.owner.empty() || !spec.ref.empty() || path != fs::absolute(installed).lexically_normal();
        result[spec.name] = path.generic_string();
        auto meta = manifest(path / "addon.json");
        auto range = meta.value("trussc_version", "");
        if (!range.empty() && !selected.empty()) checkRange(spec.name, selected, range);
        if (meta.contains("dependencies")) {
            if (!meta["dependencies"].is_array()) throw std::runtime_error("addon dependencies must be an array: " + spec.name);
            for (const auto& dependency : meta["dependencies"]) {
                countWork();
                Spec dep;
                if (dependency.is_string()) dep = parse(dependency.get<std::string>());
                else {
                    dep = parse(dependency.at("name").get<std::string>());
                    for (const auto* key : {"commit", "tag", "branch"}) {
                        if (dependency.contains(key)) {
                            dep.ref = dependency.at(key).get<std::string>();
                            trusscVersion::parse("placeholder/" + dep.name + "@" + dep.ref);
                            break;
                        }
                    }
                    if (dep.owner.empty() && dependency.contains("url")) {
                        auto url = dependency.at("url").get<std::string>();
                        const std::string prefix = "https://github.com/";
                        if (url.compare(0, prefix.size(), prefix) != 0) {
                            throw std::runtime_error("addon dependency URL must identify owner/name on GitHub: " + dep.name);
                        }
                        auto identity = url.substr(prefix.size());
                        if (identity.size() >= 4 && identity.substr(identity.size() - 4) == ".git") identity.resize(identity.size() - 4);
                        auto repository = parse(identity);
                        if (repository.owner.empty() || repository.name != dep.name || !repository.ref.empty()) {
                            throw std::runtime_error("addon dependency URL must identify owner/name on GitHub: " + dep.name);
                        }
                        dep.owner = repository.owner;
                    }
                    // Validate pinned dependency refs even when the project overrides them.
                    if (!dep.ref.empty()) trusscVersion::parse((dep.owner.empty() ? "placeholder" : dep.owner) + "/" + dep.name + "@" + dep.ref);
                }
                edges[spec.name].insert(dep.name);
                enqueue(dep);
            }
        }
    }
    // Kahn's order keeps every static parent before all of its dependencies,
    // including dependencies explicitly selected earlier in addons.make.
    std::map<std::string, size_t> parents;
    for (const auto& spec : pending) parents[spec.name] = 0;
    for (const auto& entry : edges) for (const auto& child : entry.second) ++parents[child];
    std::vector<std::string> ordered;
    for (const auto& spec : pending) if (parents[spec.name] == 0) ordered.push_back(spec.name);
    for (size_t i = 0; i < ordered.size(); ++i) {
        for (const auto& child : edges[ordered[i]]) if (--parents[child] == 0) ordered.push_back(child);
    }
    if (ordered.size() != pending.size()) throw std::runtime_error("cyclic addon dependencies");
    for (const auto& name : ordered) {
        if (!result.order.empty()) result.order += ";";
        result.order += name;
    }
    if (external) {
        auto hook = trussc / "core/cmake/use_addon.cmake";
        // Bound (core capability check): cap the CMake hook at 16 MiB.
        if (!fs::exists(hook) || trusscVersion::readBounded(hook, 16 * 1024 * 1024).find("TRUSSC_ADDON_PATHS") == std::string::npos) {
            throw std::runtime_error("TrussC " + (selected.empty() ? trussc.string() : selected) +
                " cannot load external addon paths (TRUSSC_ADDON_PATHS is unavailable); select a newer TrussC version");
        }
    }
    return result;
}
}
