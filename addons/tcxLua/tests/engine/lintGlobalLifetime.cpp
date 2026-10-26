#include "lintGlobalLifetime.h"
#include <cstdio>
int main() {
    bool ok = lintGlobalLifetime();
    std::printf("global candidate survives lookup and GC: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
