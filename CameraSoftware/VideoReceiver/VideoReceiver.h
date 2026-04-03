#ifndef VIDEORECEIVER_H
#define VIDEORECEIVER_H

#include <QObject>
#include <QWidget>  // Include this header to define WId
#include <gst/gst.h>
#include <QElapsedTimer>
#include <QMutex>
#include <QString>
#include <QTimer>

struct VideoStreamInfo {
    QString codec;             // e.g. "H264", "H265", "video/x-raw"
    int width = 0;
    int height = 0;
    double framerate = 0.0;
    int bitrate = 0;           // bits per second (from SDP/caps if available)
    QString format;            // pixel format or encoding profile
    QString transport;         // RTP/UDP or RTP/TCP
};

struct BandwidthStats {
    double current_kbps = 0.0;     // instantaneous bandwidth (kbps)
    double average_kbps = 0.0;     // running average bandwidth (kbps)
    double peak_kbps = 0.0;        // peak bandwidth observed (kbps)
    quint64 total_bytes = 0;       // total bytes received
    double elapsed_seconds = 0.0;  // time since monitoring started
    quint64 frame_count = 0;       // total frames received
};

/// Stream health snapshot emitted periodically alongside bandwidth stats.
struct StreamHealthInfo {
    bool dataFlowing = false;         // true if buffers arrived in the last interval
    double secondsSinceLastBuffer = 0.0; // time since the last buffer was seen
    quint64 totalBuffersReceived = 0; // cumulative buffer count
};

class VideoReceiver : public QObject {
    Q_OBJECT

public:
    explicit VideoReceiver(QObject *parent = nullptr);
    ~VideoReceiver();

    void setWindowId(WId id);
    QString getRtspUriFromConfig();
    static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
    static void onPadAdded(GstElement *src, GstPad *new_pad, gpointer user_data);
    void stop();
    bool isPlaying() const;
    void start();
    QImage grabFrame();
    void setRtspUri(const QString &uri);
    void createPipeline(const QString& uri);
    void collectVideoInfoFromPad(GstPad* pad);
    void setCompressorMode(bool enabled);
    bool compressorMode() const;

    // Called by the ping watcher when the camera IP becomes reachable/unreachable.
    // When unreachable: suppresses low-bandwidth restarts (network is down).
    // When reachable again: triggers an immediate pipeline restart.
    void setStreamReachable(bool reachable);

signals:
    void cameraStarted();
    void cameraError(const QString &message);
    void videoCharacteristicsUpdated(const QString &characteristics);
    void videoInfoUpdated(const VideoStreamInfo &info);
    void bandwidthUpdated(const BandwidthStats &stats);
    void streamHealthUpdated(const StreamHealthInfo &info);

private:
    GstElement *pipeline   = nullptr;
    GstElement *convert    = nullptr;
    GstElement *videosink  = nullptr;
    GstElement *appsink    = nullptr;
    GstElement *rtspSrc    = nullptr;
    WId savedWindowId = 0;
    bool analysisPrinted = false;

    // Video stream info (matching RtspAnalyzer::VideoInfo)
    VideoStreamInfo videoInfo_;
    bool videoInfoCollected_ = false;

    // Bandwidth monitoring — probes raw RTP data from rtspsrc (pre-decode)
    mutable QMutex bwMutex_;
    BandwidthStats bwStats_;
    QElapsedTimer bwStartTimer_;
    QElapsedTimer bwSampleTimer_;
    QElapsedTimer lastBufferTimer_;   // tracks time since last buffer for health
    quint64 lastSampleBytes_ = 0;
    bool probeInstalledOnSource_ = false; // prevent double-install

    // Compressor-mode fast reconnect
    bool compressorMode_ = false;
    QTimer *reconnectTimer_ = nullptr;
    QString currentUri_;              // cached RTSP URI for reconnect
    void tryReconnect();
    bool readCompressorModeFromConfig() const;

    // Low-bandwidth auto-restart: triggers pipeline restart when live
    // bandwidth drops below 0.5 Mbps (500 kbps) for several consecutive
    // samples after a generous post-restart grace period.
    QTimer *lowBwRestartTimer_ = nullptr;
    bool lowBwRestartPending_ = false;
    int  lowBwConsecutiveHits_ = 0;                                // consecutive low-bw samples
    bool streamReachable_ = true;                                  // ping watcher reachability
    static constexpr double kLowBandwidthThresholdKbps = 500.0;   // 0.5 Mbps
    static constexpr int    kLowBwRestartDelayMs       = 500;     // restart after 0.5 s
    static constexpr double kBwWarmupSeconds            = 2.0;    // ignore first 2 s after (re)start
    static constexpr int    kLowBwConsecutiveRequired   = 4;      // need 4 consecutive low samples (~2 s)
    void onLowBandwidthRestart();

    // Stall detection: if no new buffer arrives for 4 s the stream is frozen.
    // A periodic 1 s timer checks lastBufferTimer_ and triggers a restart.
    QTimer *stallCheckTimer_ = nullptr;
    static constexpr double kStallThresholdSeconds = 2.0;         // no data for 3 s = stalled
    void onStallCheck();

    void updateBandwidth(quint64 bufferSize);
    static GstPadProbeReturn onBufferProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    // Called by playbin's "source-setup" signal to attach probes to rtspsrc pads
    static void onSourceSetup(GstElement* playbin, GstElement* source, gpointer user_data);
    static void onSourcePadAdded(GstElement* source, GstPad* pad, gpointer user_data);
};

#endif // VIDEORECEIVER_H
