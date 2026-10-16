#pragma once

#include <string>
#include <string_view>

namespace trussc::internal {

inline const char* windowsGuestLinkHint() {
    return "[HotReload] The guest can only use TrussC and the code built into the guest. "
           "Functions defined in main.cpp, or libraries that only the host links, are not visible to it. "
           "If the app builds without TC_HOT_RELOAD, this is the cause: "
           "move that code into the guest side (tcApp) or an addon.";
}

// Target evidence comes from output artifacts or project ownership, never an
// input library or a status message. Later explicit targets take precedence.
// This is only a diagnostic heuristic; it does not inspect unresolved symbols.
inline bool hasWindowsGuestLinkError(std::string_view output) {
    enum class Target { Unknown, Guest, Other };
    Target target = Target::Unknown;
    bool unresolved = false;
    // A confirmed guest failure survives later output from other parallel jobs.
    bool confirmed = false;
    const auto trim = [](std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r");
        if (first == std::string_view::npos) return std::string_view{};
        text.remove_prefix(first);
        text.remove_suffix(text.size() - text.find_last_not_of(" \t\r") - 1);
        return text;
    };
    const auto token = [&](std::string_view text) {
        text = trim(text);
        if (!text.empty() && text.front() == '"') {
            text.remove_prefix(1);
            return text.substr(0, text.find('"'));
        }
        return text.substr(0, text.find_first_of(" \t\r"));
    };
    const auto classify = [&](std::string_view path) {
        path = trim(path);
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
            path.remove_prefix(1);
            path.remove_suffix(1);
        }
        const auto slash = path.find_last_of("/\\");
        if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
        return path == "guest.dll" || path == "guest.lib" || path == "guest.vcxproj"
                   ? Target::Guest : Target::Other;
    };
    while (!output.empty()) {
        const auto end = output.find('\n');
        std::string storage(output.substr(0, end));
        if (end == std::string_view::npos) output = {};
        else output.remove_prefix(end + 1);
        for (char& c : storage) {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        std::string_view line = trim(storage);
        const auto prefix = line.find_first_not_of("0123456789");
        if (prefix != 0 && prefix != std::string_view::npos && line[prefix] == '>') {
            line = trim(line.substr(prefix + 1));
        }
        Target next = Target::Unknown;
        bool linkStep = false;
        for (const auto marker : {"/out:", "-out:"}) {
            const auto pos = line.find(marker);
            if (pos != std::string_view::npos &&
                (pos == 0 || line[pos - 1] == ' ' || line[pos - 1] == '\t')) {
                next = classify(token(line.substr(pos + 5)));
                linkStep = true;
                break;
            }
        }
        if (next == Target::Unknown) {
            for (const auto marker : {"linking cxx shared library ", "linking cxx executable ",
                                      "linking cxx static library ", "failed: "}) {
                const auto pos = line.find(marker);
                if (pos != std::string_view::npos &&
                    (pos == 0 || line[pos - 1] == ' ')) {
                    next = classify(token(line.substr(pos + std::string_view(marker).size())));
                    linkStep = true;
                    break;
                }
            }
        }
        // MSVC's trailing output summary can identify an otherwise unowned error.
        auto summary = line.find(": fatal error ");
        if (summary == std::string_view::npos) summary = line.find(": error ");
        if (next == Target::Unknown && summary != std::string_view::npos) {
            const auto path = trim(line.substr(0, summary));
            const auto dot = path.find_last_of('.');
            const auto ext = dot == std::string_view::npos ? std::string_view{} : path.substr(dot);
            if (ext == ".dll" || ext == ".exe" || ext == ".lib") next = classify(path);
        }
        // Ownership is stronger evidence than a library named in the message.
        const auto bracket = line.rfind('[');
        if (bracket != std::string_view::npos && !line.empty() && line.back() == ']') {
            const auto owner = line.substr(bracket + 1, line.size() - bracket - 2);
            const auto dot = owner.find_last_of('.');
            const auto ext = dot == std::string_view::npos ? std::string_view{} : owner.substr(dot);
            if (ext == ".vcxproj") {
                next = classify(owner);
            }
        }
        if (next != Target::Unknown) {
            // Retain a preceding unowned diagnostic for a following guest step
            // or summary; known target boundaries start a fresh diagnostic block.
            if (target != Target::Unknown && (linkStep || target != next)) unresolved = false;
            target = next;
        }
        bool diagnostic = false;
        for (auto pos = line.find("error "); pos != std::string_view::npos;
             pos = line.find("error ", pos + 6)) {
            if (pos != 0 && line[pos - 1] != ' ' && line[pos - 1] != '\t' && line[pos - 1] != ':') continue;
            const auto code = trim(line.substr(pos + 6));
            diagnostic |= code.substr(0, 8) == "lnk2019:" || code.substr(0, 8) == "lnk2001:";
        }
        if (line.substr(0, 8) == "lld-link") {
            auto message = trim(line.substr(8));
            if (!message.empty() && message.front() == ':') {
                message = trim(message.substr(1));
                if (message.substr(0, 5) == "error") {
                    message = trim(message.substr(5));
                    if (!message.empty() && message.front() == ':') {
                        message = trim(message.substr(1));
                        diagnostic |= message.substr(0, 17) == "undefined symbol:";
                    }
                }
            }
        }
        unresolved |= diagnostic;
        // A line owned by the guest (its project, output or summary) confirms
        // the pending unresolved diagnostic as the guest's.
        if (next == Target::Guest && unresolved) confirmed = true;
    }
    return confirmed || (target == Target::Guest && unresolved);
}

} // namespace trussc::internal
