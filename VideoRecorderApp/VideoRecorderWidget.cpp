#include "VideoRecorderWidget.h"
#include <QVBoxLayout>
#include <QDebug>

// VideoRecorderWidget::VideoRecorderWidget(QWidget *parent)
//     : QWidget(parent)
// {
//     setFocusPolicy(Qt::StrongFocus);
//     QVBoxLayout *layout = new QVBoxLayout(this);
//     layout->setContentsMargins(0, 0, 0, 0);
//     // Create the VideoReceiver object.
//     receiver = new VideoReceiver(this);
//     // (Optionally, add a placeholder widget or set a background color here.)
//     setLayout(layout);
//     qDebug() << "VideoRecorderWidget initialized";
// }

// VideoRecorderWidget::~VideoRecorderWidget() {
//     delete receiver;
// }

// void VideoRecorderWidget::showEvent(QShowEvent *event) {
//     QWidget::showEvent(event);
//     setFocus();
//     if (receiver) {
//         // This sets the GStreamer videosink window to display inside this widget.
//         receiver->setWindowId(winId());
//         qDebug() << "VideoReceiver window handle set to" << winId();
//     }
// }

// void VideoRecorderWidget::stopPipeline() {
//   if (receiver) {
//     receiver->stop();
//   }
// }

VideoRecorderWidget::VideoRecorderWidget(QWidget *parent)
    : QWidget(parent),
    receiver(new VideoReceiver(this))
{
    // no layout child for the raw video—the video sinks straight into our winId()

    // 1) create the “Signal Lost” overlay
    overlayLabel = new QLabel(this);
    overlayLabel->setText("🚫 Signal Lost !!!");
    overlayLabel->setAlignment(Qt::AlignCenter);
    overlayLabel->setStyleSheet(R"(
        background-color: rgba(0, 0, 0, 0.75);
        color: white;
        font-size: 24pt;
        font-weight: bold;
    )");
    overlayLabel->hide();

    // 2) whenever the receiver errors out, show it…
    connect(receiver, &VideoReceiver::cameraError, this, [this](const QString&){
        overlayLabel->show();
    });
    // 3) …and whenever we start again, hide it
    connect(receiver, &VideoReceiver::cameraStarted, this, [this](){
        overlayLabel->hide();
    });

    setFocusPolicy(Qt::StrongFocus);
    qDebug() << "VideoRecorderWidget initialized";
}

VideoRecorderWidget::~VideoRecorderWidget() {
    delete receiver;
}

void VideoRecorderWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // embed the pipeline’s sink into us
    receiver->setWindowId(winId());
    qDebug() << "VideoReceiver window handle set to" << winId();
}

void VideoRecorderWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // keep the overlay filling exactly our client area
    overlayLabel->setGeometry(rect());
}

void VideoRecorderWidget::stopPipeline() {
    if (receiver) receiver->stop();
}
