#pragma once

#include <filesystem>
#include <string>

// Write generated text with LF line endings, except .bat / .cmd, which get CRLF
// as .gitattributes requires for Windows batch files. Uses core's checked atomic save;
// throws on failure so the command/GUI boundary reports the error exactly once.
// Relative CLI paths are relative to the working directory, not bin/data.
void writeGeneratedFile(const std::filesystem::path& path, const std::string& text);

// Remove a build directory. A symlink there (build-linux pointing at another
// disk, a RAM disk, ...) is the user's chosen location, so it is kept and only
// its contents are cleared; a real directory is removed whole. Inside either,
// a symlink is removed itself and never followed, so nothing outside the
// build directory is touched. A symlink whose target is missing is left alone:
// callers see no directory there, and cmake then reports the broken link.
// Throws std::filesystem::filesystem_error on failure.
void removeBuildDirectory(const std::filesystem::path& dir);
