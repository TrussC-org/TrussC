#pragma once

// Run a command and receive its stdout and stderr as ONE ordered byte stream
// (#1240). Both streams of the child are the same pipe, so a compiler
// diagnostic (stderr) arrives in the right place between the build tool's
// progress lines (stdout). No shell is involved.

#include <functional>
#include <string>
#include <vector>

// Calls onData with each piece of output as it arrives. Returns the child's
// exit code (-1 when it cannot be started or ended by a signal).
//
// Bounded wait: once the direct child has exited, only the output already in
// the pipe is read, so a background process that kept the pipe open cannot
// hold trusscli.
int runCaptured(const std::vector<std::string>& argv,
                const std::function<void(const char*, size_t)>& onData);
