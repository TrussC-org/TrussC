#pragma once

#include <string>

// cmd /s removes the outer pair of quotes around /c's entire command.
// Paths inside command must also be quoted, including batch file paths.
inline std::string windowsShellCommandLine(const std::string& command,
                                          const std::string& interpreter = "cmd.exe") {
    return "\"" + interpreter + "\" /d /s /c \"" + command + '"';
}
