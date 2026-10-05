/* TrussC: shared by the D3D11 backend and platform-independent tests. */
#ifndef SOKOL_D3D11_DEVICE_LOSS_INCLUDED
#define SOKOL_D3D11_DEVICE_LOSS_INCLUDED
#include <stdint.h>
#include <stdbool.h>

static inline bool _sapp_tc_d3d11_is_device_loss(uint32_t result) {
    /* DXGI_ERROR_DEVICE_REMOVED / DXGI_ERROR_DEVICE_RESET. Compare the
       HRESULT bits so this check also builds without Windows SDK headers. */
    return result == UINT32_C(0x887A0005) || result == UINT32_C(0x887A0007);
}

static inline bool _sapp_tc_d3d11_first_device_loss(uint32_t result, bool* notified) {
    if (!_sapp_tc_d3d11_is_device_loss(result) || *notified) return false;
    *notified = true;  /* before callbacks, including reentrant ones */
    return true;
}
#endif
