#pragma once

// Run a command and receive its stdout and stderr as ONE ordered byte stream
// (#1240). Both streams of the child are the same pipe, so a compiler
// diagnostic (stderr) arrives in the right place between the build tool's
// progress lines (stdout). No shell is involved.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Calls onData with each piece of output as it arrives. Returns the child's
// exit code (-1 when it cannot be started or ended by a signal).
//
// Bounded wait: once the direct child has exited, only the output already in
// the pipe is read, so a background process that kept the pipe open cannot
// hold trusscli.
// When stderrFile is supplied, stderr goes there instead of the output pipe.
// Setting cancel from onData terminates the child and stops reading output.
// timeoutMs=0 waits indefinitely; a timeout returns -2.
// quietSpawnFailure suppresses the diagnostic when the child cannot start.
// removeEnvironment lists variables omitted only from the child environment.
int runCaptured(const std::vector<std::string>& argv,
                const std::function<void(const char*, size_t)>& onData,
                FILE* stderrFile = nullptr, const bool* cancel = nullptr,
                int timeoutMs = 0, const std::vector<std::string>& removeEnvironment = {},
                bool quietSpawnFailure = false);
