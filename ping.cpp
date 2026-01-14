#include "ping.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QDebug>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QHostInfo>

Ping::Ping(QObject *parent) : QObject(parent) {
}

Ping::~Ping() {
}

Ping::PingResult Ping::pingHost(const QString& host, int timeoutMs) {
    return pingHostWithStats(host, 1, timeoutMs);
}

Ping::PingResult Ping::pingHostWithStats(const QString& host, int count, int timeoutMs) {
    QProcess pingProcess;
    QStringList arguments;
    
    // Build ping command based on platform
#ifdef _WIN32
    arguments << "-n" << QString::number(count) << "-w" << QString::number(timeoutMs) << host;
#else
    arguments << "-c" << QString::number(count) 
              << "-W" << QString::number(timeoutMs / 1000.0, 'f', 1) 
              << host;
#endif
    
    pingProcess.start("ping", arguments);
    
    if (!pingProcess.waitForStarted(1000)) {
        return PingResult(false, -1, "Failed to start ping process");
    }
    
    if (!pingProcess.waitForFinished(timeoutMs * count + 2000)) {
        pingProcess.kill();
        pingProcess.waitForFinished(1000);
        return PingResult(false, -1, "Ping process timed out");
    }
    
    int exitCode = pingProcess.exitCode();
    QString output = pingProcess.readAllStandardOutput();
    QString errorOutput = pingProcess.readAllStandardError();
    
    return parsePingOutput(output, exitCode);
}

Ping::PingResult Ping::parsePingOutput(const QString& output, int exitCode) {
    PingResult result;
    
    if (exitCode != 0) {
        result.success = false;
        result.errorMessage = "Ping failed";
        return result;
    }
    
    result.success = true;
    result.statistics = output;
    
    // Parse statistics from ping output
    QStringList lines = output.split('\n');
    
    // Parse packet loss and transmission statistics
    QRegularExpression packetLossRegex;
    QRegularExpression rttRegex;
    
#ifdef _WIN32
    packetLossRegex.setPattern(R"((\d+) packets transmitted, (\d+) received, (\d+)% packet loss)");
    rttRegex.setPattern(R"(Minimum = (\d+)ms, Maximum = (\d+)ms, Average = (\d+)ms)");
#else
    packetLossRegex.setPattern(R"((\d+) packets transmitted, (\d+) received, (\d+)% packet loss)");
    rttRegex.setPattern(R"(rtt min/avg/max/mdev = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+) ms)");
#endif
    
    // Extract packet statistics
    for (const QString& line : lines) {
        QRegularExpressionMatch match = packetLossRegex.match(line);
        if (match.hasMatch()) {
            result.packetsTransmitted = match.captured(1).toInt();
            result.packetsReceived = match.captured(2).toInt();
            result.packetLoss = match.captured(3).toDouble();
            break;
        }
    }
    
    // Extract RTT statistics
    for (const QString& line : lines) {
        QRegularExpressionMatch match = rttRegex.match(line);
        if (match.hasMatch()) {
#ifdef _WIN32
            result.roundTripTime = match.captured(3).toInt(); // Average
#else
            result.roundTripTime = static_cast<int>(match.captured(2).toFloat()); // Average
#endif
            break;
        }
    }
    
    // If we couldn't parse RTT from summary, try to get it from individual ping lines
    if (result.roundTripTime == -1) {
        QRegularExpression individualRttRegex;
#ifdef _WIN32
        individualRttRegex.setPattern(R"(time[=<](\d+)ms)");
#else
        individualRttRegex.setPattern(R"(time[=<](\d+\.?\d*)\s*ms)");
#endif
        
        QList<int> rttValues;
        for (const QString& line : lines) {
            QRegularExpressionMatch match = individualRttRegex.match(line);
            if (match.hasMatch()) {
                rttValues.append(static_cast<int>(match.captured(1).toFloat()));
            }
        }
        
        if (!rttValues.isEmpty()) {
            // Calculate average of all RTT values
            int sum = 0;
            for (int rtt : rttValues) {
                sum += rtt;
            }
            result.roundTripTime = sum / rttValues.size();
        }
    }
    
    // Set default values if parsing failed
    if (result.packetsTransmitted == 0) {
        result.packetsTransmitted = 1;
    }
    if (result.packetsReceived == 0 && result.success) {
        result.packetsReceived = 1;
    }
    if (result.roundTripTime == -1 && result.success) {
        result.roundTripTime = 0;
    }
    
    return result;
}

int Ping::calculateScore(const PingResult& result) {
    if (!result.success || result.packetsReceived == 0) {
        return 0;
    }
    
    // Primary scoring based on RTT (0-100 points) - lower RTT = higher score
    int rttScore = 0;
    if (result.roundTripTime >= 0) {
        if (result.roundTripTime <= 5) rttScore = 100;        // Excellent - <5ms
        else if (result.roundTripTime <= 10) rttScore = 95;   // Very Good - 5-10ms
        else if (result.roundTripTime <= 20) rttScore = 85;   // Good - 10-20ms
        else if (result.roundTripTime <= 100) rttScore = 75;   // Fair - 20-35ms
        else if (result.roundTripTime <= 250) rttScore = 65;   // Acceptable - 35-50ms
        else if (result.roundTripTime <= 350) rttScore = 50;   // Poor - 50-75ms
        else if (result.roundTripTime <= 500) rttScore = 35;  // Very Poor - 75-100ms
        else if (result.roundTripTime <= 1000) rttScore = 20;  // Critical - 100-150ms
        else if (result.roundTripTime <= 2000) rttScore = 10;  // Severe - 150-200ms
        else rttScore = 5;                                   // Unusable - >200ms
    }
    
    // Secondary scoring based on packet loss (0-30 points deduction)
    int packetLossPenalty = 0;
    if (result.packetLoss > 0) {
        if (result.packetLoss <= 10) packetLossPenalty = 5;      // Minor loss
        else if (result.packetLoss <= 25) packetLossPenalty = 10; // Moderate loss
        else if (result.packetLoss <= 50) packetLossPenalty = 15; // Significant loss
        else if (result.packetLoss <= 75) packetLossPenalty = 25; // Severe loss
        else packetLossPenalty = 30;                              // Critical loss
    }
    
    // Reliability bonus based on consecutive successful pings (0-10 points bonus)
    // This will be handled by the HostConnectivityScore updatePing method
    
    // Final score: RTT-based score minus packet loss penalty
    int finalScore = qMax(0, rttScore - packetLossPenalty);
    
    return qBound(0, finalScore, 100);
}

// PingWorker implementation - runs in separate thread
PingWorker::PingWorker(QObject *parent) : QObject(parent), pingEngine(new Ping(this)) {
}

PingWorker::~PingWorker() {
    delete pingEngine;
}

void PingWorker::pingHost(const QString& host, int timeoutMs, int requestId) {
    // This runs in the worker thread and won't block the main thread
    Ping::PingResult result = pingEngine->pingHost(host, timeoutMs);
    
    emit pingResult(requestId, host, result.success, result.roundTripTime, result.errorMessage);
}

// ContinuousPingWatcher implementation
ContinuousPingWatcher::ContinuousPingWatcher(QObject *parent)
    : QObject(parent), pingTimer(new QTimer(this)), workerThread(new QThread(this)),
      pingWorker(nullptr), pingInterval(3000), pingTimeout(1000), watching(false), nextRequestId(0) {
    
    connect(pingTimer, &QTimer::timeout, this, &ContinuousPingWatcher::performPingCycle);
    
    // Set up worker thread
    pingWorker = new PingWorker();
    pingWorker->moveToThread(workerThread);
    
    connect(pingWorker, &PingWorker::pingResult, this, &ContinuousPingWatcher::onPingResult);
    connect(workerThread, &QThread::started, []() {
        qDebug() << "[PING_WATCHER] Worker thread started";
    });
    
    workerThread->start();
}

ContinuousPingWatcher::~ContinuousPingWatcher() {
    stopWatching();
    
    if (pingWorker) {
        pingWorker->deleteLater();
        pingWorker = nullptr;
    }
    
    workerThread->quit();
    workerThread->wait(5000);
}

void ContinuousPingWatcher::addHost(const QString& name, const QString& host) {
    QMutexLocker locker(&hostsMutex);
    
    if (hosts.contains(name)) {
        // Update existing host
        hosts[name].host = host;
        hosts[name].score.reset();
        hosts[name].lastStatus = false;
        hosts[name].lastRtt = -1;
        hosts[name].pendingRequestId = -1;
    } else {
        // Add new host
        HostInfo hostInfo;
        hostInfo.name = name;
        hostInfo.host = host;
        hostInfo.score.reset();
        hostInfo.lastStatus = false;
        hostInfo.lastRtt = -1;
        hostInfo.pendingRequestId = -1;
        hosts[name] = hostInfo;
    }
    
    qDebug() << "[PING_WATCHER] Added host" << name << "at" << host;
}

void ContinuousPingWatcher::removeHost(const QString& name) {
    QMutexLocker locker(&hostsMutex);
    
    if (hosts.remove(name)) {
        qDebug() << "[PING_WATCHER] Removed host" << name;
    }
}

void ContinuousPingWatcher::setPingInterval(int intervalMs) {
    pingInterval = intervalMs;
    
    if (watching) {
        pingTimer->start(pingInterval);
    }
}

void ContinuousPingWatcher::setTimeout(int timeoutMs) {
    pingTimeout = timeoutMs;
}

void ContinuousPingWatcher::startWatching() {
    if (watching) {
        return;
    }
    
    watching = true;
    pingTimer->start(pingInterval);
    qDebug() << "[PING_WATCHER] Started continuous monitoring in background thread";
    qDebug() << "[PING_WATCHER] Main thread ID:" << QThread::currentThreadId();
    qDebug() << "[PING_WATCHER] Worker thread ID:" << workerThread->thread();
}

void ContinuousPingWatcher::stopWatching() {
    if (!watching) {
        return;
    }
    
    watching = false;
    pingTimer->stop();
    qDebug() << "[PING_WATCHER] Stopped continuous monitoring";
}

HostConnectivityScore ContinuousPingWatcher::getConnectivityScore(const QString& name) const {
    if (hosts.contains(name)) {
        return hosts[name].score;
    }
    return HostConnectivityScore(); // Return empty score if host not found
}

void ContinuousPingWatcher::performPingCycle() {
    // Queue all pings to be executed in the worker thread
    for (auto it = hosts.begin(); it != hosts.end(); ++it) {
        QString hostName = it.key();
        QString hostAddress = it.value().host;
        int requestId = ++nextRequestId;
        
        // Update pending request ID
        {
            QMutexLocker locker(&hostsMutex);
            if (hosts.contains(hostName)) {
                hosts[hostName].pendingRequestId = requestId;
            }
        }
        
        // Queue ping in worker thread
        QMetaObject::invokeMethod(pingWorker, "pingHost", Qt::QueuedConnection,
                                  Q_ARG(QString, hostAddress),
                                  Q_ARG(int, pingTimeout),
                                  Q_ARG(int, requestId));
        
        qDebug() << "[PING_WATCHER] Queued ping for" << hostName << "with ID" << requestId;
    }
}

void ContinuousPingWatcher::onPingResult(int requestId, const QString& host, bool success, int roundTripTime, const QString& error) {
    QMutexLocker locker(&hostsMutex);
    
    // Find the host with matching request ID
    for (auto it = hosts.begin(); it != hosts.end(); ++it) {
        HostInfo& hostInfo = it.value();
        
        if (hostInfo.pendingRequestId == requestId) {
            bool statusChanged = (hostInfo.lastStatus != success);
            hostInfo.lastStatus = success;
            hostInfo.lastRtt = roundTripTime;
            hostInfo.pendingRequestId = -1; // Clear pending request
            
            // Update connectivity score
            hostInfo.score.updatePing(success, roundTripTime, error);
            
            // Emit signals
            emit hostStatusChanged(hostInfo.name, success, roundTripTime);
            
            if (statusChanged) {
                qDebug() << "[PING_WATCHER]" << hostInfo.name << "camera status:" 
                         << (success ? "REACHABLE" : "UNREACHABLE") << "RTT:" << roundTripTime << "ms";
                emit hostError(hostInfo.name, success ? "" : "Host unreachable");
            }
            
            emit connectivityScoreUpdated(hostInfo.name, hostInfo.score);
            
            qDebug() << "[PING_WATCHER] Ping result for" << hostInfo.name << ":" 
                     << (success ? "SUCCESS" : "FAILED") << "ID:" << requestId
                     << "Score:" << hostInfo.score.overallScore;
            break;
        }
    }
}
