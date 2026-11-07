#pragma once
#include <filesystem>
#include <vector>

namespace trussc {
class App;
namespace internal {
// Small native-to-Runtime bridge, also usable by the standalone sokol backend.
void receiveFilesOpened(std::vector<std::filesystem::path> files);
void finishFilesOpenedSetup(const App& app);
}
}
