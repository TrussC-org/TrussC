#include "BuildOutputFilter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

using namespace std;

namespace {

// A single line is never held longer than this; a longer run is handled as
// several lines. Bounds memory for output without newlines.
constexpr size_t kMaxLine = 64 * 1024;
// Names kept per diagnostic kind. The counts stay exact beyond it.
constexpr size_t kMaxNames = 1000;
// Names shown in the ERROR / WARNING summary lines.
constexpr size_t kShownNames = 3;
// compile_commands.json bigger than this is not read.
constexpr uintmax_t kMaxCompileCommandsBytes = 256u * 1024 * 1024;

bool startsWith(const string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

// Remove ANSI escape sequences (colored compiler output) for matching.
string stripAnsi(const string& s) {
    if (s.find('\x1b') == string::npos) return s;
    string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

// "[ 42%] text" (Make) or "[12/44] text" (Ninja). On a match, percent is the
// progress and rest is the text after the prefix.
bool parseProgressPrefix(const string& s, int& percent, string& rest) {
    if (s.empty() || s[0] != '[') return false;
    size_t i = 1;
    while (i < s.size() && s[i] == ' ') ++i;
    const size_t digitsStart = i;
    long first = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - digitsStart < 9) {
        first = first * 10 + (s[i] - '0');
        ++i;
    }
    if (i == digitsStart || i >= s.size()) return false;
    long value = 0;
    if (s[i] == '%') {
        if (i + 1 >= s.size() || s[i + 1] != ']') return false;
        value = first;
        i += 2;
    } else if (s[i] == '/') {
        ++i;
        const size_t totalStart = i;
        long total = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - totalStart < 9) {
            total = total * 10 + (s[i] - '0');
            ++i;
        }
        if (i == totalStart || total <= 0 || i >= s.size() || s[i] != ']') return false;
        value = first * 100 / total;
        ++i;
    } else {
        return false;
    }
    if (i < s.size() && s[i] == ' ') ++i;
    percent = static_cast<int>(min(value, 100L));
    rest = s.substr(i);
    return true;
}

// "Building CXX object ..." (the tool's compile step, after any prefix).
bool isBuildingObject(const string& rest) {
    if (!startsWith(rest, "Building ")) return false;
    const size_t objectAt = rest.find(" object ", 9);
    return objectAt != string::npos && objectAt <= 9 + 8;
}

// Xcode's per-file steps (no percent in this format).
bool isXcodeCompileStep(const string& s) {
    return startsWith(s, "CompileC ") || startsWith(s, "SwiftCompile ") ||
           startsWith(s, "CompileSwift ") || startsWith(s, "CompileMetalFile ");
}

string baseName(const string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == string::npos ? path : path.substr(slash + 1);
}

string trimRight(string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

// "C2065", "LNK2019", "D9025": letters then digits.
bool isMsvcCode(const string& s, size_t at, string* code = nullptr) {
    size_t i = at;
    while (i < s.size() && s[i] >= 'A' && s[i] <= 'Z' && i - at < 5) ++i;
    const size_t letters = i - at;
    const size_t digitsStart = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    const size_t digits = i - digitsStart;
    if (letters == 0 || digits < 3 || digits > 5) return false;
    if (i < s.size() && s[i] != ':' && s[i] != ' ') return false;
    if (code) *code = s.substr(at, i - at);
    return true;
}

struct DiagHit {
    bool found = false;
    bool error = false;
    bool msvc = false;
    string prefix;      // text before the marker
    string code;        // MSVC code, e.g. "C2065"
    string message;     // text after the marker
};

// Find a GCC/Clang "...: error:" / "...: warning:" or an MSVC
// "...: error C2065:" marker. The leftmost one wins.
DiagHit findDiagnostic(const string& s) {
    static const struct { const char* needle; bool error; bool msvc; } kNeedles[] = {
        {": error:", true, false},        {": warning:", false, false},
        {": fatal error:", true, false},  {": error ", true, true},
        {": warning ", false, true},      {": fatal error ", true, true},
    };
    DiagHit best;
    size_t bestAt = string::npos;
    for (const auto& n : kNeedles) {
        size_t from = 0;
        while (true) {
            const size_t at = s.find(n.needle, from);
            if (at == string::npos || at >= bestAt) break;
            const size_t after = at + strlen(n.needle);
            string code;
            if (n.msvc && !isMsvcCode(s, after, &code)) { from = at + 1; continue; }
            bestAt = at;
            best.found = true;
            best.error = n.error;
            best.msvc = n.msvc;
            best.prefix = trimRight(s.substr(0, at));
            best.code = code;
            best.message = s.substr(after);
            break;
        }
    }
    return best;
}

bool isLinkerTool(const string& tool) {
    static const char* kLinkers[] = {"collect2", "ld", "ld.lld", "lld", "ld.gold", "ld.bfd",
                                     "ld64.lld", "lld-link", "wasm-ld", "link", "LINK"};
    const string name = baseName(tool);
    for (const char* l : kLinkers) if (name == l) return true;
    return false;
}

// Strip up to two trailing ":digits" (line, column). True if any was removed.
bool stripLineColumn(string& s) {
    bool stripped = false;
    for (int i = 0; i < 2; ++i) {
        const size_t colon = s.find_last_of(':');
        if (colon == string::npos || colon + 1 >= s.size()) break;
        bool digits = true;
        for (size_t k = colon + 1; k < s.size(); ++k) digits = digits && s[k] >= '0' && s[k] <= '9';
        if (!digits) break;
        s.erase(colon);
        stripped = true;
    }
    return stripped && !s.empty();
}

void addName(vector<string>& names, unordered_set<string>& seen, const string& name) {
    if (names.size() >= kMaxNames || name.empty()) return;
    if (seen.insert(name).second) names.push_back(name);
}

string namesText(const vector<string>& names) {
    string out;
    for (size_t i = 0; i < names.size() && i < kShownNames; ++i) {
        if (i) out += ", ";
        out += names[i];
    }
    if (names.size() > kShownNames) out += ", +" + to_string(names.size() - kShownNames) + " more";
    return out;
}

string filesText(long n) { return to_string(n) + (n == 1 ? " file" : " files"); }

} // namespace

BuildOutputFilter::BuildOutputFilter(ostream& screen, ostream* log, const Options& options)
    : screen_(screen), log_(log), options_(options) {}

void BuildOutputFilter::begin() {
    if (options_.verbose || options_.phase != Phase::Build) return;
    screen_ << "Building " << options_.project;
    if (options_.totalFiles >= 0) screen_ << " (" << filesText(options_.totalFiles) << ")";
    screen_ << " ";
    open_ = true;
    screen_.flush();
}

void BuildOutputFilter::write(const char* data, size_t size) {
    if (log_ && size) {
        log_->write(data, static_cast<streamsize>(size));
        log_->flush();
    }
    size_t pos = 0;
    while (pos < size) {
        const void* newline = memchr(data + pos, '\n', size - pos);
        size_t end = newline ? static_cast<size_t>(static_cast<const char*>(newline) - data) : size;
        // Never hold more than kMaxLine: a longer run is handled as several lines.
        const size_t room = kMaxLine - partial_.size();
        const bool tooLong = end - pos > room;
        if (tooLong) end = pos + room;
        partial_.append(data + pos, end - pos);
        pos = end;
        if (newline && !tooLong) ++pos;
        if (newline || tooLong) {
            string line;
            line.swap(partial_);
            handleLine(move(line));
        }
    }
    screen_.flush();
}

void BuildOutputFilter::handleLine(string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (options_.verbose) {
        screen_ << line << '\n';
        return;
    }
    const string plain = stripAnsi(line);

    int percent = 0;
    string rest;
    const bool prefixed = parseProgressPrefix(plain, percent, rest);
    if (!prefixed) rest = plain;

    bool hidden = false;
    if (startsWith(plain, "-- ")) {
        hidden = true;
    } else if (options_.phase == Phase::Build) {
        if (prefixed) {
            sawPercent_ = true;
            advanceProgressTo(percent);
        }
        if (prefixed && isBuildingObject(rest)) {
            ++compiled_;
            hidden = true;
        } else if (prefixed && startsWith(rest, "Building ")) {
            hidden = true;
        } else if (prefixed && startsWith(rest, "Linking ")) {
            hidden = true;
        } else if (prefixed && rest.size() > 1 && rest[0] == '[' &&
                   rest.find("] Compiling shader: ") != string::npos) {
            hidden = true;
        } else if (!prefixed && isXcodeCompileStep(plain)) {
            // Another format: no percent, so one dot per compile step.
            ++compiled_;
            hidden = true;
            if (!sawPercent_) dot();
        }
    }
    if (startsWith(rest, "Built target ") || startsWith(rest, "Scanning dependencies")) hidden = true;

    if (options_.phase == Phase::Build) classifyDiagnostic(plain);
    if (!hidden) printLine(line);
}

void BuildOutputFilter::classifyDiagnostic(const string& line) {
    // Cheap test first; most lines are not diagnostics.
    if (line.find("error") == string::npos && line.find("warning") == string::npos) return;
    const DiagHit hit = findDiagnostic(line);
    if (!hit.found) return;

    string tool;
    string file;
    if (hit.msvc) {
        string prefix = hit.prefix;
        const size_t open = prefix.find_last_of('(');
        if (!prefix.empty() && prefix.back() == ')' && open != string::npos && open > 0) {
            file = prefix.substr(0, open);
        } else {
            tool = prefix;
        }
    } else {
        string prefix = hit.prefix;
        if (stripLineColumn(prefix)) file = prefix;
        else tool = prefix;
    }

    const bool linker = hit.error && file.empty() &&
        (startsWith(hit.code, "LNK") || isLinkerTool(tool) ||
         hit.message.find("linker command failed") != string::npos ||
         hit.message.find("ld returned") != string::npos);
    if (linker) {
        diag_.linkFailed = true;
        return;
    }
    if (hit.error) {
        ++diag_.errors;
        addName(diag_.errorFiles, diag_.errorSeen, baseName(file));
    } else {
        ++diag_.warnings;
        addName(diag_.warningFiles, diag_.warningSeen, baseName(file));
    }
}

void BuildOutputFilter::advanceProgressTo(int percent) {
    const int target = max(0, min(percent, 100)) / 5;
    while (dotsPrinted_ < target) {
        ++dotsPrinted_;
        const int at = dotsPrinted_ * 5;
        if (at % 50 == 0) screen_ << at << "%";
        else screen_ << ".";
        open_ = true;
    }
}

void BuildOutputFilter::dot() {
    screen_ << ".";
    open_ = true;
}

void BuildOutputFilter::closeProgressLine() {
    if (!open_) return;
    screen_ << '\n';
    open_ = false;
}

void BuildOutputFilter::printLine(const string& line) {
    // A line that interrupts the progress line starts on a fresh line; the
    // dots continue on another fresh line afterwards.
    closeProgressLine();
    screen_ << line << '\n';
}

void BuildOutputFilter::printDiagnosticSummary() {
    if (diag_.errors > 0 || diag_.linkFailed) {
        if (diag_.errors == 0) {
            screen_ << "ERROR: link failed\n";
        } else {
            vector<string> names = diag_.errorFiles;
            if (diag_.linkFailed) names.push_back("link failed");
            screen_ << "ERROR: " << diag_.errors + (diag_.linkFailed ? 1 : 0);
            if (!names.empty()) screen_ << " (" << namesText(names) << ")";
            screen_ << '\n';
        }
    }
    if (diag_.warnings > 0) {
        screen_ << "WARNING: " << diag_.warnings;
        if (!diag_.warningFiles.empty()) screen_ << " (" << namesText(diag_.warningFiles) << ")";
        screen_ << '\n';
    }
}

void BuildOutputFilter::end(int exitCode, double seconds) {
    if (!partial_.empty()) {
        string line;
        line.swap(partial_);
        handleLine(move(line));
    }
    if (options_.verbose) {
        screen_.flush();
        return;
    }
    const bool ok = exitCode == 0;
    const string logPart = options_.logDisplayPath.empty()
        ? "" : " Full log: " + options_.logDisplayPath;

    if (options_.phase == Phase::Configure) {
        if (!ok) {
            closeProgressLine();
            screen_ << "Failed: configure stopped";
            if (seconds >= 0) screen_ << " (" << formatElapsed(seconds) << ")";
            screen_ << "." << logPart << '\n';
        }
        screen_.flush();
        return;
    }

    closeProgressLine();
    string what;
    const long total = options_.totalFiles;
    if (ok) {
        if (compiled_ == 0) {
            what = total >= 0 ? "all " + to_string(total) + " up to date" : "nothing to compile";
        } else {
            what = filesText(compiled_);
            if (total > compiled_) what += ", " + to_string(total - compiled_) + " up to date";
        }
    } else {
        what = compiled_ == 0 ? "stopped before compiling" : "stopped after " + filesText(compiled_);
    }
    screen_ << (ok ? "Done: " : "Failed: ") << what << " (" << formatElapsed(seconds) << ")."
            << logPart << '\n';
    printDiagnosticSummary();
    screen_.flush();
}

long countCompileCommands(const string& path) {
    ifstream in(path, ios::binary | ios::ate);
    if (!in) return -1;
    const auto size = in.tellg();
    if (size <= 0 || static_cast<uintmax_t>(size) > kMaxCompileCommandsBytes) return -1;
    in.seekg(0);
    // Count the objects directly inside the top-level array; a small scanner
    // that follows strings and nesting is enough for this fixed format.
    long count = 0;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    char buffer[64 * 1024];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        const streamsize got = in.gcount();
        for (streamsize i = 0; i < got; ++i) {
            const char c = buffer[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{' || c == '[') {
                if (c == '{' && depth == 1) ++count;
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
            }
        }
    }
    return count > 0 ? count : -1;
}

string formatElapsed(double seconds) {
    const long total = seconds < 0 ? 0 : static_cast<long>(seconds + 0.5);
    char text[32];
    snprintf(text, sizeof(text), "%ldm%02lds", total / 60, total % 60);
    return text;
}
