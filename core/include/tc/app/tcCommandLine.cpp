#include <TrussC.h>

// Mobile share/open mechanisms and browser URLs are not process arguments.
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
namespace trussc {
std::vector<std::string> getCommandLineArgs() { return {}; }
}
#endif
