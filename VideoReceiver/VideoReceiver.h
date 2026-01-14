#ifndef VIDEORECEIVER_H
#define VIDEORECEIVER_H

#include <QObject>
#include <QWidget>  // Include this header to define WId
#include <gst/gst.h>

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

signals:
    void cameraStarted();
    void cameraError(const QString &message);

private:
    GstElement *pipeline   = nullptr;
    GstElement *convert    = nullptr;
    GstElement *videosink  = nullptr;
    GstElement *appsink    = nullptr;
    GstElement *rtspSrc    = nullptr;
    WId savedWindowId = 0;
};

#endif // VIDEORECEIVER_H
