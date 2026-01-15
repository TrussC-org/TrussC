// =============================================================================
// main.cpp - TCV Encoder entry point
// =============================================================================

#include "tcApp.h"

// Global storage for command line args
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
    settings.setSize(800, 600);
    settings.setTitle("TCV Encoder");

    return tc::runApp<tcApp>(settings);
}
