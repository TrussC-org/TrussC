// Helpers for playback device selection (not public API).
#pragma once

#include "miniaudio.h"

namespace trussc {
namespace internal {

// No selected ID means the system default was opened, including fallback
// from an unknown name. Otherwise use the selected device's enumeration flag.
bool openedDeviceIsDefault(const ma_device_id* selectedID,
                           const ma_device_info* infos, ma_uint32 count);

} // namespace internal
} // namespace trussc
