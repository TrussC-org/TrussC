#pragma once
#include <filesystem>
#include <vector>

namespace trussc {
class App;
namespace internal {
// Small native-to-Runtime bridge called from sokol_app_tc.h. Like the other
// Runtime hooks there, a standalone sokol fixture must define a stub.
void receiveFilesOpened(std::vector<std::filesystem::path> files);
void finishFilesOpenedSetup(const App& app);
}
}
