// Test driver for tools/src/BuildOutputFilter (#1240): feeds stdin through the
// filter and prints what the screen would show. Built by
// test_build_output_filter.py; not part of trusscli.
//
//   driver [--project NAME] [--total N] [--verbose] [--configure] [--exit CODE]
//          [--seconds S] [--log-display TEXT] [--log-file PATH] [--chunk BYTES]
#include "BuildOutputFilter.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace std;

int main(int argc, char** argv) {
    BuildOutputFilter::Options options;
    options.project = "myApp";
    string logFile;
    int exitCode = 0;
    double seconds = 0;
    size_t chunk = 0;
    for (int i = 1; i < argc; ++i) {
        const string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? string(argv[++i]) : string(); };
        if (a == "--project") options.project = next();
        else if (a == "--total") options.totalFiles = atol(next().c_str());
        else if (a == "--verbose") options.verbose = true;
        else if (a == "--configure") options.phase = BuildOutputFilter::Phase::Configure;
        else if (a == "--exit") exitCode = atoi(next().c_str());
        else if (a == "--seconds") seconds = atof(next().c_str());
        else if (a == "--log-display") options.logDisplayPath = next();
        else if (a == "--log-file") logFile = next();
        else if (a == "--chunk") chunk = static_cast<size_t>(atol(next().c_str()));
    }
    ofstream log;
    if (!logFile.empty()) log.open(logFile, ios::binary | ios::trunc);
    const string input((istreambuf_iterator<char>(cin)), istreambuf_iterator<char>());

    BuildOutputFilter filter(cout, logFile.empty() ? nullptr : &log, options);
    filter.begin();
    // chunk > 0 feeds small pieces, so lines arrive split across writes
    const size_t step = chunk ? chunk : input.size() + 1;
    for (size_t at = 0; at < input.size(); at += step) {
        filter.write(input.data() + at, min(step, input.size() - at));
    }
    filter.end(exitCode, seconds);
    return 0;
}
