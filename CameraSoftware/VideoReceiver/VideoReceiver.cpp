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
#include <gst/video/video.h>
#include <QElapsedTimer>
#include <gst/app/gstappsink.h>
#include <QImage>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>




// --- pad-added handler -----------------------------------------------------
struct PadUserData {
    GstElement* convert;
    bool* analysisPrinted;
    VideoReceiver* receiver;
};

static void on_pad_added(GstElement *decodebin,
                         GstPad     *newPad,
                         gpointer    user_data)
{
    auto *data = static_cast<PadUserData*>(user_data);
    auto *convert = data->convert;
    auto *analysisPrinted = data->analysisPrinted;
    auto *receiver = data->receiver;
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
        // Collect video info from decoded caps (matching RtspAnalyzer::collectVideoInfo)
        if (!(*analysisPrinted) && g_str_has_prefix(name, "video/x-raw")) {
            *analysisPrinted = true;
            if (receiver) {
                receiver->collectVideoInfoFromPad(newPad);
            }
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

    // In compressor mode use a faster reconnect throttle (500 ms vs 2 s)
    const qint64 throttleMs = self->compressorMode_ ? 500 : 2000;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_STATE_CHANGED: {
        GstState oldS, newS, pend;
        gst_message_parse_state_changed(msg, &oldS, &newS, &pend);
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline)) {
            bool nowPlaying = (newS == GST_STATE_PLAYING);
            if (nowPlaying != wasPlaying) {
                wasPlaying = nowPlaying;
                if (nowPlaying) {
                    // Stream recovered — stop the reconnect timer if running
                    if (self->reconnectTimer_ && self->reconnectTimer_->isActive()) {
                        QMetaObject::invokeMethod(self->reconnectTimer_, "stop", Qt::QueuedConnection);
                        qDebug() << "[VideoReceiver] Stream recovered, reconnect timer stopped";
                    }

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
                                        // Delegate to collectVideoInfoFromPad (matching RtspAnalyzer approach)
                                        self->collectVideoInfoFromPad(sinkPad);
                                    }
                                    gst_caps_unref(caps);
                                }
                                gst_object_unref(sinkPad);
                            }
                        }
                    }

                    // Bandwidth timers are now lazy-started in updateBandwidth()
                    // on receipt of the first buffer probe from rtspsrc.

                    emit self->cameraStarted();
                } else {
                    emit self->cameraError("Stream stopped");
                    // In compressor mode, kick off fast reconnect timer
                    if (self->compressorMode_ && self->reconnectTimer_ && !self->reconnectTimer_->isActive()) {
                        QMetaObject::invokeMethod(self->reconnectTimer_, "start", Qt::QueuedConnection);
                        qDebug() << "[VideoReceiver] Compressor mode: stream stopped, starting 500ms reconnect timer";
                    }
                }
            }
        }
        break;
    }
    case GST_MESSAGE_ERROR: {
        if (errorTimer.elapsed() > throttleMs) {
            GError *err = nullptr;
            gchar  *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);

            if (err) {
                QString what = QString::fromUtf8(err->message);
                emit self->cameraError(what);
                g_error_free(err);
            }

            if (dbg) g_free(dbg);

            if (self->compressorMode_) {
                // In compressor mode, delegate recovery to the reconnect timer
                if (self->reconnectTimer_ && !self->reconnectTimer_->isActive()) {
                    QMetaObject::invokeMethod(self->reconnectTimer_, "start", Qt::QueuedConnection);
                    qDebug() << "[VideoReceiver] Compressor mode: error, starting 500ms reconnect timer";
                }
            } else {
                // Normal mode: inline recovery attempt
                if (self->pipeline) {
                    gst_element_set_state(self->pipeline, GST_STATE_READY);
                    gst_element_set_state(self->pipeline, GST_STATE_PLAYING);
                }
            }
            errorTimer.restart();
        }
        break;
    }

    case GST_MESSAGE_EOS: {
        if (errorTimer.elapsed() > throttleMs) {
            emit self->cameraError("End of stream");
            if (self->compressorMode_) {
                // In compressor mode, delegate recovery to the reconnect timer
                if (self->reconnectTimer_ && !self->reconnectTimer_->isActive()) {
                    QMetaObject::invokeMethod(self->reconnectTimer_, "start", Qt::QueuedConnection);
                    qDebug() << "[VideoReceiver] Compressor mode: EOS, starting 500ms reconnect timer";
                }
            } else {
                gst_element_set_state(self->pipeline, GST_STATE_READY);
                gst_element_set_state(self->pipeline, GST_STATE_PLAYING);
            }
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

    // Read compressor mode from config for fast-reconnect behaviour
    compressorMode_ = readCompressorModeFromConfig();
    qDebug() << "[VideoReceiver] compressorMode:" << compressorMode_;

    // Setup reconnect timer (used only in compressor mode)
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setInterval(500);  // 500 ms reconnect attempts
    reconnectTimer_->setSingleShot(false);
    connect(reconnectTimer_, &QTimer::timeout, this, &VideoReceiver::tryReconnect);

    // Low-bandwidth auto-restart timer — single-shot 500 ms delay
    lowBwRestartTimer_ = new QTimer(this);
    lowBwRestartTimer_->setInterval(kLowBwRestartDelayMs);
    lowBwRestartTimer_->setSingleShot(true);
    connect(lowBwRestartTimer_, &QTimer::timeout, this, &VideoReceiver::onLowBandwidthRestart);

    // Stall detection timer — checks every 1 s if data has stopped flowing
    stallCheckTimer_ = new QTimer(this);
    stallCheckTimer_->setInterval(1000);
    stallCheckTimer_->setSingleShot(false);
    connect(stallCheckTimer_, &QTimer::timeout, this, &VideoReceiver::onStallCheck);

    QString uri = getRtspUriFromConfig();
    currentUri_ = uri;  // cache for reconnect
    qDebug() << "[VideoReceiver] Using RTSP URI:" << uri;

    // 1) Create playbin
    pipeline  = gst_element_factory_make("playbin", "player");
    if (!pipeline) {
        qCritical() << "[VideoReceiver] Failed to create playbin";
        return;
    }

    GstElement* convert = gst_element_factory_make("videoconvert",  "convert");

    // 2) Create the sink and tell playbin to use it
    // Try different video sinks in order of preference
    const char* sinkNames[] = {"xvimagesink", "glimagesink", "vaapisink", "autovideosink", nullptr};
    const char** sinkName = sinkNames;
    
    while (*sinkName) {
        videosink = gst_element_factory_make(*sinkName, "videosink");
        if (videosink) {
            qDebug() << "[VideoReceiver] Created" << *sinkName << "as videosink";
            break;
        }
        sinkName++;
    }
    
    if (!videosink) {
        qCritical() << "[VideoReceiver] Failed to create any videosink";
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return;
    }
    
    // Check if the sink supports video overlay
    if (GST_IS_VIDEO_OVERLAY(videosink)) {
        qDebug() << "[VideoReceiver]" << *sinkName << "supports video overlay";
    } else {
        qDebug() << "[VideoReceiver]" << *sinkName << "does NOT support video overlay";
    }
    
    // disable sync so frames show immediately (zero-latency rendering)
    g_object_set(videosink, "sync", FALSE, nullptr);

    // Enable last-sample so grabFrame() can pull the current frame
    g_object_set(videosink, "enable-last-sample", TRUE, nullptr);

    // Enable QoS — lets the sink signal upstream to drop frames if
    // rendering can't keep up, preventing queue buildup.
    GParamSpec* qosSpec = g_object_class_find_property(
        G_OBJECT_GET_CLASS(videosink), "qos");
    if (qosSpec) {
        g_object_set(videosink, "qos", TRUE, nullptr);
    }

    // 3) Configure playbin for low-latency live streaming
    g_object_set(pipeline,
                 "uri",             uri.toUtf8().constData(),
                 "latency",         100,            // 100 ms jitter buffer
                 "buffer-size",     0,              // disable byte-based buffering
                 "buffer-duration", (gint64)0,      // disable time-based buffering
                 "video-sink",      videosink,
                 nullptr);

    // Set playbin flags: video + audio + native-video (skip colorspace
    // conversion when the sink can handle the format natively).
    // GST_PLAY_FLAG_VIDEO=1, GST_PLAY_FLAG_AUDIO=2, GST_PLAY_FLAG_NATIVE_VIDEO=32
    // We drop BUFFERING (0x100) and SOFT_COLORBALANCE (0x400) to reduce overhead.
    gint flags = 0;
    g_object_get(pipeline, "flags", &flags, nullptr);
    flags |= (1 | 2 | 32);   // video + audio + native-video
    flags &= ~0x100;          // disable internal buffering (live stream)
    g_object_set(pipeline, "flags", flags, nullptr);

    //Using PC camera for testing purposes:
    // temporarily use the laptop camera:
    // const char *webcamUri = "v4l2:///dev/video0";
    // g_object_set(pipeline,
    //              "uri",       webcamUri,
    //              "video-sink", videosink,
    //              nullptr);

    // 3.5) Intercept rtspsrc via source-setup to install bandwidth probes
    //       on the raw RTP pads (before decoding), matching RtspAnalyzer.
    g_signal_connect(pipeline, "source-setup",
                     G_CALLBACK(VideoReceiver::onSourceSetup), this);

    // 4) Watch the bus for EOS / errors / state changes
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, VideoReceiver::bus_call, this);
    gst_object_unref(bus);

    // 5) Fire it up
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    qDebug() << "[VideoReceiver] playbin → PLAYING";

    // Start the warmup clock and stall detection for the initial connection
    bwStartTimer_.start();
    if (stallCheckTimer_ && !stallCheckTimer_->isActive())
        stallCheckTimer_->start();
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

    // Check if videosink implements the video overlay interface
    if (!GST_IS_VIDEO_OVERLAY(videosink)) {
        qDebug() << "[VideoReceiver] setWindowId: videosink does not support video overlay";
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

    // Stop reconnect timer so it doesn't fire after teardown
    if (reconnectTimer_ && reconnectTimer_->isActive())
        reconnectTimer_->stop();

    // Cancel any pending low-bandwidth restart
    if (lowBwRestartTimer_ && lowBwRestartTimer_->isActive())
        lowBwRestartTimer_->stop();
    lowBwRestartPending_ = false;
    lowBwConsecutiveHits_ = 0;

    // Stop stall detection
    if (stallCheckTimer_ && stallCheckTimer_->isActive())
        stallCheckTimer_->stop();
}

// ---------------------------------------------------------------------------
// Compressor-mode fast reconnect helpers
// ---------------------------------------------------------------------------

void VideoReceiver::setCompressorMode(bool enabled)
{
    compressorMode_ = enabled;
    qDebug() << "[VideoReceiver] compressorMode set to" << enabled;
}

bool VideoReceiver::compressorMode() const
{
    return compressorMode_;
}

void VideoReceiver::setStreamReachable(bool reachable)
{
    if (streamReachable_ == reachable)
        return;

    streamReachable_ = reachable;

    if (!reachable) {
        // Camera IP became unreachable — cancel any pending low-bandwidth
        // restart (no point cycling the pipeline when the network is down).
        if (lowBwRestartPending_) {
            lowBwRestartPending_ = false;
            lowBwConsecutiveHits_ = 0;
            if (lowBwRestartTimer_ && lowBwRestartTimer_->isActive())
                lowBwRestartTimer_->stop();
        }
        // Pause stall detection while host is down
        if (stallCheckTimer_ && stallCheckTimer_->isActive())
            stallCheckTimer_->stop();
        qDebug() << "[VideoReceiver] Stream host became UNREACHABLE — low-bw restart suppressed";
    } else {
        // Camera IP became reachable again — restart the pipeline so the
        // RTSP source re-establishes the connection.
        qDebug() << "[VideoReceiver] Stream host became REACHABLE — restarting pipeline";

        // Reset bandwidth state so the warmup window applies fresh
        {
            QMutexLocker lk(&bwMutex_);
            bwStats_ = BandwidthStats{};
            lastSampleBytes_ = 0;
        }
        bwStartTimer_.start();             // start the warmup clock now
        bwSampleTimer_.invalidate();
        lastBufferTimer_.invalidate();     // stays invalid until first buffer
        probeInstalledOnSource_ = false;
        lowBwConsecutiveHits_ = 0;
        lowBwRestartPending_ = false;

        // Start stall detection so onStallCheck can catch "never connected"
        if (stallCheckTimer_ && !stallCheckTimer_->isActive())
            stallCheckTimer_->start();

        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_element_set_state(pipeline, GST_STATE_READY);
            GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE) {
                qWarning() << "[VideoReceiver] setStreamReachable: PLAYING transition failed";
                emit cameraError(QStringLiteral("Pipeline restart failed after host became reachable"));
            }
        } else {
            // Pipeline was destroyed — rebuild from scratch
            createPipeline(currentUri_);
            if (pipeline && savedWindowId && videosink && GST_IS_VIDEO_OVERLAY(videosink)) {
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videosink),
                                                    (guintptr)savedWindowId);
            }
        }
    }
}

bool VideoReceiver::readCompressorModeFromConfig() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    QString cfgFile = dir.filePath("Haxa5Camera/Hexa5CameraConfig.json");

    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QByteArray data = f.readAll();
    f.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;

    QJsonObject obj = doc.object();
    if (obj.contains("siyiConfig")) {
        QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
        return siyiConfig.value("compressorMode").toBool(false);
    }
    return false;
}

void VideoReceiver::tryReconnect()
{
    qDebug() << "[VideoReceiver] tryReconnect() — attempting RTSP reconnect to" << currentUri_;

    if (!pipeline) {
        // Pipeline was fully destroyed — rebuild from scratch
        createPipeline(currentUri_);
        if (pipeline && savedWindowId) {
            if (videosink && GST_IS_VIDEO_OVERLAY(videosink)) {
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videosink),
                                                    (guintptr)savedWindowId);
            }
        }
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
        }
        return;
    }

    // Pipeline exists — cycle READY → PLAYING
    gst_element_set_state(pipeline, GST_STATE_READY);
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[VideoReceiver] tryReconnect: PLAYING transition failed, will retry";
    }
}

// ---------------------------------------------------------------------------
// Low-bandwidth auto-restart
// Called 0.5 s after bandwidth dropped below 0.5 Mbps.  Resets monitoring
// state and cycles the pipeline READY → PLAYING so the RTSP source
// re-establishes the connection.
// ---------------------------------------------------------------------------
void VideoReceiver::onLowBandwidthRestart()
{
    lowBwRestartPending_ = false;
    lowBwConsecutiveHits_ = 0;

    if (!pipeline) {
        qDebug() << "[VideoReceiver] onLowBandwidthRestart: no pipeline, skipping";
        return;
    }

    qDebug() << "[VideoReceiver] onLowBandwidthRestart: restarting pipeline due to low bandwidth";

    // Reset bandwidth counters so the warm-up window applies again after restart
    {
        QMutexLocker lk(&bwMutex_);
        bwStats_ = BandwidthStats{};
        lastSampleBytes_ = 0;
    }
    bwStartTimer_.start();             // restart warmup clock immediately
    bwSampleTimer_.invalidate();
    lastBufferTimer_.invalidate();     // stays invalid until first buffer arrives
    probeInstalledOnSource_ = false;

    // Cycle the pipeline to force a fresh RTSP SETUP/PLAY
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_set_state(pipeline, GST_STATE_READY);

    // Re-attach source-setup since probeInstalledOnSource_ was cleared
    // (playbin fires source-setup again when transitioning back to PLAYING)
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[VideoReceiver] onLowBandwidthRestart: PLAYING transition failed";
        emit cameraError(QStringLiteral("Low bandwidth restart failed"));
    } else {
        qDebug() << "[VideoReceiver] onLowBandwidthRestart: pipeline restarted successfully";
    }
}

// ---------------------------------------------------------------------------
// Stall detection — no new buffer for kStallThresholdSeconds
// ---------------------------------------------------------------------------
void VideoReceiver::onStallCheck()
{
    if (!streamReachable_ || !pipeline)
        return;

    // Case 1: Pipeline is running but no buffer has EVER arrived.
    // bwStartTimer_ is started when the pipeline goes to PLAYING (or on
    // first buffer).  If it's valid but lastBufferTimer_ is not, no data
    // has arrived at all since the last (re)start.
    if (bwStartTimer_.isValid() && !lastBufferTimer_.isValid()) {
        double sincePipelineStart = bwStartTimer_.elapsed() / 1000.0;
        if (sincePipelineStart >= kBwWarmupSeconds + kStallThresholdSeconds) {
            if (lowBwRestartPending_) return;
            qDebug() << "[VideoReceiver] Stream never connected: no data for"
                     << sincePipelineStart << "s since pipeline start — triggering restart";
            onLowBandwidthRestart();
        }
        return;
    }

    // Case 2: Data was flowing but has stopped.
    if (!bwStartTimer_.isValid() || !lastBufferTimer_.isValid())
        return;

    double elapsed = bwStartTimer_.elapsed() / 1000.0;
    if (elapsed < kBwWarmupSeconds)
        return;

    double secsSinceLastBuffer = lastBufferTimer_.elapsed() / 1000.0;
    if (secsSinceLastBuffer >= kStallThresholdSeconds) {
        if (lowBwRestartPending_) return;
        qDebug() << "[VideoReceiver] Stream stalled: no data for"
                 << secsSinceLastBuffer << "s — triggering restart";
        onLowBandwidthRestart();
    }
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
                    bool compressorMode = siyiConfig.value("compressorMode").toBool(false);
                    QString siyiIP;
                    if (compressorMode) {
                        siyiIP = siyiConfig.value("videoIP").toString(defaultIP);
                    } else {
                        siyiIP = siyiConfig.value("ip").toString(defaultIP);
                    }
                    int siyiPort = siyiConfig.value("port").toInt(defaultPort);
                    QString siyiPath = siyiConfig.value("path").toString(defaultPath);
                    QString rtspUrl = QString("rtsp://%1:%2%3").arg(siyiIP).arg(siyiPort).arg(siyiPath);
                    qDebug() << "[VIDEO_SOURCE] Parallel SIYI RTSP URL:" << rtspUrl << "(compressor:" << compressorMode << ")";
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
    // Use the videosink's "last-sample" property to grab the current frame.
    // (The old appsink-based approach can't work because appsink is never
    //  created in the playbin pipeline.)
    if (!videosink) {
        qDebug() << "[VideoReceiver] grabFrame: no videosink";
        return {};
    }

    // 1) Get the last rendered sample from the video sink
    GstSample *sample = nullptr;
    g_object_get(videosink, "last-sample", &sample, nullptr);
    if (!sample) {
        qDebug() << "[VideoReceiver] grabFrame: no last-sample available";
        return {};
    }

    // 2) Convert to RGB using GStreamer's built-in converter
    //    This handles NV12, I420, YUY2, BGRx, etc. → RGB in one call.
    GstCaps *rgbCaps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "RGB",
                                            nullptr);

    GError *err = nullptr;
    GstSample *rgbSample = gst_video_convert_sample(sample, rgbCaps,
                                                     GST_SECOND * 5, &err);
    gst_caps_unref(rgbCaps);
    gst_sample_unref(sample);

    if (!rgbSample) {
        qDebug() << "[VideoReceiver] grabFrame: conversion failed:"
                 << (err ? err->message : "unknown error");
        if (err) g_error_free(err);
        return {};
    }

    // 3) Map the converted RGB buffer into a QImage
    GstCaps *convertedCaps = gst_sample_get_caps(rgbSample);
    GstStructure *s = gst_caps_get_structure(convertedCaps, 0);
    int width = 0, height = 0;
    gst_structure_get_int(s, "width",  &width);
    gst_structure_get_int(s, "height", &height);

    GstBuffer *buf = gst_sample_get_buffer(rgbSample);
    GstMapInfo info;
    QImage result;

    if (buf && gst_buffer_map(buf, &info, GST_MAP_READ)) {
        // RGB888: 3 bytes per pixel, stride = width * 3
        QImage img(info.data, width, height, width * 3, QImage::Format_RGB888);
        result = img.copy();   // deep copy before we unmap
        gst_buffer_unmap(buf, &info);
    }

    gst_sample_unref(rgbSample);

    if (result.isNull()) {
        qDebug() << "[VideoReceiver] grabFrame: failed to map RGB buffer";
    } else {
        qDebug() << "[VideoReceiver] grabFrame: captured" << width << "x" << height;
    }

    return result;
}


// void VideoReceiver::setRtspUri(const QString &uri)
// {
//     if (rtspSrc) {
//         g_object_set(rtspSrc, "location", uri.toUtf8().constData(), nullptr);
//     }
// }
void VideoReceiver::setRtspUri(const QString& uri) {
    qDebug() << "[VideoReceiver] setRtspUri called with" << uri;

    // Update cached URI and compressor mode for reconnect
    currentUri_ = uri;
    compressorMode_ = readCompressorModeFromConfig();

    // Stop any in-flight reconnect attempts
    if (reconnectTimer_ && reconnectTimer_->isActive())
        reconnectTimer_->stop();
    if (lowBwRestartTimer_ && lowBwRestartTimer_->isActive())
        lowBwRestartTimer_->stop();
    lowBwRestartPending_ = false;
    lowBwConsecutiveHits_ = 0;
    if (stallCheckTimer_ && stallCheckTimer_->isActive())
        stallCheckTimer_->stop();

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
        // IMPORTANT: Also clear videosink since it was owned by the pipeline
        videosink = nullptr;
        qDebug() << "[VideoReceiver] setRtspUri: old pipeline destroyed";
    }

    // Reset analysis flag and monitoring state for new stream
    analysisPrinted = false;
    videoInfoCollected_ = false;
    videoInfo_ = VideoStreamInfo{};
    {
        QMutexLocker lk(&bwMutex_);
        bwStats_ = BandwidthStats{};
        lastSampleBytes_ = 0;
    }
    bwStartTimer_.invalidate();
    bwSampleTimer_.invalidate();
    lastBufferTimer_.invalidate();
    probeInstalledOnSource_ = false;

    // create a fresh pipeline that uses the persistent videosink
    createPipeline(uri);
}



void VideoReceiver::createPipeline(const QString& uri) {
    qDebug() << "[VideoReceiver] createPipeline: creating playbin for" << uri;

    // make sure we have a persistent videosink to attach the Qt window to
    if (!videosink) {
        // Try different video sinks in order of preference
        const char* sinkNames[] = {"xvimagesink", "glimagesink", "vaapisink", "autovideosink", nullptr};
        const char** sinkName = sinkNames;
        
        while (*sinkName) {
            videosink = gst_element_factory_make(*sinkName, "videosink");
            if (videosink) {
                qDebug() << "[VideoReceiver] createPipeline: created" << *sinkName;
                break;
            }
            sinkName++;
        }
        
        if (!videosink) {
            qCritical() << "[VideoReceiver] createPipeline: failed to create any videosink";
            return;
        }
        
        // Check if the sink supports video overlay
        if (GST_IS_VIDEO_OVERLAY(videosink)) {
            qDebug() << "[VideoReceiver] createPipeline:" << *sinkName << "supports video overlay";
        } else {
            qDebug() << "[VideoReceiver] createPipeline:" << *sinkName << "does NOT support video overlay";
        }
        
        // disable sync so frames show immediately (zero-latency rendering)
        g_object_set(videosink, "sync", FALSE, nullptr);

        // Enable last-sample so grabFrame() can pull the current frame
        g_object_set(videosink, "enable-last-sample", TRUE, nullptr);

        // Enable QoS — lets the sink signal upstream to drop frames if
        // rendering can't keep up, preventing queue buildup.
        GParamSpec* qosSpec = g_object_class_find_property(
            G_OBJECT_GET_CLASS(videosink), "qos");
        if (qosSpec) {
            g_object_set(videosink, "qos", TRUE, nullptr);
        }
    } else {
        // Verify the videosink is still valid
        if (!GST_IS_ELEMENT(videosink)) {
            qWarning() << "[VideoReceiver] createPipeline: existing videosink is invalid, creating new one";
            videosink = nullptr;
            // Recursively call to create a new sink
            createPipeline(uri);
            return;
        }
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
                 "uri",             uri.toUtf8().constData(),
                 "latency",         100,            // 100 ms jitter buffer
                 "buffer-size",     0,              // disable byte-based buffering
                 "buffer-duration", (gint64)0,      // disable time-based buffering
                 "video-sink",      videosink,
                 nullptr);

    // Set playbin flags: video + audio + native-video (skip colorspace
    // conversion when the sink can handle the format natively).
    gint flags = 0;
    g_object_get(pipeline, "flags", &flags, nullptr);
    flags |= (1 | 2 | 32);   // video + audio + native-video
    flags &= ~0x100;          // disable internal buffering (live stream)
    g_object_set(pipeline, "flags", flags, nullptr);

    // If we already have a saved window handle, apply it to the videosink overlay.
    if (savedWindowId != 0 && GST_IS_VIDEO_OVERLAY(videosink)) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videosink),
                                            (guintptr)savedWindowId);
        qDebug() << "[VideoReceiver] createPipeline: applied window handle to videosink";
    } else if (savedWindowId != 0) {
        qDebug() << "[VideoReceiver] createPipeline: videosink does not support video overlay, cannot set window handle";
    }

    // Use source-setup signal to attach probes on the rtspsrc element's
    // dynamic pads.  This measures actual RTP/encoded network bytes
    // (matching RtspAnalyzer's identity-element approach).
    g_signal_connect(pipeline, "source-setup",
                     G_CALLBACK(VideoReceiver::onSourceSetup), this);

    // Attach a bus watch
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, VideoReceiver::bus_call, this);
    gst_object_unref(bus);

    // Start the pipeline
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    qDebug() << "[VideoReceiver] createPipeline: PLAYING for" << uri << " (ret =" << ret << ")";

    // Start the warmup clock and stall detection for this pipeline
    bwStartTimer_.start();
    if (stallCheckTimer_ && !stallCheckTimer_->isActive())
        stallCheckTimer_->start();
}

// ---------------------------------------------------------------------------
// Video Info Collection (matching RtspAnalyzer::collectVideoInfo)
// ---------------------------------------------------------------------------

void VideoReceiver::collectVideoInfoFromPad(GstPad* pad) {
    if (videoInfoCollected_) return;

    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) return;

    GstStructure* s = gst_caps_get_structure(caps, 0);

    // Resolution
    gst_structure_get_int(s, "width", &videoInfo_.width);
    gst_structure_get_int(s, "height", &videoInfo_.height);

    // Framerate
    gint fps_n = 0, fps_d = 1;
    if (gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d) && fps_d != 0) {
        videoInfo_.framerate = static_cast<double>(fps_n) / fps_d;
    }

    // Pixel format
    const gchar* fmt = gst_structure_get_string(s, "format");
    if (fmt) videoInfo_.format = QString::fromUtf8(fmt);

    // Codec — derive from the structure name (e.g. "video/x-raw", "video/x-h264")
    const gchar* structName = gst_structure_get_name(s);
    videoInfo_.codec = QString::fromUtf8(structName);

    // Try to get encoding-name from upstream RTP caps for a better codec name
    // (matching RtspAnalyzer::onPadAdded which extracts encoding-name from application/x-rtp)
    GstPad* peerPad = gst_pad_get_peer(pad);
    if (peerPad) {
        GstCaps* peerCaps = gst_pad_get_current_caps(peerPad);
        if (peerCaps) {
            GstStructure* ps = gst_caps_get_structure(peerCaps, 0);
            const gchar* encoding = gst_structure_get_string(ps, "encoding-name");
            if (encoding) {
                videoInfo_.codec = QString::fromUtf8(encoding);
            }
            gst_caps_unref(peerCaps);
        }
        gst_object_unref(peerPad);
    }

    // Transport protocol — check if playbin is using TCP
    videoInfo_.transport = QStringLiteral("RTP/UDP");
    if (pipeline) {
        GstElement* source = nullptr;
        g_object_get(pipeline, "source", &source, nullptr);
        if (source) {
            // Check if source has a "protocols" property (rtspsrc)
            GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(source), "protocols");
            if (pspec) {
                gint protocols = 0;
                g_object_get(source, "protocols", &protocols, nullptr);
                if (protocols & 0x4) { // GST_RTSP_LOWER_TRANS_TCP
                    videoInfo_.transport = QStringLiteral("RTP/TCP");
                }
            }
            gst_object_unref(source);
        }
    }

    videoInfoCollected_ = true;

    qDebug() << "\n===== Video Stream Info =====";
    qDebug() << "  Codec      :" << videoInfo_.codec;
    qDebug() << "  Resolution :" << videoInfo_.width << "x" << videoInfo_.height;
    qDebug() << "  Framerate  :" << videoInfo_.framerate << "fps";
    qDebug() << "  Format     :" << videoInfo_.format;
    qDebug() << "  Transport  :" << videoInfo_.transport;
    qDebug() << "=============================\n";

    // Emit structured video info signal
    emit videoInfoUpdated(videoInfo_);

    // Also emit the legacy text-based signal for backward compatibility
    QString characteristics;
    characteristics += QString("Codec: %1\n").arg(videoInfo_.codec);
    characteristics += QString("Resolution: %1x%2\n").arg(videoInfo_.width).arg(videoInfo_.height);
    characteristics += QString("Framerate: %1 fps\n").arg(videoInfo_.framerate, 0, 'f', 1);
    characteristics += QString("Format: %1\n").arg(videoInfo_.format);
    characteristics += QString("Transport: %1").arg(videoInfo_.transport);
    emit videoCharacteristicsUpdated(characteristics);

    gst_caps_unref(caps);
}

// ---------------------------------------------------------------------------
// Bandwidth Measurement (matching RtspAnalyzer::updateBandwidth)
// Probes are now on rtspsrc output pads → measures RTP/encoded bytes.
// ---------------------------------------------------------------------------

void VideoReceiver::updateBandwidth(quint64 bufferSize) {
    // Lazy-start the timers on the first buffer received.
    // (Previously timers were started in bus_call on GST_STATE_PLAYING,
    //  which could race with probe installation.)
    if (!bwStartTimer_.isValid()) {
        bwStartTimer_.start();
        bwSampleTimer_.start();
        lastBufferTimer_.start();
        qDebug() << "[VideoReceiver] Bandwidth timers started (first buffer received)";

        // Start stall detection now that we know data is flowing
        if (stallCheckTimer_ && !stallCheckTimer_->isActive()) {
            stallCheckTimer_->start();
        }
    }

    QMutexLocker lk(&bwMutex_);

    bwStats_.total_bytes += bufferSize;
    bwStats_.frame_count++;
    lastBufferTimer_.restart();        // mark that we just saw data

    double totalElapsed = bwStartTimer_.elapsed() / 1000.0;
    bwStats_.elapsed_seconds = totalElapsed;

    // Average bandwidth over the entire session
    if (totalElapsed > 0.0) {
        bwStats_.average_kbps = (bwStats_.total_bytes * 8.0 / 1000.0) / totalElapsed;
    }

    // Instantaneous bandwidth: bytes since last sample / time since last sample
    double dt = bwSampleTimer_.elapsed() / 1000.0;
    if (dt >= 0.5) {  // update instantaneous reading every 0.5 s
        quint64 deltaBytes = bwStats_.total_bytes - lastSampleBytes_;
        bwStats_.current_kbps = (deltaBytes * 8.0 / 1000.0) / dt;

        if (bwStats_.current_kbps > bwStats_.peak_kbps) {
            bwStats_.peak_kbps = bwStats_.current_kbps;
        }

        lastSampleBytes_ = bwStats_.total_bytes;
        bwSampleTimer_.restart();

        // Copy stats before unlocking to emit signals
        BandwidthStats statsCopy = bwStats_;
        double secsSinceLastBuf = lastBufferTimer_.elapsed() / 1000.0;
        quint64 totalBufs = bwStats_.frame_count;
        lk.unlock();

        // Emit bandwidth update
        emit bandwidthUpdated(statsCopy);

        // Emit stream health alongside
        StreamHealthInfo health;
        health.dataFlowing = (secsSinceLastBuf < 2.0);
        health.secondsSinceLastBuffer = secsSinceLastBuf;
        health.totalBuffersReceived = totalBufs;
        emit streamHealthUpdated(health);

        // --- Low-bandwidth auto-restart detection ---
        // Suppressed when the ping watcher says the host is unreachable
        // (restarting the pipeline won't help if the network is down).
        if (statsCopy.elapsed_seconds > kBwWarmupSeconds && streamReachable_) {
            if (statsCopy.current_kbps < kLowBandwidthThresholdKbps) {
                ++lowBwConsecutiveHits_;
                if (lowBwConsecutiveHits_ >= kLowBwConsecutiveRequired
                    && !lowBwRestartPending_) {
                    lowBwRestartPending_ = true;
                    qDebug() << "[VideoReceiver] Sustained low bandwidth:"
                             << statsCopy.current_kbps << "kbps for"
                             << lowBwConsecutiveHits_ << "samples — scheduling restart in"
                             << kLowBwRestartDelayMs << "ms";
                    QMetaObject::invokeMethod(lowBwRestartTimer_, "start",
                                              Qt::QueuedConnection);
                }
            } else {
                // Bandwidth is healthy — reset the consecutive counter
                if (lowBwConsecutiveHits_ > 0) {
                    lowBwConsecutiveHits_ = 0;
                }
                // Cancel any pending restart
                if (lowBwRestartPending_) {
                    lowBwRestartPending_ = false;
                    QMetaObject::invokeMethod(lowBwRestartTimer_, "stop",
                                              Qt::QueuedConnection);
                    qDebug() << "[VideoReceiver] Bandwidth recovered:"
                             << statsCopy.current_kbps << "kbps — restart cancelled";
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// GStreamer Buffer Probe Callback (matching RtspAnalyzer::onBufferProbe)
// Now fires on rtspsrc output pads (raw RTP/encoded network data).
// ---------------------------------------------------------------------------

GstPadProbeReturn VideoReceiver::onBufferProbe(GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data) {
    auto* self = static_cast<VideoReceiver*>(user_data);

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer) {
        self->updateBandwidth(gst_buffer_get_size(buffer));
    }

    return GST_PAD_PROBE_OK;
}

// ---------------------------------------------------------------------------
// playbin source-setup callback — intercept rtspsrc to install RTP probes
// (this is the key change: we measure pre-decode RTP bytes, not decoded pixels)
// ---------------------------------------------------------------------------

void VideoReceiver::onSourceSetup(GstElement* /*playbin*/, GstElement* source, gpointer user_data) {
    auto* self = static_cast<VideoReceiver*>(user_data);

    if (self->probeInstalledOnSource_) {
        return; // already done for this pipeline
    }

    // Store a reference to the rtspsrc so we can query transport later
    self->rtspSrc = source;

    qDebug() << "[VideoReceiver] source-setup: intercepted rtspsrc, tuning for low latency";

    // ── Low-latency rtspsrc tuning ──────────────────────────────────
    // Force TCP interleaved transport — more reliable than UDP on
    // congested / lossy links and avoids firewall issues.
    // GST_RTSP_LOWER_TRANS_TCP = 0x04
    g_object_set(source,
                 "protocols",       0x04,       // TCP only
                 "latency",         (guint)100, // 100 ms jitter buffer inside rtspsrc
                 "drop-on-latency", TRUE,       // drop frames that arrive too late
                 "do-retransmission", FALSE,    // no NACK retransmission (adds latency)
                 "tcp-timeout",     (guint64)5000000, // 5 s TCP timeout (µs)
                 nullptr);

    // Disable NTP clock sync — use pipeline clock for minimum latency
    GParamSpec* ntpSpec = g_object_class_find_property(
        G_OBJECT_GET_CLASS(source), "ntp-sync");
    if (ntpSpec) {
        g_object_set(source, "ntp-sync", FALSE, nullptr);
    }

    // Set buffer-mode to "slave" (4) for low-latency live streams
    GParamSpec* bmSpec = g_object_class_find_property(
        G_OBJECT_GET_CLASS(source), "buffer-mode");
    if (bmSpec) {
        g_object_set(source, "buffer-mode", 4, nullptr); // RTP_JITTER_BUFFER_MODE_SLAVE
    }

    // rtspsrc creates pads dynamically — we attach probes as they appear.
    g_signal_connect(source, "pad-added",
                     G_CALLBACK(VideoReceiver::onSourcePadAdded), user_data);

    self->probeInstalledOnSource_ = true;
}

void VideoReceiver::onSourcePadAdded(GstElement* /*source*/, GstPad* pad, gpointer user_data) {
    auto* self = static_cast<VideoReceiver*>(user_data);

    // Attach a buffer probe on every rtspsrc output pad.
    // These pads carry raw RTP packets before any depayloading/decoding.
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                      VideoReceiver::onBufferProbe, self, nullptr);

    gchar* padName = gst_pad_get_name(pad);
    qDebug() << "[VideoReceiver] source pad-added: installed probe on" << padName;
    g_free(padName);
}
