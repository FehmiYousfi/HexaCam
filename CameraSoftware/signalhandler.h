#ifndef SIGNALHANDLER_H
#define SIGNALHANDLER_H

#include <QObject>
#include <QSocketNotifier>

class SignalHandler : public QObject {
    Q_OBJECT
public:
    explicit SignalHandler(QObject *parent = nullptr);
    ~SignalHandler();

private slots:
    void handleSignal();

private:
    static int sigintFd[2];
    QSocketNotifier *sn;
};

#endif // SIGNALHANDLER_H
