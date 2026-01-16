// VideoReceiver.cpp
#include "VideoReceiver.h"
#include <QDebug>
#include <QMetaObject>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <gst/video/videooverlay.h>
#include <QElapsedTimer>
#include <gst/app/gstappsink.h>
#include <QImage>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>




// --- pad-added handler -----------------------------------------------------
struct PadUserData {
    GstElement* convert;
    bool* analysisPrinted;
};

static void on_pad_added(GstElement *decodebin,
                         GstPad     *newPad,
                         gpointer    user_data)
{
    auto *data = static_cast<PadUserData*>(user_data);
    auto *convert = data->convert;
    auto *analysisPrinted = data->analysisPrinted;
    GstPad *sinkPad = gst_element_get_static_pad(convert, "sink");

    if (gst_pad_is_linked(sinkPad)) {
        gst_object_unref(sinkPad);
        return;
    }

    // Check caps safely and analyze video stream
    GstCaps *caps = gst_pad_get_current_caps(newPad);
    if (!caps) {
        qWarning() << "[VideoReceiver] Failed to get caps for new pad";
        gst_object_unref(sinkPad);
        return;
    }

    GstStructure *str = gst_caps_get_structure(caps, 0);
    const char *name = gst_structure_get_name(str);
    qDebug() << "[VideoReceiver] pad-added type:" << name;

    if (g_str_has_prefix(name, "video/")) {
        // Analyze and print video stream properties
        if (!(*analysisPrinted) && g_str_has_prefix(name, "video/x-raw")) {
            *analysisPrinted = true;
            
            // Extract video properties
            int width = 0, height = 0;
            gst_structure_get_int(str, "width", &width);
            gst_structure_get_int(str, "height", &height);
            
            const gchar *format = gst_structure_get_string(str, "format");
            const gchar *colorimetry = gst_structure_get_string(str, "colorimetry");
            const gchar *chroma = gst_structure_get_string(str, "chroma-site");
            
            // Get framerate
            const GValue *framerate = gst_structure_get_value(str, "framerate");
            int fps_num = 0, fps_den = 1;
            if (framerate && GST_VALUE_HOLDS_FRACTION(framerate)) {
                fps_num = gst_value_get_fraction_numerator(framerate);
                fps_den = gst_value_get_fraction_denominator(framerate);
            }
            
            // Get pixel aspect ratio
            const GValue *par = gst_structure_get_value(str, "pixel-aspect-ratio");
            int par_num = 1, par_den = 1;
            if (par && GST_VALUE_HOLDS_FRACTION(par)) {
                par_num = gst_value_get_fraction_numerator(par);
                par_den = gst_value_get_fraction_denominator(par);
            }
            
            // Calculate display aspect ratio
            int dar_num = width * par_num;
            int dar_den = height * par_den;
            
            // Simplify DAR
            int gcd = dar_num;
            int temp = dar_den;
            while (temp != 0) {
                int remainder = gcd % temp;
                gcd = temp;
                temp = remainder;
            }
            dar_num /= gcd;
            dar_den /= gcd;
            
            // Print JSON analysis
            printf("\n[gstreamer data]: {\n");
            printf("  \"video_stream_properties\": {\n");
            printf("    \"video_characteristics\": {\n");
            printf("      \"resolution\": {\n");
            printf("        \"width\": %d,\n", width);
            printf("        \"height\": %d\n", height);
            printf("      },\n");
            printf("      \"pixel_format\": \"%s\",\n", format ? format : "unknown");
            printf("      \"framerate\": {\n");
            printf("        \"numerator\": %d,\n", fps_num);
            printf("        \"denominator\": %d,\n", fps_den);
            printf("        \"mode\": \"negotiated\"\n");
            printf("      },\n");
            printf("      \"colorimetry\": \"%s\",\n", colorimetry ? colorimetry : "BT.709");
            printf("      \"chroma_subsampling\": \"%s\",\n", chroma && strstr(chroma, "420") ? "4:2:0" : "unknown");
            printf("      \"bit_depth\": %d,\n", (format && strstr(format, "10")) ? 10 : 8);
            printf("      \"scan_type\": \"progressive\",\n");
            printf("      \"aspect_ratio\": {\n");
            printf("        \"pixel_aspect_ratio\": \"%d:%d\",\n", par_num, par_den);
            printf("        \"display_aspect_ratio\": \"%d:%d\"\n", dar_num, dar_den);
            printf("      }\n");
            printf("    },\n");
            printf("    \"codec_specific\": {\n");
            printf("      \"gop_size\": 0,\n");
            printf("      \"keyframe_interval\": 0,\n");
            printf("      \"nal_unit_type\": \"unknown\"\n");
            printf("    }\n");
            printf("  },\n");
            printf("  \"metadata_source\": {\n");
            printf("    \"caps_access\": [\n");
            printf("      \"pad_caps\",\n");
            printf("      \"GST_MESSAGE_CAPS\",\n");
            printf("      \"gst_pad_get_current_caps\"\n");
            printf("    ]\n");
            printf("  }\n");
            printf("}\n\n");
            fflush(stdout);
        }
        
        if (gst_pad_link(newPad, sinkPad) != GST_PAD_LINK_OK) {
            qDebug() << "[VideoReceiver] Failed to link decodebin → convert";
        }
    }

    gst_caps_unref(caps);
    gst_object_unref(sinkPad);
}
// gboolean VideoReceiver::bus_call(GstBus * /*bus*/, GstMessage *msg, gpointer data) {
//     auto *self = static_cast<VideoReceiver*>(data);
//     switch (GST_MESSAGE_TYPE(msg)) {
//     case GST_MESSAGE_STATE_CHANGED: {
//         if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline)) {
//             GstState oldS, newS, pend;
//             gst_message_parse_state_changed(msg, &oldS, &newS, &pend);
//             if (newS == GST_STATE_PLAYING)
//                 emit self->cameraStarted();
//         }
//         break;
//     }
//     case GST_MESSAGE_ERROR: {
//         GError *err = nullptr;
//         gchar  *dbg = nullptr;
//         gst_message_parse_error(msg, &err, &dbg);
//         QString txt = QString::fromUtf8(err->message);
//         qDebug() << "[VideoReceiver] ERROR:" << txt;
//         emit self->cameraError(txt);
//         gst_element_set_state(self->pipeline, GST_STATE_READY);
//         gst_element_set_state(self->pipeline, GST_STATE_PLAYING);
//         g_error_free(err);
//         g_free(dbg);
//         break;
//     }
//     case GST_MESSAGE_EOS:
//         qDebug() << "[VideoReceiver] End of stream";
//         emit self->cameraError("End of stream");
//         gst_element_set_state(self->pipeline, GST_STATE_READY);
//         gst_element_set_state(self->pipeline, GST_STATE_PLAYING);
//         break;
//     default:
//         break;
//     }
//     return TRUE;
// }


gboolean VideoReceiver::bus_call(GstBus* /*bus*/, GstMessage* msg, gpointer data) {
    auto *self = static_cast<VideoReceiver*>(data);
    if (!self || !self->pipeline) return TRUE;
    static bool wasPlaying = false;               // remember last known state
    static QElapsedTimer errorTimer;              // throttle restarts
    if (!errorTimer.isValid()) errorTimer.start();

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_STATE_CHANGED: {
        GstState oldS, newS, pend;
        gst_message_parse_state_changed(msg, &oldS, &newS, &pend);
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline)) {
            bool nowPlaying = (newS == GST_STATE_PLAYING);
            if (nowPlaying != wasPlaying) {
                wasPlaying = nowPlaying;
                if (nowPlaying) {
                    // Try to get caps from the sink pad when stream starts
                    if (!self->analysisPrinted) {
                        GstElement *sink = nullptr;
                        g_object_get(self->pipeline, "video-sink", &sink, nullptr);
                        if (sink) {
                            GstPad *sinkPad = gst_element_get_static_pad(sink, "sink");
                            if (sinkPad) {
                                GstCaps *caps = gst_pad_get_current_caps(sinkPad);
                                if (caps) {
                                    GstStructure *str = gst_caps_get_structure(caps, 0);
                                    const char *name = gst_structure_get_name(str);
                                    
                                    if (g_str_has_prefix(name, "video/") && !self->analysisPrinted) {
                                        self->analysisPrinted = true;
                                        
                                        // Extract video properties
                                        int width = 0, height = 0;
                                        gst_structure_get_int(str, "width", &width);
                                        gst_structure_get_int(str, "height", &height);
                                        
                                        const gchar *format = gst_structure_get_string(str, "format");
                                        const gchar *colorimetry = gst_structure_get_string(str, "colorimetry");
                                        
                                        // Get framerate
                                        const GValue *framerate = gst_structure_get_value(str, "framerate");
                                        int fps_num = 0, fps_den = 1;
                                        if (framerate && GST_VALUE_HOLDS_FRACTION(framerate)) {
                                            fps_num = gst_value_get_fraction_numerator(framerate);
                                            fps_den = gst_value_get_fraction_denominator(framerate);
                                        }
                                        
                                        // Determine pixel format based on codec
                                        const char *pixelFormat = "I420";
                                        if (g_str_has_prefix(name, "video/x-h264")) {
                                            pixelFormat = "H264";
                                        } else if (g_str_has_prefix(name, "video/x-h265")) {
                                            pixelFormat = "H265";
                                        } else if (format) {
                                            pixelFormat = format;
                                        }
                                        
                                        // Print JSON analysis
                                        printf("\n[gstreamer data]: {\n");
                                        printf("  \"video_stream_properties\": {\n");
                                        printf("    \"video_characteristics\": {\n");
                                        printf("      \"resolution\": {\n");
                                        printf("        \"width\": %d,\n", width);
                                        printf("        \"height\": %d\n", height);
                                        printf("      },\n");
                                        printf("      \"pixel_format\": \"%s\",\n", pixelFormat);
                                        printf("      \"framerate\": {\n");
                                        printf("        \"numerator\": %d,\n", fps_num);
                                        printf("        \"denominator\": %d,\n", fps_den);
                                        printf("        \"mode\": \"negotiated\"\n");
                                        printf("      },\n");
                                        printf("      \"colorimetry\": \"%s\",\n", colorimetry ? colorimetry : "BT.709");
                                        printf("      \"chroma_subsampling\": \"4:2:0\",\n");
                                        printf("      \"bit_depth\": 8,\n");
                                        printf("      \"scan_type\": \"progressive\",\n");
                                        printf("      \"aspect_ratio\": {\n");
                                        printf("        \"pixel_aspect_ratio\": \"1:1\",\n");
                                        printf("        \"display_aspect_ratio\": \"%d:%d\"\n", width, height);
                                        printf("      }\n");
                                        printf("    },\n");
                                        printf("    \"codec_specific\": {\n");
                                        printf("      \"gop_size\": 0,\n");
                                        printf("      \"keyframe_interval\": 0,\n");
                                        printf("      \"nal_unit_type\": \"%s\"\n", g_str_has_prefix(name, "video/x-h264") ? "H264" : "unknown");
                                        printf("    }\n");
                                        printf("  },\n");
                                        printf("  \"metadata_source\": {\n");
                                        printf("    \"caps_access\": [\n");
                                        printf("      \"pad_caps\",\n");
                                        printf("      \"GST_MESSAGE_CAPS\",\n");
                                        printf("      \"gst_pad_get_current_caps\"\n");
                                        printf("    ]\n");
                                        printf("  }\n");
                                        printf("}\n\n");
                                        fflush(stdout);
                                    }
                                    gst_caps_unref(caps);
                                }
                                gst_object_unref(sinkPad);
                            }
                        }
                    }
                    emit self->cameraStarted();
                } else {
                    emit self->cameraError("Stream stopped");
                }
            }
        }
        break;
    }
    case GST_MESSAGE_ERROR: {
        if (errorTimer.elapsed() > 2000) {
            GError *err = nullptr;
            gchar  *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);

            if (err) {
                QString what = QString::fromUtf8(err->message);
                emit self->cameraError(what);
                g_error_free(err);
            }

            if (dbg) g_free(dbg);

            // Try to recover
            if (self->pipeline) {
                gst_element_set_state(self->pipeline, GST_STATE_READY);
                gst_element_set_state(self->pipeline, GST_STATE_PLAYING);
            }
            errorTimer.restart();
        }
        break;
    }

    case GST_MESSAGE_EOS: {
        if (errorTimer.elapsed() > 2000) {
            emit self->cameraError("End of stream");
            gst_element_set_state(self->pipeline, GST_STATE_READY);
            gst_element_set_state(self->pipeline, GST_STATE_PLAYING);
            errorTimer.restart();
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}

//For SIYI camera
VideoReceiver::VideoReceiver(QObject *parent)
    : QObject(parent),
    pipeline(nullptr),
    videosink(nullptr)
{
    gst_init(nullptr, nullptr);

    QString uri = getRtspUriFromConfig();
    qDebug() << "[VideoReceiver] Using RTSP URI:" << uri;

    // 1) Create playbin
    pipeline  = gst_element_factory_make("playbin", "player");
    if (!pipeline) {
        qCritical() << "[VideoReceiver] Failed to create playbin";
        return;
    }

    GstElement* convert = gst_element_factory_make("videoconvert",  "convert");

    // 2) Create the sink and tell playbin to use it
    videosink = gst_element_factory_make("xvimagesink", "videosink");
    if (!videosink) {
        qCritical() << "[VideoReceiver] Failed to create xvimagesink";
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return;
    }
    // disable internal clock sync so frames show immediately
    g_object_set(videosink, "sync", FALSE, nullptr);

    // 3) Configure playbin: set our RTSP URI, low latency, and video sink
    g_object_set(pipeline,
                 "uri",         uri.toUtf8().constData(),
                 "latency",     100,            // 100 ms jitter buffer
                 "video-sink",  videosink,
                 nullptr);

    //Using PC camera for testing purposes:
    // temporarily use the laptop camera:
    // const char *webcamUri = "v4l2:///dev/video0";
    // g_object_set(pipeline,
    //              "uri",       webcamUri,
    //              "video-sink", videosink,
    //              nullptr);

    // 4) Watch the bus for EOS / errors / state changes
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, VideoReceiver::bus_call, this);
    gst_object_unref(bus);

    // 5) Fire it up
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    qDebug() << "[VideoReceiver] playbin → PLAYING";
}


// // //For second camera
// VideoReceiver::VideoReceiver(QObject *parent)
//     : QObject(parent),
//     pipeline(nullptr),
//     videosink(nullptr),
//     appsink(nullptr)
// {
//     gst_init(nullptr, nullptr);

//     // 1) Create all elements
//     //auto* src      = gst_element_factory_make("rtspsrc",      "src");
//     rtspSrc        = gst_element_factory_make("rtspsrc",      "src");
//     auto* depay    = gst_element_factory_make("rtph264depay", "depay");
//     auto* parser   = gst_element_factory_make("h264parse",    "parser");
//     //auto* decoder  = gst_element_factory_make("avdec_h264",   "decoder");
//     auto* decoder = gst_element_factory_make("vaapih264dec", "hwdec");
//     auto* tee      = gst_element_factory_make("tee",          "tee");
//     auto* q1       = gst_element_factory_make("queue",        "q1");
//     auto* q2       = gst_element_factory_make("queue",        "q2");
//     auto* convert1 = gst_element_factory_make("videoconvert","convert1");
//     videosink      = gst_element_factory_make("xvimagesink",  "videosink");
//     //auto* videosink = gst_element_factory_make("glimagesink",   "glsink");
//     auto* convert2 = gst_element_factory_make("videoconvert","convert2");
//     appsink        = gst_element_factory_make("appsink",       "appsink");

//     if (!rtspSrc||!depay||!parser||!decoder||!tee||!q1||!q2
//         ||!convert1||!videosink||!convert2||!appsink)
//     {
//         qCritical() << "[VideoReceiver] failed to create elements";
//         return;
//     }

//     // ── **HERE** ──
//     // 2) Configure RTSP source with low‐latency
//     QString uri = getRtspUriFromConfig().trimmed();
//     qDebug() << "RTSP URI is" << uri;
//     g_object_set(rtspSrc,"location", uri.toUtf8().constData(),"latency",(guint)500,nullptr);

//     GstCaps* caps = gst_caps_new_simple("video/x-raw","format", G_TYPE_STRING, "RGB",nullptr);
//     // g_object_set(appsink,"caps",caps,"emit-signals", FALSE,"sync", FALSE,nullptr);
//     g_object_set(appsink,"max-buffers", 5,"drop", TRUE, nullptr);
//     gst_caps_unref(caps);

//     // 3) Build pipeline
//     pipeline = gst_pipeline_new("video-receiver");
//     gst_bin_add_many(GST_BIN(pipeline),rtspSrc,depay, parser, decoder,tee,q1, convert1, videosink,q2, convert2, appsink,nullptr);

//     gst_element_link_many(depay, parser, decoder, tee, nullptr);
//     gst_element_link_many(tee, q1, convert1, videosink, nullptr);
//     gst_element_link_many(tee, q2, convert2, appsink,     nullptr);

//     // 4) Dynamic pad from rtspsrc → depay
//     g_signal_connect(rtspSrc, "pad-added", G_CALLBACK(+[](GstElement* /*rtspsrc*/,
//         GstPad* newPad,gpointer data){GstPad* sink = gst_element_get_static_pad(static_cast<GstElement*>(data),"sink");
//             if (!GST_PAD_IS_LINKED(sink))
//                 gst_pad_link(newPad, sink);
//             gst_object_unref(sink);
//             }),
//     depay);

//     // 5) Bus watch, start playing, etc.…
//     GstBus* bus = gst_element_get_bus(pipeline);
//     gst_bus_add_watch(bus, VideoReceiver::bus_call, this);
//     gst_object_unref(bus);

//     gst_element_set_state(pipeline, GST_STATE_PLAYING);
//     qDebug() << "[VideoReceiver] Pipeline set to PLAYING";
// }
/*optimised resources pipeline*/
// VideoReceiver::VideoReceiver(QObject *parent)
//     : QObject(parent),
//     pipeline(nullptr),
//     videosink(nullptr),
//     appsink(nullptr)
// {
//     gst_init(nullptr, nullptr);

//     // 1) Create all elements
//     rtspSrc        = gst_element_factory_make("rtspsrc",      "src");
//     auto* depay    = gst_element_factory_make("rtph264depay", "depay");
//     auto* parser   = gst_element_factory_make("h264parse",    "parser");
//     auto* decoder  = gst_element_factory_make("vaapih264dec", "hwdec"); // HW decode
//     auto* tee      = gst_element_factory_make("tee",          "tee");
//     auto* q1       = gst_element_factory_make("queue",        "q1");
//     auto* q2       = gst_element_factory_make("queue",        "q2");

//     // Use GPU postproc instead of CPU videoconvert
//     auto* convert1 = gst_element_factory_make("vaapipostproc", "convert1");
//     auto* convert2 = gst_element_factory_make("vaapipostproc", "convert2");

//     // Use GPU-accelerated sink if available
//     videosink      = gst_element_factory_make("vaapisink",    "videosink");

//     appsink        = gst_element_factory_make("appsink",      "appsink");

//     if (!rtspSrc || !depay || !parser || !decoder || !tee || !q1 || !q2 ||
//         !convert1 || !videosink || !convert2 || !appsink)
//     {
//         qCritical() << "[VideoReceiver] failed to create elements";
//         return;
//     }

//     // 2) Configure RTSP source with low latency
//     QString uri = getRtspUriFromConfig().trimmed();
//     qDebug() << "RTSP URI is" << uri;
//     g_object_set(rtspSrc,
//                  "location", uri.toUtf8().constData(),
//                  "latency", (guint)150,       // Reduced buffering
//                  "protocols", 0x00000004,     // TCP (reliable)
//                  nullptr);

//     // 3) Limit queue memory usage
//     g_object_set(q1,
//                  "max-size-buffers", 5,
//                  "max-size-bytes", 0,
//                  "max-size-time", 0,
//                  nullptr);
//     g_object_set(q2,
//                  "max-size-buffers", 5,
//                  "max-size-bytes", 0,
//                  "max-size-time", 0,
//                  nullptr);

//     // 4) Appsink settings — low memory, no sync, drop old frames
//     GstCaps* caps = gst_caps_new_simple("video/x-raw",
//                                         "format", G_TYPE_STRING, "I420", // Lightweight format
//                                         nullptr);
//     g_object_set(appsink,
//                  "caps", caps,
//                  "emit-signals", FALSE,
//                  "sync", FALSE,
//                  "max-buffers", 5,
//                  "drop", TRUE,
//                  nullptr);
//     gst_caps_unref(caps);

//     // 5) Build pipeline
//     pipeline = gst_pipeline_new("video-receiver");
//     gst_bin_add_many(GST_BIN(pipeline),
//                      rtspSrc, depay, parser, decoder,
//                      tee,
//                      q1, convert1, videosink,
//                      q2, convert2, appsink,
//                      nullptr);

//     gst_element_link_many(depay, parser, decoder, tee, nullptr);
//     gst_element_link_many(tee, q1, convert1, videosink, nullptr);
//     gst_element_link_many(tee, q2, convert2, appsink, nullptr);

//     // 6) Dynamic pad from rtspsrc → depay
//     g_signal_connect(rtspSrc, "pad-added",
//                      G_CALLBACK(+[](GstElement* /*rtspsrc*/,
//                                     GstPad* newPad,
//                                     gpointer data)
//                                 {
//                                     GstPad* sink = gst_element_get_static_pad(
//                                         static_cast<GstElement*>(data), "sink");
//                                     if (!GST_PAD_IS_LINKED(sink))
//                                         gst_pad_link(newPad, sink);
//                                     gst_object_unref(sink);
//                                 }),
//                      depay);

//     // 7) Bus watch and start
//     GstBus* bus = gst_element_get_bus(pipeline);
//     gst_bus_add_watch(bus, VideoReceiver::bus_call, this);
//     gst_object_unref(bus);

//     gst_element_set_state(pipeline, GST_STATE_PLAYING);
//     qDebug() << "[VideoReceiver] Pipeline set to PLAYING (optimized)";
// }

// void VideoReceiver::setWindowId(WId id) {
//     if (videosink) {
//         gst_video_overlay_set_window_handle(
//             GST_VIDEO_OVERLAY(videosink),
//             (guintptr)id
//             );
//     }
// }

// void VideoReceiver::setWindowId(WId id) {
//     savedWindowId = id; // store for later if pipeline is rebuilt
//     if (videosink && GST_IS_VIDEO_OVERLAY(videosink)) {
//         gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videosink), (guintptr)id);
//         qDebug() << "[VideoReceiver] setWindowId: applied handle to existing videosink";
//     } else {
//         qDebug() << "[VideoReceiver] setWindowId: saved handle" << id;
//     }
// }

void VideoReceiver::setWindowId(WId id) {
    // store the id for later pipelines too
    savedWindowId = id;

    if (!videosink) {
        qDebug() << "[VideoReceiver] setWindowId: saved handle" << id << " (no videosink yet)";
        return;
    }

    // attach the overlay to the videosink immediately
    qDebug() << "[VideoReceiver] setWindowId: applied handle to existing videosink";
    gst_video_overlay_set_window_handle(
        GST_VIDEO_OVERLAY(videosink),
        (guintptr)savedWindowId
        );
}



VideoReceiver::~VideoReceiver() {
    if (pipeline) {
        // Stop pipeline first
        gst_element_set_state(pipeline, GST_STATE_NULL);

        // Remove bus watch
        GstBus* bus = gst_element_get_bus(pipeline);
        gst_bus_remove_watch(bus);
        gst_object_unref(bus);

        // Unreference pipeline
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }

    // Don't unref individual elements - they're owned by the pipeline
    videosink = nullptr;
    appsink = nullptr;
}


void VideoReceiver::start() {
    if (!pipeline) {
        qWarning() << "[VideoReceiver] start(): no pipeline to start";
        return;
    }

    // Create local copy of pipeline pointer so the worker thread
    // doesn't race with pipeline being reset on the main thread.
    GstElement *localPipeline = pipeline;

    // Use QPointer so we can detect if `this` is deleted while the background task runs.
    QPointer<VideoReceiver> self(this);

    // Run the potentially-blocking state change in a background task.
    QtConcurrent::run([self, localPipeline]() {
        if (!self) return; // object was destroyed already

        // attempt to set to PLAYING (this CAN block if the RTSP connect stalls)
        GstStateChangeReturn ret = gst_element_set_state(localPipeline, GST_STATE_PLAYING);

        if (ret == GST_STATE_CHANGE_FAILURE) {
            // notify on the GUI thread
            QMetaObject::invokeMethod(self, [self]() {
                if (!self) return;
                qWarning() << "[VideoReceiver] async start failed (STATE_CHANGE_FAILURE)";
                emit self->cameraError(QStringLiteral("Failed to start GStreamer pipeline"));
            }, Qt::QueuedConnection);
        } else {
            // success or async change in progress — bus callback will emit cameraStarted when state reaches PLAYING
            qDebug() << "[VideoReceiver] async: initiated state change (ret =" << ret << ")";
        }
    });
}



void VideoReceiver::stop()
{
    if (!pipeline) return;

    gst_element_set_state(pipeline, GST_STATE_NULL);

    if (auto bus = gst_element_get_bus(pipeline)) {
        gst_bus_remove_watch(bus);
        gst_object_unref(bus);
    }

    qDebug() << "[VideoReceiver] stop(): entering";

    gst_object_unref(pipeline);
    pipeline = nullptr;

    // Make sure to drop sinks too (they're owned by pipeline anyway)
    videosink = nullptr;
    appsink   = nullptr;

    qDebug() << "[VideoReceiver] stop(): pipeline destroyed and references cleared";
}

// --- read URI from JSON config (unchanged) --------------------------------
QString VideoReceiver::getRtspUriFromConfig() {
    qDebug() << "[VIDEO_SOURCE] getRtspUriFromConfig() called";
    
    // Get the standard config location (typically ~/.config)
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

    // Define a subfolder name for our camera configuration
    QString subFolder = "Haxa5Camera";

    // Create a QDir object for the config directory
    QDir configDirectory(configDir);
    // Create the subfolder if it doesn't exist
    if (!configDirectory.exists(subFolder)) {
        if (!configDirectory.mkdir(subFolder)) {
            qWarning() << "Failed to create subfolder:" << subFolder;
        }
    }

    // Build the full path for the config file in the subfolder
    QString configFile = configDirectory.filePath(subFolder + "/Hexa5CameraConfig.json");
    qDebug() << "[VIDEO_SOURCE] Reading config from:" << configFile;

    // Default values
    QString defaultIP = "192.168.144.25";
    int defaultPort = 8554;
    QString defaultPath = "/main.264";
    QString defaultUri = QString("rtsp://%1:%2%3").arg(defaultIP).arg(defaultPort).arg(defaultPath);

    QFile file(configFile);
    if (!file.exists()) {
        qDebug() << "[VIDEO_SOURCE] Config file does not exist, using default URI:" << defaultUri;
        // If the config file does not exist, create it with default values.
        QJsonObject obj;
        obj["ip"] = defaultIP;
        obj["port"] = defaultPort;
        obj["path"] = defaultPath;
        QJsonDocument doc(obj);

        // Ensure the subfolder exists (already attempted above)
        if (file.open(QIODevice::WriteOnly)) {
            file.write(doc.toJson(QJsonDocument::Indented));
            file.close();
            qDebug() << "Created config file with default RTSP URI at" << configFile;
        } else {
            qWarning() << "Failed to create config file at" << configFile << "; using default RTSP URI.";
        }
        return defaultUri;
    } else {
        // Read and parse the config file.
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                qDebug() << "[VIDEO_SOURCE] Config loaded successfully";
                
                // Check if this is old format for backward compatibility
                if (obj.contains("cameraType")) {
                    QString cameraType = obj.value("cameraType").toString("siyi").toLower();
                    qDebug() << "[VIDEO_SOURCE] Using legacy format, cameraType:" << cameraType;
                    QString ip = obj.value("ip").toString(defaultIP);
                    int port = obj.value("port").toInt(defaultPort);
                    QString path = obj.value("path").toString(defaultPath);
                    
                    if (cameraType == "ai") {
                        // Handle old AI format
                        QString aiCameraIP = obj.value("aiCameraIP").toString(defaultIP);
                        int aiControlPort = obj.value("aiControlPort").toInt(defaultPort);
                        QString aiPath = obj.value("path").toString("/ai/stream");
                        QString rtspUrl = QString("rtsp://%1:%2%3").arg(aiCameraIP).arg(aiControlPort).arg(aiPath);
                        qDebug() << "[VIDEO_SOURCE] Legacy AI RTSP URL:" << rtspUrl;
                        return rtspUrl;
                    } else {
                        // Handle old SIYI format
                        QString rtspUrl = QString("rtsp://%1:%2%3").arg(ip).arg(port).arg(path);
                        qDebug() << "[VIDEO_SOURCE] Legacy SIYI RTSP URL:" << rtspUrl;
                        return rtspUrl;
                    }
                }
                
                // Handle new parallel format
                QString videoSource = obj.value("videoSource").toString("siyi").toLower();
                qDebug() << "[VIDEO_SOURCE] Using parallel format, videoSource:" << videoSource;
                
                if (videoSource == "ai" && obj.contains("aiConfig")) {
                    QJsonObject aiConfig = obj.value("aiConfig").toObject();
                    QString aiCameraIP = aiConfig.value("cameraIP").toString(defaultIP);
                    int aiControlPort = aiConfig.value("controlPort").toInt(defaultPort);
                    QString aiPath = aiConfig.value("path").toString("/video");
                    QString rtspUrl = QString("rtsp://%1:%2%3").arg(aiCameraIP).arg(aiControlPort).arg(aiPath);
                    qDebug() << "[VIDEO_SOURCE] Parallel AI RTSP URL:" << rtspUrl;
                    return rtspUrl;
                    
                } else if (videoSource == "siyi" && obj.contains("siyiConfig")) {
                    QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
                    QString siyiIP = siyiConfig.value("ip").toString(defaultIP);
                    int siyiPort = siyiConfig.value("port").toInt(defaultPort);
                    QString siyiPath = siyiConfig.value("path").toString(defaultPath);
                    QString rtspUrl = QString("rtsp://%1:%2%3").arg(siyiIP).arg(siyiPort).arg(siyiPath);
                    qDebug() << "[VIDEO_SOURCE] Parallel SIYI RTSP URL:" << rtspUrl;
                    return rtspUrl;
                }
                
                // Fallback to default if no valid config found
                qDebug() << "[VIDEO_SOURCE] No valid config found, using default URI:" << defaultUri;
                return defaultUri;
                
            } else {
                qWarning() << "[VIDEO_SOURCE] Config file is not a valid JSON object; using default RTSP URI.";
                return defaultUri;
            }
        } else {
            qWarning() << "[VIDEO_SOURCE] Failed to open config file; using default RTSP URI.";
            return defaultUri;
        }
    }
}

bool VideoReceiver::isPlaying() const {
    if (!pipeline) return false;
    GstState cur, pending;
    // zero timeout means “just query”
    if (gst_element_get_state(pipeline, &cur, &pending, 0)
        == GST_STATE_CHANGE_SUCCESS) {
        return cur == GST_STATE_PLAYING;
    }
    return false;
}

QImage VideoReceiver::grabFrame()
{
    if (!appsink) return {};

    // Pull the most recent sample (non-blocking)
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 0);
    if (!sample) return {};

    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstMapInfo info;
    if (!gst_buffer_map(buf, &info, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return {};
    }

    GstCaps *caps = gst_sample_get_caps(sample);
    auto *s      = gst_caps_get_structure(caps, 0);
    int width, height;
    gst_structure_get_int(s, "width",  &width);
    gst_structure_get_int(s, "height", &height);

    // appsink caps forced us to RGB
    QImage img((uchar*)info.data, width, height, QImage::Format_RGB888);

    // copy before we unmap/unref
    QImage copy = img.copy();

    gst_buffer_unmap(buf, &info);
    gst_sample_unref(sample);
    return copy;

    if (sample) {
        // Always unref sample when done
        QImage result;
        GstBuffer *buf = gst_sample_get_buffer(sample);
        if (buf && gst_buffer_map(buf, &info, GST_MAP_READ)) {
            // ... image creation code ...
            gst_buffer_unmap(buf, &info);
        }
        gst_sample_unref(sample);
        return result;
    }
    return {};
}


// void VideoReceiver::setRtspUri(const QString &uri)
// {
//     if (rtspSrc) {
//         g_object_set(rtspSrc, "location", uri.toUtf8().constData(), nullptr);
//     }
// }
void VideoReceiver::setRtspUri(const QString& uri) {
    qDebug() << "[VideoReceiver] setRtspUri called with" << uri;

    // If pipeline exists, stop and clean up
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);

        // remove bus watch (if any) and unref pipeline
        if (auto bus = gst_element_get_bus(pipeline)) {
            gst_bus_remove_watch(bus);
            gst_object_unref(bus);
        }
        gst_object_unref(pipeline);
        pipeline = nullptr;
        qDebug() << "[VideoReceiver] setRtspUri: old pipeline destroyed";
    }

    // Reset analysis flag for new stream
    analysisPrinted = false;

    // create a fresh pipeline that uses the persistent videosink
    createPipeline(uri);
}



void VideoReceiver::createPipeline(const QString& uri) {
    qDebug() << "[VideoReceiver] createPipeline: creating playbin for" << uri;

    // make sure we have a persistent videosink to attach the Qt window to
    if (!videosink) {
        // create a persistent xvimagesink (owned by us, not the pipeline)
        videosink = gst_element_factory_make("xvimagesink", "videosink");
        if (!videosink) {
            qCritical() << "[VideoReceiver] createPipeline: failed to create xvimagesink";
            return;
        }
        // disable sync so frames show immediately
        g_object_set(videosink, "sync", FALSE, nullptr);
    }

    // Create the playbin pipeline
    pipeline = gst_element_factory_make("playbin", "playbin");
    if (!pipeline) {
        qCritical() << "[VideoReceiver] createPipeline: failed to create playbin!";
        return;
    }

    // Ensure the sink is set on playbin before starting playback so playbin
    // does not create a default sink (which would open a new top-level window).
    g_object_set(pipeline,
                 "uri", uri.toUtf8().constData(),
                 "latency", 100,
                 "video-sink", videosink,
                 nullptr);

    // If we already have a saved window handle, apply it to the videosink overlay.
    if (savedWindowId != 0) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videosink),
                                            (guintptr)savedWindowId);
    }

    // Attach a bus watch
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, VideoReceiver::bus_call, this);
    gst_object_unref(bus);

    // Start the pipeline
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    qDebug() << "[VideoReceiver] createPipeline: PLAYING for" << uri << " (ret =" << ret << ")";
}
