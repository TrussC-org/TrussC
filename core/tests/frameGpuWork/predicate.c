// Compile and exercise the shared predicate as C, without any TrussC headers.
#include "tc/app/tcGpuFrame.h"

bool frame_gpu_work_from_c(void) {
    return tc_internal_gpu_frame_has_work();
}
