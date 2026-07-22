// The TC_HOT_RELOAD macro below does two jobs:
//  - at cmake configure time, its presence in a .cpp switches this project to
//    the host/guest split build (main.cpp -> host EXE, this file -> libguest)
//  - at compile time it emits the extern "C" create/destroy factories the
//    host's GuestLibrary resolves via dlsym/GetProcAddress
#include "tcApp.h"

TC_HOT_RELOAD(tcApp)
