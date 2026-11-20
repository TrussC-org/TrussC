#include <TrussC.h>
#include "tc/sound/tcAudioDeviceInternal.h"
#include "../../common/tcCoreTest.h"
#include <cstdio>

using namespace tc;

TC_CORE_TEST_MAIN() {
    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    };
    const auto defaults = ma_device_config_init(ma_device_type_playback);
    for (auto profile : {AudioPerformanceProfile::LowLatency, AudioPerformanceProfile::Conservative}) {
        for (auto content : {AudioContentType::Default, AudioContentType::Music}) {
            AudioSettings settings;
            settings.performanceProfile = profile;
            settings.contentType = content;
            auto config = defaults;
            config.periodSizeInFrames = 512;
            config.aaudio.usage = ma_aaudio_usage_media;
            internal::applyAudioOutputPolicy(config, settings);
            check("performance profile maps independently", config.performanceProfile ==
                (profile == AudioPerformanceProfile::Conservative
                    ? ma_performance_profile_conservative : ma_performance_profile_low_latency));
            check("content type maps independently", config.aaudio.contentType ==
                (content == AudioContentType::Music ? ma_aaudio_content_type_music : ma_aaudio_content_type_default));
            check("explicit period and usage preserved", config.periodSizeInFrames == 512
                && config.aaudio.usage == ma_aaudio_usage_media);
        }
    }
    for (auto type : {ma_device_notification_type_started, ma_device_notification_type_stopped,
                      ma_device_notification_type_rerouted, ma_device_notification_type_interruption_began,
                      ma_device_notification_type_interruption_ended, ma_device_notification_type_unlocked}) {
        for (auto profile : {AudioPerformanceProfile::LowLatency, AudioPerformanceProfile::Conservative}) {
            for (bool openedDefault : {false, true}) {
                check("reroute reapply requires rerouted, Conservative, and default output",
                    internal::shouldReapplyProfileAfterReroute(type, profile, openedDefault) ==
                        (type == ma_device_notification_type_rerouted
                            && profile == AudioPerformanceProfile::Conservative && openedDefault));
            }
        }
    }
    auto config = defaults;
    internal::applyAudioOutputPolicy(config, AudioSettings{});
    check("defaults match miniaudio", config.performanceProfile == defaults.performanceProfile
        && config.aaudio.contentType == defaults.aaudio.contentType
        && config.aaudio.usage == defaults.aaudio.usage);
    return failures ? 1 : 0;
}
