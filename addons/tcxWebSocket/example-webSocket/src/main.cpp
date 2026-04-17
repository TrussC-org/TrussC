#include <TrussC.h>
#include "tcApp.h"

using namespace tc;

int main() {
    return TC_RUN_APP(tcApp,
        WindowSettings()
            .setSize(800, 600)
            .setTitle("TrussC WebSocket Example")
    );
}
