#include "../../include/tc/video/tcGrabberSelection.h"
#include <cstdio>

namespace {
struct Device {
    int deviceId;
    std::string uniqueId;
};
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    failures += !ok;
}
} // namespace

int main() {
    using trussc::internal::resolveGrabberUniqueID;
    const std::vector<Device> devices{{2, "port-left"}, {7, "port-right"}, {9, "カメラ"}};
    check("returns device ID, not list position", resolveGrabberUniqueID(devices, "port-right") == 7);
    check("first device", resolveGrabberUniqueID(devices, "port-left") == 2);
    check("UTF-8 exact match", resolveGrabberUniqueID(devices, "カメラ") == 9);
    check("missing ID has no fallback", resolveGrabberUniqueID(devices, "absent") == -1);
    check("empty list", resolveGrabberUniqueID(std::vector<Device>{}, "port-left") == -1);
    check("empty ID cannot match a missing backend ID", resolveGrabberUniqueID(std::vector<Device>{{0, ""}}, "") == -1);
    check("reordering resolves fresh device ID", resolveGrabberUniqueID(std::vector<Device>{{0, "port-right"}, {1, "port-left"}}, "port-right") == 0);
    return failures ? 1 : 0;
}
