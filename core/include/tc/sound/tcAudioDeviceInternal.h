// Helpers for playback device selection (not public API).
#pragma once

#include "miniaudio.h"
#include "tcSound.h"

namespace trussc {
namespace internal {

// Backend/ID conversion uses only the active union member, never padding.
ma_backend audioBackendToMA(AudioBackend backend);
AudioBackend audioBackendFromMA(ma_backend backend);
std::string audioDeviceUniqueId(ma_backend backend, const ma_device_id& id);
AudioDeviceInfo audioDeviceInfo(ma_backend backend, const ma_device_info& info);
bool findAudioDevice(ma_context* context, ma_device_type direction,
                     const AudioDeviceInfo* device, const std::string& name,
                     ma_device_id& selectedID);

// No selected ID means the system default was opened. Otherwise use the
// selected device's enumeration flag.
bool openedDeviceIsDefault(const ma_device_id* selectedID,
                           const ma_device_info* infos, ma_uint32 count);

} // namespace internal
} // namespace trussc
