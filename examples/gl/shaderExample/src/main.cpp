#include "tcApp.h"

int main() {
    runApp<tcApp>({
        .width = 800,
        .height = 600,
        .title = "shaderExample"
    });
    return 0;
}
