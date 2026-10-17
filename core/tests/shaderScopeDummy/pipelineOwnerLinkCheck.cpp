// A Debug header consumer must link against a Release/RelWithDebInfo core.
#undef NDEBUG
#include <TrussC.h>

int main() {
    return trussc::internal::pipelineOwnerCtx().empty() ? 0 : 1;
}
