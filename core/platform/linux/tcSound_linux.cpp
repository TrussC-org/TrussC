// =============================================================================
// tcSound_linux.cpp - AAC decoding using GStreamer
// =============================================================================
// Uses GStreamer for AAC decoding (system-level multimedia framework).
// This approach is similar to macOS (AudioToolbox) and Web (Web Audio API)
// in that it delegates codec handling to the OS/system.
// =============================================================================

#include "tc/sound/tcSound.h"

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
        static bool initialized = false;
        if (!initialized) {
            gst_init(nullptr, nullptr);
            initialized = true;
        }
    }

    ~GstAacDecoder() {
        cleanup();
    }

    bool decodeFile(const std::string& path, SoundBuffer& buffer) {
        // Build pipeline: filesrc -> decodebin -> audioconvert -> audioresample -> appsink
        std::string pipelineStr =
            "filesrc location=\"" + path + "\" ! "
            "decodebin ! "
            "audioconvert ! "
            "audioresample ! "
            "audio/x-raw,format=F32LE,rate=44100 ! "
            "appsink name=sink sync=false";

        return runPipeline(pipelineStr, buffer);
    }

    bool decodeMemory(const void* data, size_t dataSize, SoundBuffer& buffer) {
        // Store memory data for appsrc callback
        memoryData_ = static_cast<const uint8_t*>(data);
        memorySize_ = dataSize;
        memoryPos_ = 0;

        // Build pipeline: appsrc -> decodebin -> audioconvert -> audioresample -> appsink
        std::string pipelineStr =
            "appsrc name=src ! "
            "decodebin ! "
            "audioconvert ! "
            "audioresample ! "
            "audio/x-raw,format=F32LE,rate=44100 ! "
            "appsink name=sink sync=false";

        return runPipeline(pipelineStr, buffer, true);
    }

private:
    bool runPipeline(const std::string& pipelineStr, SoundBuffer& buffer, bool useAppsrc = false) {
        GError* error = nullptr;
        pipeline_ = gst_parse_launch(pipelineStr.c_str(), &error);

        if (error) {
            printf("SoundBuffer: GStreamer pipeline error: %s\n", error->message);
            g_error_free(error);
            return false;
        }

        if (!pipeline_) {
            printf("SoundBuffer: Failed to create GStreamer pipeline\n");
            return false;
        }

        // Get appsink element
        GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
        if (!sink) {
            printf("SoundBuffer: Failed to get appsink element\n");
            cleanup();
            return false;
        }

        // Configure appsink
        gst_app_sink_set_emit_signals(GST_APP_SINK(sink), FALSE);
        gst_app_sink_set_drop(GST_APP_SINK(sink), FALSE);

        // Setup appsrc if decoding from memory
        if (useAppsrc) {
            GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
            if (src) {
                // Set stream type and size
                g_object_set(src,
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
            printf("SoundBuffer: Failed to start GStreamer pipeline\n");
            gst_object_unref(sink);
            cleanup();
            return false;
        }

        // Collect decoded samples
        std::vector<float> allSamples;
        int channels = 0;

        while (true) {
            GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
            if (!sample) {
                // Check if EOS or error
                if (gst_app_sink_is_eos(GST_APP_SINK(sink))) {
                    break;  // End of stream
                }
                // Check for errors
                GstMessage* msg = gst_bus_pop_filtered(
                    gst_element_get_bus(pipeline_),
                    static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
                if (msg) {
                    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                        GError* err = nullptr;
                        gst_message_parse_error(msg, &err, nullptr);
                        printf("SoundBuffer: GStreamer error: %s\n", err->message);
                        g_error_free(err);
                    }
                    gst_message_unref(msg);
                }
                break;
            }

            // Get buffer info
            GstCaps* caps = gst_sample_get_caps(sample);
            if (caps && channels == 0) {
                GstStructure* structure = gst_caps_get_structure(caps, 0);
                gst_structure_get_int(structure, "channels", &channels);
            }

            GstBuffer* gstBuffer = gst_sample_get_buffer(sample);
            if (gstBuffer) {
                GstMapInfo map;
                if (gst_buffer_map(gstBuffer, &map, GST_MAP_READ)) {
                    // Append float samples
                    size_t numFloats = map.size / sizeof(float);
                    size_t oldSize = allSamples.size();
                    allSamples.resize(oldSize + numFloats);
                    memcpy(allSamples.data() + oldSize, map.data, map.size);

                    gst_buffer_unmap(gstBuffer, &map);
                }
            }

            gst_sample_unref(sample);
        }

        gst_object_unref(sink);
        cleanup();

        if (allSamples.empty()) {
            printf("SoundBuffer: No audio samples decoded\n");
            return false;
        }

        // Fill SoundBuffer
        buffer.samples = std::move(allSamples);
        buffer.channels = channels > 0 ? channels : 2;
        buffer.sampleRate = 44100;
        buffer.numSamples = buffer.samples.size() / buffer.channels;

        printf("SoundBuffer: Loaded AAC (%d ch, %d Hz, %zu samples)\n",
               buffer.channels, buffer.sampleRate, buffer.numSamples);

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

        // Create buffer and copy data
        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, toSend, nullptr);
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
            memcpy(map.data, self->memoryData_ + self->memoryPos_, toSend);
            gst_buffer_unmap(buffer, &map);
        }

        self->memoryPos_ += toSend;

        // Push buffer
        gst_app_src_push_buffer(GST_APP_SRC(src), buffer);

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

    GstElement* pipeline_ = nullptr;

    // Memory buffer for appsrc
    const uint8_t* memoryData_ = nullptr;
    size_t memorySize_ = 0;
    size_t memoryPos_ = 0;
};

bool SoundBuffer::loadAac(const std::string& path) {
    GstAacDecoder decoder;
    return decoder.decodeFile(path, *this);
}

bool SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    GstAacDecoder decoder;
    return decoder.decodeMemory(data, dataSize, *this);
}

} // namespace trussc
