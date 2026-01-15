// =============================================================================
// main.cpp - TCV Player entry point
// =============================================================================

#include "tcApp.h"

namespace {
    int g_argc = 0;
    char** g_argv = nullptr;
}

int getArgCount() { return g_argc; }
char** getArgValues() { return g_argv; }

int main(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;

    tc::WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("TCV Player");

    return tc::runApp<tcApp>(settings);
}
