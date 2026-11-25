#pragma once
#include <TrussC.h>
#include <fstream>

namespace tcCoreTest {
// Generate a silent 80-sample WAV so background delivery needs no asset/audio device.
inline bool writeBackgroundWav(const trussc::fs::path& path) {
    std::ofstream file(path, std::ios::binary);
    const unsigned char header[] = {
        'R','I','F','F',196,0,0,0,'W','A','V','E','f','m','t',' ',
        16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,
        'd','a','t','a',160,0,0,0
    };
    const char silence[160]{};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(silence, sizeof(silence));
    return bool(file);
}
}
