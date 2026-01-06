#include <TrussC.h>
#include "tcApp.h"

using namespace tc;

int main() {
    return runApp<tcApp>(
        WindowSettings()
            .setSize(800, 600)
            .setTitle("TrussC WebSocket Example")
    );
}
