// =============================================================================
// tcVideoRecorder_linux.cpp - Linux GStreamer (appsrc -> enc -> mp4) impl
// =============================================================================
//
// Mirrors the macOS AVFoundation / Windows Media Foundation backends: it
// implements the three platform hooks declared in tc/video/tcVideoRecorder.h
//   - openPlatform()   : build an appsrc -> encoder -> mp4mux pipeline
//   - appendPlatform() : push one RGBA8 (top-down) frame
//   - closePlatform()  : end-of-stream, flush and finalize the .mp4
//
// GStreamer is the desktop-native multimedia framework on Linux and is already
// a TrussC dependency (Sound/VideoPlayer), so this adds no new library - the
// same "use the platform's native media stack, no ffmpeg" spirit as the other
// backends.
//
// Codecs: H.264 and HEVC (H.265). ProRes is AVFoundation-only and rejected
// here. For each, the encoder is chosen at runtime: hardware first (NVENC, then
// modern VA / legacy VA-API, V4L2 for the Pi), falling back to software x264enc/
// x265enc. Each candidate is validated with a tiny probe pipeline before use -
// a HW encoder that exists but can't actually run on this machine is skipped
// rather than silently producing an empty file - so we get the efficient path
// automatically and still never hard-fail where software is available.
//
// The recorder feeds RGBA top-down, which is exactly GStreamer's default raw
// video layout, so no vertical flip or R<->B swizzle is needed here (unlike the
// mac/win backends) - videoconvert handles RGBA -> I420 for the encoder.
// =============================================================================

#if defined(__linux__)

#include "TrussC.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>

namespace trussc {

// ---------------------------------------------------------------------------
// Platform state (the pipeline owns the encoder + mp4 muxer + file sink)
// ---------------------------------------------------------------------------
namespace internal {
struct VideoWriterPlatformData {
    GstElement* pipeline = nullptr;
    GstElement* appsrc   = nullptr;  // video source (we hold a ref)
    GstElement* audiosrc = nullptr;  // audio source (we hold a ref); null = video-only
    int    width  = 0;
    int    height = 0;
    double fps    = 30.0;
    bool   hasAudio = false;
    int    audioRate = 0;
    int    audioChannels = 0;
    std::atomic<bool> failed{false};
    std::mutex syncHandlerMutex;
    std::condition_variable syncHandlerDone;
    bool syncHandlerRemoved = false;
};
}  // namespace internal
using internal::VideoWriterPlatformData;

namespace {

void flushRecordingSource(GstElement* src, gpointer) {
    gst_element_send_event(src, gst_event_new_flush_start());
}

// This runs on the posting (usually streaming) thread, including while the
// caller is blocked in push_buffer(). Flush only once; leave error text on the
// bus for the caller, which alone owns the SaveResult and logs the failure.
GstBusSyncReply recordingBusSync(GstBus*, GstMessage* message, gpointer data) {
    auto* pd = static_cast<VideoWriterPlatformData*>(data);
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR && !pd->failed.exchange(true)) {
        // Sending downstream events here can re-enter the failing sink while
        // it holds its streaming locks. GStreamer keeps each source alive for
        // this asynchronous call; the callback never accesses pd.
        gst_element_call_async(pd->appsrc, flushRecordingSource, nullptr, nullptr);
        if (pd->audiosrc)
            gst_element_call_async(pd->audiosrc, flushRecordingSource, nullptr, nullptr);
    }
    return GST_BUS_PASS;
}

// One H.264 encoder option. `segment` is the pipeline fragment that sits
// between "videoconvert ! " and " ! h264parse" and must contain the encoder
// element named "enc". HW encoders negotiate their own (4:2:0) chroma and may
// need an uploader (vaapipostproc); software encoders get an explicit I420
// capsfilter so x264enc doesn't pick the poorly-supported high-4:4:4 profile.
struct EncoderOption {
    const char* probe;       // element that must exist for this option
    const char* encName;     // the "enc" element (for logging / bitrate)
    const char* segment;     // videoconvert -> ... -> h264parse fragment
    bool        hardware;
    bool        bitrateKbps; // true: "bitrate" in kbit/s, false: bits/s
};

// Hardware first (auto-prefer the efficient path), software last as the
// always-works safety net. Every option is validated with a real probe before
// use, so an encoder that exists but can't actually run on this machine just
// falls through to the next instead of producing an empty file.
const EncoderOption kH264Options[] = {
    // NVIDIA NVENC first: a dedicated encode ASIC, the gold standard for HW
    // H.264. Takes system memory directly (nvcodec uploads internally). On a
    // hybrid Intel+NVIDIA box this prefers the discrete GPU's encoder.
    {"nvh264enc",    "nvh264enc",    "nvh264enc name=enc",                              true,  true},
    // Modern VA-API (Intel/AMD): takes system memory, picks 4:2:0 itself.
    {"vah264lpenc",  "vah264lpenc",  "vah264lpenc name=enc",                            true,  true},
    {"vah264enc",    "vah264enc",    "vah264enc name=enc",                              true,  true},
    // Legacy gstreamer-vaapi: needs frames uploaded to VA surfaces first.
    {"vaapih264enc", "vaapih264enc", "vaapipostproc ! vaapih264enc name=enc",           true,  true},
    // V4L2 stateful HW encoder (Raspberry Pi 3/4 etc.): wants NV12.
    {"v4l2h264enc",  "v4l2h264enc",  "video/x-raw,format=NV12 ! v4l2h264enc name=enc",  true,  true},
    // Software (gst-plugins-ugly): the portable default, forced to 4:2:0.
    {"x264enc",      "x264enc",      "video/x-raw,format=I420 ! x264enc name=enc speed-preset=medium", false, true},
    // Software fallback (gst-plugins-bad). bitrate here is in bits/sec.
    {"openh264enc",  "openh264enc",  "video/x-raw,format=I420 ! openh264enc name=enc",  false, false},
};

// HEVC / H.265. Same hardware-first ordering; no openh264 equivalent, so the
// software fallback is x265enc (gst-plugins-bad). All emit 4:2:0 Main profile.
const EncoderOption kHevcOptions[] = {
    {"nvh265enc",    "nvh265enc",    "nvh265enc name=enc",                              true,  true},
    {"vah265lpenc",  "vah265lpenc",  "vah265lpenc name=enc",                            true,  true},
    {"vah265enc",    "vah265enc",    "vah265enc name=enc",                              true,  true},
    {"vaapih265enc", "vaapih265enc", "vaapipostproc ! vaapih265enc name=enc",           true,  true},
    {"x265enc",      "x265enc",      "video/x-raw,format=I420 ! x265enc name=enc speed-preset=medium", false, true},
};

// Per-codec encoding plan: which candidate table to try and which H.26x parser
// to put before mp4mux. Returns false for codecs this backend can't do (ProRes
// is AVFoundation-only on macOS), so the caller rejects them cleanly.
struct CodecPlan {
    const EncoderOption* options;
    int                  count;
    const char*          parse;  // h264parse / h265parse
    const char*          label;  // for logs / errors
};

bool codecPlan(VideoCodec codec, CodecPlan& out) {
    switch (codec) {
        case VideoCodec::H264:
            out = {kH264Options, (int)(sizeof(kH264Options) / sizeof(kH264Options[0])),
                   "h264parse", "H.264"};
            return true;
        case VideoCodec::HEVC:
            out = {kHevcOptions, (int)(sizeof(kHevcOptions) / sizeof(kHevcOptions[0])),
                   "h265parse", "HEVC"};
            return true;
        default:
            return false;  // ProRes422 / ProRes4444 — macOS only
    }
}

bool elementAvailable(const char* name) {
    GstElementFactory* f = gst_element_factory_find(name);
    if (f) { gst_object_unref(f); return true; }
    return false;
}

// Run a tiny self-contained pipeline to confirm the encoder actually works on
// this machine (driver present, caps negotiable). HW elements routinely exist
// yet fail to initialize; this reaches EOS only if encoding truly succeeds.
//
// Probed at the real recording resolution: NVENC (and some HW encoders) reject
// frames below a minimum size, so a fixed tiny probe would falsely reject a
// perfectly good encoder. Even dimensions only (H.264 needs them).
bool probeEncoder(const EncoderOption& opt, int w, int h) {
    int pw = (w < 16 ? 16 : (w & ~1));
    int ph = (h < 16 ? 16 : (h & ~1));
    std::string desc =
        "videotestsrc num-buffers=2 ! video/x-raw,format=RGBA,width=" +
        std::to_string(pw) + ",height=" + std::to_string(ph) +
        ",framerate=30/1 ! videoconvert ! ";
    desc += opt.segment;
    desc += " ! fakesink sync=false";

    GError* err = nullptr;
    GstElement* p = gst_parse_launch(desc.c_str(), &err);
    if (!p || err) {
        if (err) g_error_free(err);
        if (p) gst_object_unref(p);
        return false;
    }
    bool ok = false;
    if (gst_element_set_state(p, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        GstBus* bus = gst_element_get_bus(p);
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus, 5 * GST_SECOND,
            (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        ok = (msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
        if (msg) gst_message_unref(msg);
        gst_object_unref(bus);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return ok;
}

// Pick the best working encoder for this codec + resolution: the first hardware
// option that probes OK, otherwise the first working software option. Cached
// per (table, w, h) so repeated recordings at the same settings don't re-probe,
// but a codec or size change re-validates (min-size support can differ).
const EncoderOption* selectEncoder(const CodecPlan& plan, int w, int h) {
    static const EncoderOption* chosen = nullptr;
    static const EncoderOption* cachedTable = nullptr;
    static int cachedW = 0, cachedH = 0;
    if (chosen && cachedTable == plan.options && w == cachedW && h == cachedH)
        return chosen;
    chosen = nullptr;
    cachedTable = plan.options;
    cachedW = w;
    cachedH = h;
    for (int i = 0; i < plan.count; ++i) {
        const EncoderOption& opt = plan.options[i];
        if (!elementAvailable(opt.probe)) continue;
        if (probeEncoder(opt, w, h)) {
            chosen = &opt;
            logNotice("VideoWriter")
                << "selected " << (opt.hardware ? "hardware" : "software")
                << " " << plan.label << " encoder: " << opt.encName;
            break;
        }
        logWarning("VideoWriter")
            << opt.encName << " is installed but not functional here; "
            << "trying next encoder";
    }
    return chosen;
}

// Split a float fps into an exact rational (num/den) for caps framerate.
void fpsToRatio(double fps, int& num, int& den) {
    if (fps <= 0.0) fps = 30.0;
    double rounded = std::floor(fps + 0.5);
    if (std::fabs(fps - rounded) < 1e-4) {
        num = (int)rounded;
        den = 1;
    } else {
        num = (int)std::floor(fps * 1000.0 + 0.5);
        den = 1000;
    }
}

// Configure the encoder's bitrate. The unit differs per encoder (kbit/s vs
// bits/s, captured in EncoderOption). Some HW encoders default to a constant-
// quality mode that ignores "bitrate"; we set it best-effort where the
// property exists rather than juggle each encoder's rate-control enum.
void configureEncoderBitrate(GstElement* enc, const EncoderOption& opt,
                             int bitsPerSec) {
    if (!enc || bitsPerSec <= 0) return;
    if (!g_object_class_find_property(G_OBJECT_GET_CLASS(enc), "bitrate")) return;
    guint val = opt.bitrateKbps ? (guint)((bitsPerSec + 999) / 1000)
                                : (guint)bitsPerSec;
    g_object_set(enc, "bitrate", val, nullptr);
}

// Best-effort keyframe interval (frames between IDRs). Property name differs:
// x264/x265/nvenc use "key-int-max", VA-API encoders use "keyframe-period".
void configureKeyframeInterval(GstElement* enc, int frames) {
    if (!enc || frames <= 0) return;
    GObjectClass* klass = G_OBJECT_GET_CLASS(enc);
    if (g_object_class_find_property(klass, "key-int-max")) {
        g_object_set(enc, "key-int-max", (guint)frames, nullptr);
    } else if (g_object_class_find_property(klass, "keyframe-period")) {
        g_object_set(enc, "keyframe-period", (guint)frames, nullptr);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// openPlatform - build and start the encoding pipeline
// ---------------------------------------------------------------------------
bool VideoWriter::openPlatform(const std::string& fullPath, int w, int h,
                               float fps, const VideoRecordSettings& settings) {
    // Resolve the codec to an encoder plan. H.264 and HEVC are supported via
    // GStreamer; ProRes is AVFoundation-only (macOS), so reject it clearly
    // rather than silently writing a different format than requested.
    CodecPlan plan;
    if (!codecPlan(settings.codec, plan)) {
        result_ = SaveResult::fail(SaveError::WriteFailed, std::string("codec ") +
            videoCodecName(settings.codec) + " is not available on Linux (macOS only); use H.264 or HEVC");
        return false;
    }

    // Safe to call multiple times; matches the Sound backend's guard.
    static bool gstInitialized = false;
    if (!gstInitialized) {
        gst_init(nullptr, nullptr);
        gstInitialized = true;
    }

    // Audio track: pick the first AAC encoder available on this system. All of
    // these are software encoders that initialize reliably when installed, so
    // (unlike the video encoders) a presence check is enough — no probe
    // pipeline needed. Missing encoder or unusable format degrades to
    // video-only with a warning, mirroring the mac/win backends.
    const char* aacEnc = nullptr;
    if (settings.audio) {
        if (settings.audioSampleRate > 0 && settings.audioChannels > 0) {
            static const char* kAacEncoders[] = {
                "fdkaacenc",   // gst-plugins-bad + libfdk-aac: best quality
                "voaacenc",    // gst-plugins-bad + vo-aacenc
                "avenc_aac",   // gst-libav: broadly installed
                "faac",        // gst-plugins-bad + libfaac
            };
            for (const char* cand : kAacEncoders) {
                if (elementAvailable(cand)) { aacEnc = cand; break; }
            }
            if (!aacEnc) {
                logWarning("VideoWriter")
                    << "no GStreamer AAC encoder found (install gst-libav or "
                       "gst-plugins-bad) - recording video only";
            }
        } else {
            logWarning("VideoWriter")
                << "audio requested but sample rate / channel count are unset "
                   "- recording video only";
        }
    }

    // Auto-select the most efficient encoder that actually works here: the
    // first probed-OK hardware option, else software. Resolved once, cached.
    const EncoderOption* opt = selectEncoder(plan, w, h);
    if (!opt) {
        result_ = SaveResult::fail(SaveError::WriteFailed, std::string("no working ") + plan.label +
            " GStreamer encoder found (check gst-plugins-ugly/bad and your GPU's VA-API/NVENC plugins)");
        return false;
    }

    // The H.26x parser makes the encoder->mp4mux hand-off robust (avc/hvc1
    // stream-format, AU alignment). Include it only when the plugin is present.
    bool haveParse = elementAvailable(plan.parse);

    // appsrc -> queue -> videoconvert -> <encoder segment> -> [parse] -> mp4mux -> filesink
    // Named elements let us configure caps/bitrate/location after parse, which
    // avoids fragile shell-style escaping of the output path. The encoder
    // segment (HW uploader / chroma capsfilter) is baked into EncoderOption.
    std::string desc = "appsrc name=src ! queue ! videoconvert ! ";
    desc += opt->segment;
    if (haveParse) { desc += " ! "; desc += plan.parse; }
    // The queue between encoder and muxer is load-bearing when audio is muxed:
    // mp4mux blocks video consumption for its interleave window (~250ms) while
    // waiting for audio, audio only arrives from the main thread, and the main
    // thread is blocked pushing video into an appsrc that holds barely one raw
    // frame -> permanent deadlock. A post-encode queue absorbs the window in
    // the cheap compressed domain and breaks the cycle.
    desc += " ! queue ! mp4mux name=mux ! filesink name=sink";
    // Audio branch: a second appsrc feeding interleaved float samples through
    // audioconvert into the AAC encoder, muxed into the same mp4.
    if (aacEnc) {
        desc += " appsrc name=asrc ! queue ! audioconvert ! ";
        desc += aacEnc;
        desc += " name=aenc";
        if (elementAvailable("aacparse")) desc += " ! aacparse";
        desc += " ! mux.";
    }

    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &err);
    if (!pipeline || err) {
        result_ = SaveResult::fail(SaveError::WriteFailed, std::string("failed to build pipeline: ") +
            (err ? err->message : "unknown") + " [" + desc + "]");
        if (err) g_error_free(err);
        if (pipeline) gst_object_unref(pipeline);
        return false;
    }

    GstElement* appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    GstElement* enc    = gst_bin_get_by_name(GST_BIN(pipeline), "enc");
    GstElement* sink   = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!appsrc || !sink) {
        result_ = SaveResult::fail(SaveError::WriteFailed, "pipeline missing src/sink");
        if (appsrc) gst_object_unref(appsrc);
        if (enc) gst_object_unref(enc);
        if (sink) gst_object_unref(sink);
        gst_object_unref(pipeline);
        return false;
    }

    // Output file.
    g_object_set(sink, "location", fullPath.c_str(), nullptr);

    // Encoder bitrate: ~0.1 bits per pixel per second is a good clean-demo
    // default, matching the mac/win backends.
    int br = (settings.bitrate > 0)
                 ? settings.bitrate
                 : (int)((double)w * h * (fps > 0 ? fps : 30.0) * 0.1);
    configureEncoderBitrate(enc, *opt, br);
    configureKeyframeInterval(enc, settings.keyframeInterval);
    if (enc) gst_object_unref(enc);

    // appsrc: time-stamped (we set PTS ourselves), pushed synchronously with
    // backpressure so a slow encoder throttles capture instead of ballooning
    // memory. RGBA top-down matches the recorder's frame layout exactly.
    int fnum = 0, fden = 1;
    fpsToRatio((fps > 0) ? fps : 30.0, fnum, fden);
    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "format",    G_TYPE_STRING, "RGBA",
        "width",     G_TYPE_INT, w,
        "height",    G_TYPE_INT, h,
        "framerate", GST_TYPE_FRACTION, fnum, fden,
        nullptr);
    gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);
    gst_caps_unref(caps);

    g_object_set(appsrc,
                 "format",       GST_FORMAT_TIME,
                 "is-live",      FALSE,
                 "do-timestamp", FALSE,
                 "block",        TRUE,
                 nullptr);

    // Audio appsrc: interleaved float32 at the engine's rate/channels;
    // audioconvert adapts to whatever the AAC encoder negotiates.
    GstElement* audiosrc = nullptr;
    if (aacEnc) {
        audiosrc = gst_bin_get_by_name(GST_BIN(pipeline), "asrc");
        if (audiosrc) {
            GstCaps* acaps = gst_caps_new_simple(
                "audio/x-raw",
                "format",   G_TYPE_STRING, "F32LE",
                "layout",   G_TYPE_STRING, "interleaved",
                "rate",     G_TYPE_INT, settings.audioSampleRate,
                "channels", G_TYPE_INT, settings.audioChannels,
                nullptr);
            gst_app_src_set_caps(GST_APP_SRC(audiosrc), acaps);
            gst_caps_unref(acaps);
            g_object_set(audiosrc,
                         "format",       GST_FORMAT_TIME,
                         "is-live",      FALSE,
                         "do-timestamp", FALSE,
                         "block",        TRUE,
                         nullptr);
        }
        // AAC bitrate (bits/sec on all candidate encoders), best-effort.
        GstElement* aenc = gst_bin_get_by_name(GST_BIN(pipeline), "aenc");
        if (aenc) {
            if (settings.audioBitrate > 0 &&
                g_object_class_find_property(G_OBJECT_GET_CLASS(aenc),
                                             "bitrate")) {
                g_object_set(aenc, "bitrate", (gint)settings.audioBitrate,
                             nullptr);
            }
            gst_object_unref(aenc);
        }
    }

    const auto state = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(pipeline);
    GstMessage* openError = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    gst_object_unref(bus);
    if (state == GST_STATE_CHANGE_FAILURE || openError) {
        result_ = SaveResult::fail(SaveError::WriteFailed,
            std::string("could not start pipeline (encoder: ") + opt->encName + ")");
        if (openError) {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(openError, &error, &debug);
            if (error) result_.message = error->message;
            // filesink's OPEN_WRITE carries GST_ERROR_SYSTEM in the debug text,
            // not a numeric errno field. Match its localized g_strerror suffix;
            // do not use this thread's errno after the state transition.
            if (error && error->domain == GST_RESOURCE_ERROR &&
                error->code == GST_RESOURCE_ERROR_OPEN_WRITE && debug) {
                for (int code : {EACCES, EPERM, EROFS, ENOSPC}) {
                    const std::string reason = std::string("system error: ") + g_strerror(code);
                    if (g_str_has_suffix(debug, reason.c_str())) {
                        result_.error = code == ENOSPC ? SaveError::NoSpace : SaveError::AccessDenied;
                        result_.message += " (" + std::string(g_strerror(code)) + ")";
                        break;
                    }
                }
            }
            if (error) g_error_free(error);
            g_free(debug);
            gst_message_unref(openError);
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(appsrc);
        if (audiosrc) gst_object_unref(audiosrc);
        gst_object_unref(sink);
        gst_object_unref(pipeline);
        return false;
    }
    gst_object_unref(sink);

    auto* pd = new VideoWriterPlatformData();
    pd->pipeline = pipeline;
    pd->appsrc   = appsrc;  // keep our ref; released in closePlatform()
    pd->audiosrc = audiosrc;
    pd->width    = w;
    pd->height   = h;
    pd->fps      = (fps > 0) ? fps : 30.0;
    if (audiosrc) {
        pd->hasAudio      = true;
        pd->audioRate     = settings.audioSampleRate;
        pd->audioChannels = settings.audioChannels;
    }
    platform_ = pd;

    bus = gst_element_get_bus(pipeline);
    gst_bus_set_sync_handler(bus, recordingBusSync, pd, [](gpointer data) {
        auto* pd = static_cast<VideoWriterPlatformData*>(data);
        // GStreamer releases the handler data after in-flight callbacks exit.
        std::lock_guard<std::mutex> lock(pd->syncHandlerMutex);
        pd->syncHandlerRemoved = true;
        pd->syncHandlerDone.notify_one();
    });
    gst_object_unref(bus);

    logNotice("VideoWriter")
        << "GStreamer " << plan.label << " encoder: " << opt->encName
        << (opt->hardware ? " (hardware)" : " (software)")
        << (pd->hasAudio ? std::string(" + AAC audio (") + aacEnc + ")"
                         : std::string());
    return true;
}

// ---------------------------------------------------------------------------
// appendAudioPlatform - push interleaved float samples into the audio branch
// ---------------------------------------------------------------------------
// Downstream file/encoder failures can arrive after appsrc accepted a buffer.
// Healthy appends poll without waiting. After the sync handler wakes a blocked
// push, its ERROR may not be queued yet: wait for that pending message, using
// the same bound as finalization, so the caller retains the pipeline's reason.
static bool pollRecordingError(VideoWriterPlatformData* pd, SaveResult& result) {
    if (!result) return false;  // the first failure has already been retained
    GstBus* bus = gst_element_get_bus(pd->pipeline);
    GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (!msg && !pd->failed) {
        gst_object_unref(bus);
        return true;
    }
    if (!msg) msg = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND, GST_MESSAGE_ERROR);
    gst_object_unref(bus);
    if (!msg) {
        result = SaveResult::fail(SaveError::WriteFailed, "GStreamer pipeline error");
        return false;
    }
    GError* error = nullptr;
    gst_message_parse_error(msg, &error, nullptr);
    result = SaveResult::fail(SaveError::WriteFailed, error ? error->message : "GStreamer pipeline error");
    if (error) g_error_free(error);
    gst_message_unref(msg);
    pd->failed = true;
    return false;
}

bool VideoWriter::appendAudioPlatform(const float* interleaved, int frames,
                                      double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->audiosrc || !pd->hasAudio) return false;
    if (!interleaved || frames <= 0) return false;

    if (!pollRecordingError(pd, result_)) return false;
    const gsize size = (gsize)frames * pd->audioChannels * sizeof(float);
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (!buf) {
        result_ = SaveResult::fail(SaveError::WriteFailed, "could not allocate audio buffer");
        pd->failed = true;
        return false;
    }
    gst_buffer_fill(buf, 0, interleaved, size);

    GST_BUFFER_PTS(buf) = (GstClockTime)(timeSec * GST_SECOND);
    GST_BUFFER_DURATION(buf) =
        (GstClockTime)((double)frames / pd->audioRate * GST_SECOND);

    GstFlowReturn ret =
        gst_app_src_push_buffer(GST_APP_SRC(pd->audiosrc), buf);  // takes ownership
    if (ret != GST_FLOW_OK) {
        if (pollRecordingError(pd, result_))
            result_ = SaveResult::fail(SaveError::WriteFailed,
                std::string("GStreamer appsrc: ") + gst_flow_get_name(ret));
        pd->failed = true;
        return false;
    }
    return true;
}

bool VideoWriter::appendPlatform(const unsigned char* rgba, double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->appsrc) return false;

    if (!pollRecordingError(pd, result_)) return false;
    const gsize size = (gsize)pd->width * pd->height * 4;
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (!buf) {
        result_ = SaveResult::fail(SaveError::WriteFailed, "could not allocate frame buffer");
        pd->failed = true;
        return false;
    }

    // RGBA top-down -> GStreamer RGBA top-down: a straight copy.
    gst_buffer_fill(buf, 0, rgba, size);

    // PTS is decided by the caller (fixed-rate for manual addFrame(), real
    // elapsed time for auto-capture). Duration is one frame at the target fps.
    GST_BUFFER_PTS(buf) = (GstClockTime)(timeSec * GST_SECOND);
    GST_BUFFER_DURATION(buf) =
        (GstClockTime)(GST_SECOND / (pd->fps > 0 ? pd->fps : 30.0));

    GstFlowReturn ret =
        gst_app_src_push_buffer(GST_APP_SRC(pd->appsrc), buf);  // takes ownership
    if (ret != GST_FLOW_OK) {
        if (pollRecordingError(pd, result_))
            result_ = SaveResult::fail(SaveError::WriteFailed,
                std::string("GStreamer appsrc: ") + gst_flow_get_name(ret));
        pd->failed = true;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// closePlatform - end the stream, wait for the muxer to finalize, tear down
// ---------------------------------------------------------------------------
SaveResult VideoWriter::closePlatform() {
    VideoWriterPlatformData* pd = platform_;
    if (!pd) return SaveResult::success();
    auto result = result_;
    if (!pollRecordingError(pd, result) && result)
        result = internal::saveFailure(path_, "GStreamer recording failed");
    if (result && pd->pipeline && pd->appsrc) {
        bool eos = !pd->audiosrc ||
            gst_app_src_end_of_stream(GST_APP_SRC(pd->audiosrc)) == GST_FLOW_OK;
        eos = (gst_app_src_end_of_stream(GST_APP_SRC(pd->appsrc)) == GST_FLOW_OK) && eos;
        if (!eos) {
            // An ERROR can flush appsrc after the initial poll. Keep that
            // failure instead of treating GST_FLOW_FLUSHING as a new one.
            if (pollRecordingError(pd, result))
                result = internal::saveFailure(path_, "GStreamer rejected EOS");
        }
        else {
            GstBus* bus = gst_element_get_bus(pd->pipeline);
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND,
                (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            if (!msg) result = internal::saveFailure(path_, "GStreamer finalize timed out");
            else {
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                    GError* error = nullptr;
                    gst_message_parse_error(msg, &error, nullptr);
                    result = SaveResult::fail(SaveError::WriteFailed,
                        error ? error->message : "GStreamer finalize failed");
                    if (error) g_error_free(error);
                }
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
        }
    }

    if (pd->pipeline) {
        GstBus* bus = gst_element_get_bus(pd->pipeline);
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);
        {
            // Removing the handler prevents new calls but need not wait for
            // one already in progress. Keep pd and its source refs until done.
            std::unique_lock<std::mutex> lock(pd->syncHandlerMutex);
            pd->syncHandlerDone.wait(lock, [pd] { return pd->syncHandlerRemoved; });
        }
        // Stop/join streaming before releasing the sources and callback data.
        gst_element_set_state(pd->pipeline, GST_STATE_NULL);
    }
    if (pd->appsrc)   gst_object_unref(pd->appsrc);
    if (pd->audiosrc) gst_object_unref(pd->audiosrc);
    if (pd->pipeline) gst_object_unref(pd->pipeline);

    delete pd;
    platform_ = nullptr;
    return result;
}

} // namespace trussc

#endif // __linux__
