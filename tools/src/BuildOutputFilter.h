#pragma once

// Short screen output for `trusscli build` / `run` / `update` (#1240).
//
// BuildOutputFilter takes the merged stdout+stderr of a build as raw bytes
// and does two things with it:
//   - the log stream (if any) gets every byte, unchanged;
//   - the screen gets a one-line progress bar (a dot per 5 %), only the lines
//     that are not known noise, and a short summary at the end.
// It has no dependency on the rest of trusscli, so the tests feed it fixtures
// directly (tools/tests/test_build_output_filter.py).
//
// Hidden lines (known noise only; everything else passes through):
//   - CMake configure lines "-- ..."
//   - "[ NN%] Building/Linking ..." (Make) and "[n/total] Building/Linking ..." (Ninja)
//   - "Scanning dependencies ...", "Built target ..."
//   - "[ NN%] [<target>] Compiling shader: ..." (TrussC's own shader step)

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_set>
#include <vector>

class BuildOutputFilter {
public:
    enum class Phase { Build, Configure };

    struct Options {
        Phase phase = Phase::Build;
        bool verbose = false;           // pass every line through, no progress or summary
        std::string project;            // name shown in "Building <project> ..."
        long totalFiles = -1;           // compile steps in a full build; -1 if unknown
        std::string logDisplayPath;     // shown in "Full log: ..." ("" = not shown)
    };

    // screen: where the short output goes. log: receives the raw bytes (may be null).
    BuildOutputFilter(std::ostream& screen, std::ostream* log, const Options& options);

    // Build phase: print "Building <project> (N files) " and open the progress line.
    void begin();
    // Feed raw bytes (any chunking; lines may be split across calls).
    void write(const char* data, size_t size);
    // Flush a last unterminated line and print the summary. seconds: elapsed time
    // (negative: not shown).
    void end(int exitCode, double seconds);

    // Number of compile steps seen so far.
    long compiledFiles() const { return compiled_; }

private:
    struct Diagnostics {
        long errors = 0;
        long warnings = 0;
        bool linkFailed = false;
        std::vector<std::string> errorFiles;    // basenames, first-seen order
        std::vector<std::string> warningFiles;
        std::unordered_set<std::string> errorSeen, warningSeen;
    };

    void handleLine(std::string line);
    void classifyDiagnostic(const std::string& line);
    void advanceProgressTo(int percent);
    void dot();
    void closeProgressLine();
    void printLine(const std::string& line);
    void printDiagnosticSummary();

    std::ostream& screen_;
    std::ostream* log_;
    Options options_;
    std::string partial_;           // bytes of the current unterminated line
    bool open_ = false;             // the cursor is in the middle of the progress line
    int dotsPrinted_ = 0;           // progress steps printed (one per 5 %)
    bool sawPercent_ = false;       // progress is read from the build tool, not counted
    long compiled_ = 0;
    Diagnostics diag_;
};

// Number of entries in a compile_commands.json (the compile steps of a full
// build), or -1 if the file is missing, unreadable or too large.
long countCompileCommands(const std::string& path);

// "0m30s" for 30.4 seconds.
std::string formatElapsed(double seconds);
