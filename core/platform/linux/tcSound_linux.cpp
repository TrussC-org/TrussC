// =============================================================================
// tcSound_linux.cpp - AAC decoding using GStreamer
// =============================================================================
// Uses GStreamer for AAC decoding (system-level multimedia framework).
// This approach is similar to macOS (AudioToolbox) and Web (Web Audio API)
// in that it delegates codec handling to the OS/system.
// =============================================================================

#include "tc/sound/internal/tcAudioDecodeBudget.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/audio/audio.h>

#include <vector>
#include <cstring>

namespace trussc {

// Helper class for AAC decoding with GStreamer
class GstAacDecoder {
public:
    GstAacDecoder() {
        // Initialize GStreamer (safe to call multiple times)
        static std::once_flag initialized;
        std::call_once(initialized, [] { gst_init(nullptr, nullptr); });
    }

    ~GstAacDecoder() {
        cleanup();
    }

    bool decodeFile(const std::string& path, SoundBuffer& buffer) {
        internal::AudioDecodeBudget budget;
        std::vector<unsigned char> bytes;
        if (!internal::readAudioFile(internal::utf8ToPath(path), bytes, budget)) return false;
        return decodeMemoryInput(bytes.data(), bytes.size(), buffer, path);
    }

    bool decodeMemory(const void* data, size_t dataSize, SoundBuffer& buffer) {
        return decodeMemoryInput(data, dataSize, buffer, "memory");
    }
private:
    bool decodeMemoryInput(const void* data, size_t dataSize, SoundBuffer& buffer, const std::string& label) {
        internal::AudioDecodeBudget entry;
        if (!data || !dataSize || !entry.encoded(dataSize)) return false;
        // Store memory data for appsrc callback
        memoryData_ = static_cast<const uint8_t*>(data);
        memorySize_ = dataSize;
        memoryPos_ = 0;

        // Build pipeline: appsrc -> decodebin -> audioconvert -> audioresample -> appsink
        std::string pipelineStr =
            "appsrc name=src ! "
            "decodebin name=decoder ! "
            "audioconvert ! "
            "audioresample ! "
            "audio/x-raw,format=F32LE,rate=44100 ! "
            "appsink name=sink sync=false";

        return runPipeline(pipelineStr, buffer, label, true);
    }

    bool runPipeline(const std::string& pipelineStr, SoundBuffer& buffer,
                     const std::string& source, bool useAppsrc = false) {
        GError* error = nullptr;
        pipeline_ = gst_parse_launch(pipelineStr.c_str(), &error);

        if (error) {
            logError("SoundBuffer") << "GStreamer pipeline error: " << error->message;
            g_error_free(error);
            return false;
        }

        if (!pipeline_) {
            logError("SoundBuffer") << "failed to create the GStreamer pipeline";
            return false;
        }

        GstElement* decoder = gst_bin_get_by_name(GST_BIN(pipeline_), "decoder");
        if (decoder) {
            g_signal_connect(decoder, "autoplug-continue", G_CALLBACK(onAutoplug), this);
            g_signal_connect(decoder, "pad-added", G_CALLBACK(onPadAdded), this);
            gst_object_unref(decoder);
        }
        internal::AudioDecodeBudget budget;
        // Get appsink element
        GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
        if (!sink) {
            logError("SoundBuffer") << "failed to get the appsink element";
            cleanup();
            return false;
        }

        // Configure appsink
        gst_app_sink_set_emit_signals(GST_APP_SINK(sink), FALSE);
        gst_app_sink_set_drop(GST_APP_SINK(sink), FALSE);
        // Bound (security): queued SDK output is backpressured before collecting a full track. Overflow waits; format rejection rejects the whole sound and is counted.
        gst_app_sink_set_max_buffers(GST_APP_SINK(sink), 1);

        // Setup appsrc if decoding from memory
        if (useAppsrc) {
            GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
            if (src) {
                // Set stream type and size
                // Bound (security): a 64 KiB appsrc threshold limits queued external input references; complete random-access requests may cross it. Overflow waits for queue space; encoded-input rejection rejects the whole sound and is counted.
                g_object_set(src,
                    "max-bytes", guint64(65536), "block", TRUE,
                    "stream-type", GST_APP_STREAM_TYPE_RANDOM_ACCESS,
                    "size", (gint64)memorySize_,
                    nullptr);

                // Connect signals
                g_signal_connect(src, "need-data", G_CALLBACK(onNeedData), this);
                g_signal_connect(src, "seek-data", G_CALLBACK(onSeekData), this);

                gst_object_unref(src);
            }
        }

        // Start pipeline
        GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            logError("SoundBuffer") << "failed to start the GStreamer pipeline";
            gst_object_unref(sink);
            cleanup();
            return false;
        }

        // Collect decoded samples
        std::vector<float> allSamples;
        int channels = 0;
        GstBus* bus = gst_element_get_bus(pipeline_);
        bool decodeFailed = false;

        while (true) {
            GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 100 * GST_MSECOND);
            if (!sample) {
                // A pipeline error need not send EOS to appsink. Check the bus
                // after every empty try, including when appsink reports EOS.
                bool eos = gst_app_sink_is_eos(GST_APP_SINK(sink));
                GstMessage* msg = gst_bus_pop_filtered(
                    bus,
                    static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
                if (msg) {
                    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                        GError* err = nullptr;
                        gst_message_parse_error(msg, &err, nullptr);
                        logError("SoundBuffer") << "GStreamer error for " << source << ": " << err->message;
                        g_error_free(err);
                        decodeFailed = true;
                    }
                    // A bus EOS can race with the last samples reaching
                    // appsink; appsink reports EOS only once its queue is
                    // drained, so keep pulling until it does.
                    gst_message_unref(msg);
                }
                if (invalidFormat_.load()) {
                    budget.reject("AAC source format", 0, internal::audioChannelLimit);
                    decodeFailed = true;
                }
                if (decodeFailed || eos) break;
                continue;
            }

            GstCaps* caps = gst_sample_get_caps(sample);
            int sampleChannels = 0, sampleRate = 0;
            if (caps) {
                auto* structure = gst_caps_get_structure(caps, 0);
                gst_structure_get_int(structure, "channels", &sampleChannels);
                gst_structure_get_int(structure, "rate", &sampleRate);
            }
            if (invalidFormat_.load() || !budget.format(sampleChannels, sampleRate) ||
                (channels && channels != sampleChannels)) {
                budget.reject("AAC format change", sampleChannels, channels);
                decodeFailed = true;
            }
            channels = sampleChannels;
            gint64 duration = 0;
            if (!decodeFailed && gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &duration) && duration > 0) {
                const uint64_t frames = gst_util_uint64_scale_ceil(uint64_t(duration), 44100, GST_SECOND);
                if (!budget.pcm(frames, channels)) decodeFailed = true;
            }
            GstBuffer* gstBuffer = gst_sample_get_buffer(sample);
            if (!decodeFailed && gstBuffer) {
                const size_t bytes = gst_buffer_get_size(gstBuffer);
                if (bytes % (sizeof(float) * channels)) decodeFailed = true;
                else if (bytes > SIZE_MAX / 3) {
                    budget.reject("AAC sample bytes", bytes, SIZE_MAX / 3);
                    decodeFailed = true;
                } else if (!budget.admit(bytes * 3, allSamples.capacity() * sizeof(float))) {
                    decodeFailed = true;
                } else {
                    GstMapInfo map;
                    if (!gst_buffer_map(gstBuffer, &map, GST_MAP_READ)) decodeFailed = true;
                    else {
                        if (!internal::appendAudio(allSamples, reinterpret_cast<const float*>(map.data),
                                                   map.size / sizeof(float), budget, bytes * 3)) decodeFailed = true;
                        gst_buffer_unmap(gstBuffer, &map);
                    }
                }
            }
            gst_sample_unref(sample);
            if (decodeFailed) break;
        }

        gst_object_unref(bus);
        gst_object_unref(sink);
        cleanup();

        if (decodeFailed || inputFailed_.load()) return false;

        if (allSamples.empty()) {
            logError("SoundBuffer") << "no audio samples decoded";
            return false;
        }

        // Fill SoundBuffer
        buffer.samples = std::move(allSamples);
        buffer.channels = channels > 0 ? channels : 2;
        buffer.sampleRate = 44100;
        buffer.numSamples = buffer.samples.size() / buffer.channels;

        logVerbose("SoundBuffer") << "loaded AAC (" << buffer.channels << " ch, "
                                  << buffer.sampleRate << " Hz, " << buffer.numSamples
                                  << " samples)";

        return true;
    }

    void cleanup() {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
        }
    }

    // Callback: GStreamer needs data from memory buffer
    static void onNeedData(GstElement* src, guint length, gpointer userData) {
        GstAacDecoder* self = static_cast<GstAacDecoder*>(userData);

        if (self->memoryPos_ >= self->memorySize_) {
            // End of data
            gst_app_src_end_of_stream(GST_APP_SRC(src));
            return;
        }

        // Calculate how much data to send
        size_t remaining = self->memorySize_ - self->memoryPos_;
        size_t toSend = (length > 0 && length < remaining) ? length : remaining;

        // Random-access appsrc must serve the complete request. Wrap the admitted
        // input without copying; it stays alive until cleanup stops the pipeline.
        GstBuffer* buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
            const_cast<uint8_t*>(self->memoryData_), self->memorySize_,
            self->memoryPos_, toSend, nullptr, nullptr);
        if (!buffer) { gst_app_src_end_of_stream(GST_APP_SRC(src)); self->inputFailed_.store(true); return; }
        self->memoryPos_ += toSend;

        // Push buffer
        if (gst_app_src_push_buffer(GST_APP_SRC(src), buffer) != GST_FLOW_OK) {
            self->inputFailed_.store(true);
            gst_app_src_end_of_stream(GST_APP_SRC(src));
            return;
        }

        // Signal end of stream if we've sent all data
        if (self->memoryPos_ >= self->memorySize_) {
            gst_app_src_end_of_stream(GST_APP_SRC(src));
        }
    }

    // Callback: GStreamer wants to seek in memory buffer
    static gboolean onSeekData(GstElement* src, guint64 offset, gpointer userData) {
        GstAacDecoder* self = static_cast<GstAacDecoder*>(userData);

        if (offset < self->memorySize_) {
            self->memoryPos_ = offset;
            return TRUE;
        }
        return FALSE;
    }

    void checkRawCaps(GstCaps* caps) {
        if (!caps || !gst_caps_get_size(caps)) return;
        auto* st = gst_caps_get_structure(caps, 0);
        if (!gst_structure_has_name(st, "audio/x-raw")) return;
        int channels = 0, rate = 0;
        if (!gst_structure_get_int(st, "channels", &channels) || !gst_structure_get_int(st, "rate", &rate)) return;
        const int previousChannels = rawChannels_.exchange(channels);
        const int previousRate = rawRate_.exchange(rate);
        if (channels < 1 || channels > int(internal::audioChannelLimit) || rate < 1 || rate > int(internal::audioRateLimit) ||
            (previousChannels && previousChannels != channels) || (previousRate && previousRate != rate)) invalidFormat_.store(true);
    }
    static GstPadProbeReturn onCapsEvent(GstPad* pad, GstPadProbeInfo* info, gpointer data) {
        if (!gst_pad_is_linked(pad)) return GST_PAD_PROBE_OK;
        GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
        if (event && GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps* caps = nullptr;
            gst_event_parse_caps(event, &caps);
            static_cast<GstAacDecoder*>(data)->checkRawCaps(caps);
        }
        return GST_PAD_PROBE_OK;
    }
    static void onPadAdded(GstElement*, GstPad* pad, gpointer data) {
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, onCapsEvent, data, nullptr);
        GstCaps* caps = gst_pad_get_current_caps(pad);
        if (gst_pad_is_linked(pad)) static_cast<GstAacDecoder*>(data)->checkRawCaps(caps);
        if (caps) gst_caps_unref(caps);
    }
    // Compressed metadata is available before decodebin instantiates its decoder.
    static gboolean onAutoplug(GstElement*, GstPad*, GstCaps* caps, gpointer data) {
        auto& self = *static_cast<GstAacDecoder*>(data);
        int channels = 0, rate = 0;
        auto* st = gst_caps_get_structure(caps, 0);
        const bool badChannels = gst_structure_get_int(st, "channels", &channels) &&
            (channels < 1 || channels > int(internal::audioChannelLimit));
        const bool badRate = gst_structure_get_int(st, "rate", &rate) &&
            (rate < 1 || rate > int(internal::audioRateLimit));
        if (badChannels || badRate) { self.invalidFormat_.store(true); return FALSE; }
        return TRUE;
    }
    std::atomic<bool> invalidFormat_{false}, inputFailed_{false};
    std::atomic<int> rawChannels_{0}, rawRate_{0};
    GstElement* pipeline_ = nullptr;

    // Memory buffer for appsrc
    const uint8_t* memoryData_ = nullptr;
    size_t memorySize_ = 0;
    size_t memoryPos_ = 0;
};

LoadResult SoundBuffer::loadAacPlatform(const fs::path& path) {
    GstAacDecoder decoder;
    return decoder.decodeFile(internal::pathToUtf8(path), *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed,
                                  "GStreamer failed to decode AAC: " +
                                  internal::pathToDisplayUtf8(path));
}

LoadResult SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
    GstAacDecoder decoder;
    return decoder.decodeMemory(data, dataSize, *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed,
                                  "GStreamer failed to decode AAC from memory");
}

} // namespace trussc
