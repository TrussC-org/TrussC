#include "tcApp.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

Options options;

int main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (i + 1 == argc) throw std::runtime_error("missing value for " + key);
            const std::string value = argv[++i];
            if (key == "--clip") options.clips.push_back(tc::fs::absolute(tc::utf8ToPath(value)));
            else if (key == "--out") options.output = tc::utf8ToPath(value);
            else {
                size_t used = 0;
                if (key == "--players" || key == "--openclose") {
                    const int number = std::stoi(value, &used);
                    if (used != value.size()) throw std::runtime_error("invalid number: " + value);
                    if (key == "--players") options.players = number;
                    else options.openclose = number;
                } else if (key == "--seconds" || key == "--warmup") {
                    const double number = std::stod(value, &used);
                    if (used != value.size() || !std::isfinite(number)) throw std::runtime_error("invalid number: " + value);
                    if (key == "--seconds") options.seconds = number;
                    else options.warmup = number;
                } else throw std::runtime_error("unknown option: " + key);
            }
        }
        if (options.clips.empty() || options.players < 1 || options.seconds <= 0 ||
            options.warmup < 0 || options.openclose < 0 ||
            (options.clips.size() != 1 && options.clips.size() != size_t(options.players)))
            throw std::runtime_error("use one --clip for all players, or one per player; counts/durations must be valid");
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nUsage: winZeroCopy --clip path [--clip path ...] [--players N] "
            "[--seconds S] [--warmup S] [--openclose K] [--out file.csv]\n";
        return 2;
    }
    tc::WindowSettings settings;
    settings.setSize(1280, 720);
    return TC_RUN_APP(tcApp, settings);
}
