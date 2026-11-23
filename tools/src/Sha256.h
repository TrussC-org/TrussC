#pragma once
#include <istream>
#include <string>

// Hash bytes from the current stream position through EOF.
std::string sha256(std::istream& input);
