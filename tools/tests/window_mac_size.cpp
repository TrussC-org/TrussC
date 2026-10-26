// Actual shared Window dispatcher; native calls only record their point sizes.
#include "core/include/sokol/util/sokol_window_size.h"
#include "core/include/tc/app/internal/tcWindowFullscreen.h"
#include <string>
#include <limits>
#include <cmath>
#include <cassert>
#undef __linux__
#define __APPLE__
#define TARGET_OS_IPHONE 0
namespace trussc {
struct IVec2 { int x,y; };
struct Window;
float dpi = 2;
float sapp_dpi_scale() { return dpi; }
bool sapp_isvalid() { return true; }
bool sapp_is_fullscreen() { return false; }
void sapp_toggle_fullscreen() {}
void sapp_set_window_title(const char*) {}
using CGFloat = double;
double pointWidth = 0, pointHeight = 0;
void recordMac(double width, double height) { pointWidth=width; pointHeight=height; }
namespace internal {
enum class WindowControl { Title, Size, PixelSize, Position, Fullscreen, PositionQuery, ToggleFullscreen };
struct WindowRequestAccess {
    static bool ignoreGeometry(Window&, WindowGeometryCall) { return false; }
};
void setMainFullscreen(bool) {}
constexpr bool windowSizeIsScreenControlled() { return false; }
constexpr bool supportsSecondaryWindows() { return TEST_SECONDARY; }
using Hook = bool (*)(Window&, WindowControl, int, int, const std::string*, IVec2&);
Hook windowControlHookForTests() { return nullptr; }
void setMainWindowSizeFractionalMac(double width, double height) { recordMac(MAC_MAIN_CALL); }
void setMainWindowSizeLogical(int width, int height) { pointWidth=width; pointHeight=height; }
void setMainWindowPosition(int,int) {}
IVec2 mainWindowPosition() { return {}; }
}
namespace mcp::internal { void mainWindowTitle(const std::string&) {} }
struct Window {
    struct { bool isMain = false; float dpiScale = 2; } ctx_;
    std::string title_;
    bool isOpen() const { return true; }
    void setSecondaryTitle(const std::string&) {}
    void setSecondaryPosition(int,int) {}
    bool isSecondaryFullscreen() const { return false; }
    void setSecondaryFullscreen(bool) {}
    IVec2 getSecondaryPosition() { return {}; }
    void setSecondarySize(double width,double height,bool=false) { recordMac(MAC_SECONDARY_CALL); }
    IVec2 control(internal::WindowControl,int,int,const std::string* =nullptr, internal::WindowGeometryCall = internal::WindowGeometryCall::WindowSize);
};
CONTROL_BODY
}
int main() {
    using namespace trussc;
    for (bool mainWindow : {true, false}) {
        if (!mainWindow && !TEST_SECONDARY) continue;
        Window window; window.ctx_.isMain=mainWindow;
        window.control(internal::WindowControl::PixelSize,1001,667);
        assert(pointWidth==500.5 && pointHeight==333.5);
        assert(std::ceil(pointWidth)*2==1002 && std::ceil(pointHeight)*2==668);
    }
}
