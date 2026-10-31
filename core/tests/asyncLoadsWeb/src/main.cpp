#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>
using namespace std;
using namespace tc;
namespace {
#include "../../common/resourceLoadSmoke.h"
}
TC_CORE_TEST_MAIN() { return resourceLoadSmoke(); }
