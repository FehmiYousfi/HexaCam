#ifndef VIDEORECORDERWIDGET_H
#define VIDEORECORDERWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QApplication>
#include <QShowEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include "VideoReceiver.h"

class VideoRecorderWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoRecorderWidget(QWidget *parent = nullptr);
    ~VideoRecorderWidget();
    void stopPipeline();
    VideoReceiver* getReceiver() const { return receiver; }


protected:
    void showEvent(QShowEvent *event) override ;
    void resizeEvent(QResizeEvent *event) override;

    // Forward every key press to the MainWindow
    void keyPressEvent(QKeyEvent *event) override {
        if (parentWidget())
            QCoreApplication::sendEvent(parentWidget(), event);
    }
    void keyReleaseEvent(QKeyEvent *event) override {
        if (parentWidget())
            QCoreApplication::sendEvent(parentWidget(), event);
    }

private:
    VideoReceiver *receiver;
    QLabel        *overlayLabel;
};

#endif // VIDEORECORDERWIDGET_H

