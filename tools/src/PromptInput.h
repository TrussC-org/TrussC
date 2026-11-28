#pragma once

#include <iostream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace trusscli {

// Windows consoles supply UTF-16; pipes and files keep their original bytes.
inline bool readPromptLine(std::string& line) {
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(input, &mode)) {
        // Like the CRT stream, end of input (Ctrl+Z) stays ended for later prompts.
        static bool ended = false;
        line.clear();
        if (ended) return false;
        std::wstring wide;
        for (;;) {
            wchar_t buffer[256];
            DWORD count = 0;
            if (!ReadConsoleW(input, buffer, 256, &count, nullptr) || count == 0) {
                ended = true;
                return false;
            }
            std::wstring chunk(buffer, count);
            // Consume the LF too, including when CRLF crosses a read boundary.
            auto ending = chunk.find(L'\n');
            wide.append(chunk, 0, ending);
            if (ending != std::wstring::npos) break;
        }
        if (!wide.empty() && wide.front() == L'\x1a') {
            ended = true;
            return false;
        }
        if (!wide.empty() && wide.back() == L'\r') wide.pop_back();
        if (wide.empty()) return true;
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                       nullptr, 0, nullptr, nullptr);
        if (bytes == 0) return false;
        line.resize(bytes);
        return WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                   line.data(), bytes, nullptr, nullptr) != 0;
    }
#endif
    return static_cast<bool>(std::getline(std::cin, line));
}

} // namespace trusscli
