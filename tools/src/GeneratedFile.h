#pragma once

#include <filesystem>
#include <string>

// Write generated text with LF line endings. Uses core's checked atomic save;
// throws on failure so the command/GUI boundary reports the error exactly once.
// Relative CLI paths are relative to the working directory, not bin/data.
void writeGeneratedFile(const std::filesystem::path& path, const std::string& text);
