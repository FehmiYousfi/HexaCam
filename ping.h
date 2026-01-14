#ifndef PING_H
#define PING_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtCore/QMap>
#include <QtCore/QDateTime>
#include <QtCore/QtGlobal>
#include <QtCore/QWaitCondition>
#include <memory>
#include <climits>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <netinet/ip_icmp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    #include <cstring>
#endif

struct PingResult {
    bool success;
    int roundTripTime;  // in milliseconds
    QString errorMessage;
    
    PingResult(bool s, int rtt, const QString& error = QString()) 
        : success(s), roundTripTime(rtt), errorMessage(error) {}
};

struct HostConnectivityScore {
    QString hostName;
    QString hostAddress;
    
    // Current status
    bool isReachable;
    int currentRtt;
    QString lastError;
    
    // Historical data for scoring
    int totalPings;
    int successfulPings;
    int consecutiveFailures;
    int consecutiveSuccesses;
    int averageRtt;
    int minRtt;
    int maxRtt;
    qint64 lastSeen;
    qint64 firstSeen;
    
    // Calculated scores (0-100)
    int reliabilityScore;    // Based on success rate
    int performanceScore;    // Based on RTT consistency
    int stabilityScore;      // Based on consecutive success/failure
    int overallScore;        // Weighted combination
    
    HostConnectivityScore() {
        reset();
    }
    
    void reset() {
        isReachable = false;
        currentRtt = -1;
        lastError.clear();
        totalPings = 0;
        successfulPings = 0;
        consecutiveFailures = 0;
        consecutiveSuccesses = 0;
        averageRtt = 0;
        minRtt = INT_MAX;
        maxRtt = 0;
        lastSeen = 0;
        firstSeen = 0;
        reliabilityScore = 0;
        performanceScore = 0;
        stabilityScore = 0;
        overallScore = 0;
    }
    
    void updatePing(bool success, int rtt, const QString& error = QString()) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        
        if (firstSeen == 0) {
            firstSeen = now;
        }
        lastSeen = now;
        
        isReachable = success;
        currentRtt = rtt;
        lastError = error;
        
        totalPings++;
        
        if (success) {
            successfulPings++;
            consecutiveSuccesses++;
            consecutiveFailures = 0;
            
            if (rtt >= 0) {
                if (minRtt == INT_MAX) minRtt = rtt;
                if (rtt < minRtt) minRtt = rtt;
                if (rtt > maxRtt) maxRtt = rtt;
                
                // Calculate running average
                averageRtt = ((averageRtt * (successfulPings - 1)) + rtt) / successfulPings;
            }
        } else {
            consecutiveFailures++;
            consecutiveSuccesses = 0;
        }
        
        calculateScores();
    }
    
private:
    void calculateScores() {
        // Use the new RTT-based scoring system
        if (totalPings > 0 && successfulPings > 0) {
            // Base score from average RTT (calculated by Ping::calculateScore)
            if (averageRtt <= 5) overallScore = 100;        // Excellent - <5ms
            else if (averageRtt <= 10) overallScore = 95;   // Very Good - 5-10ms
            else if (averageRtt <= 20) overallScore = 85;   // Good - 10-20ms
            else if (averageRtt <= 35) overallScore = 75;   // Fair - 20-35ms
            else if (averageRtt <= 50) overallScore = 65;   // Acceptable - 35-50ms
            else if (averageRtt <= 75) overallScore = 50;   // Poor - 50-75ms
            else if (averageRtt <= 100) overallScore = 35;  // Very Poor - 75-100ms
            else if (averageRtt <= 150) overallScore = 20;  // Critical - 100-150ms
            else if (averageRtt <= 200) overallScore = 10;  // Severe - 150-200ms
            else overallScore = 5;                           // Unusable - >200ms
            
            // Apply packet loss penalty
            double packetLossPercent = ((double)(totalPings - successfulPings) / totalPings) * 100.0;
            if (packetLossPercent > 0) {
                if (packetLossPercent <= 10) overallScore -= 5;      // Minor loss
                else if (packetLossPercent <= 25) overallScore -= 10; // Moderate loss
                else if (packetLossPercent <= 50) overallScore -= 15; // Significant loss
                else if (packetLossPercent <= 75) overallScore -= 25; // Severe loss
                else overallScore -= 30;                              // Critical loss
            }
            
            // Apply consecutive failure penalty
            if (consecutiveFailures >= 3) {
                overallScore -= qMin(20, consecutiveFailures * 5);
            }
            
            // Apply consecutive success bonus
            if (consecutiveSuccesses >= 5) {
                overallScore += qMin(10, consecutiveSuccesses / 2);
            }
            
            overallScore = qBound(0, overallScore, 100);
            
            // Set individual scores for compatibility
            reliabilityScore = (successfulPings * 100) / totalPings;
            performanceScore = overallScore;
            stabilityScore = consecutiveSuccesses >= 3 ? 100 : 
                           (consecutiveFailures >= 3 ? 0 : 50);
        } else {
            reliabilityScore = 0;
            performanceScore = 0;
            stabilityScore = 0;
            overallScore = 0;
        }
    }
};

class Ping : public QObject {
    Q_OBJECT

public:
    explicit Ping(QObject *parent = nullptr);
    ~Ping();

    struct PingResult {
        bool success;
        int roundTripTime;
        QString errorMessage;
        int packetsTransmitted;
        int packetsReceived;
        double packetLoss;
        QString statistics;
        
        PingResult() : success(false), roundTripTime(-1), packetsTransmitted(0), packetsReceived(0), packetLoss(100.0) {}
        PingResult(bool success, int rtt, const QString& error = QString(), 
                  int transmitted = 1, int received = 0, double loss = 100.0, 
                  const QString& stats = QString()) 
            : success(success), roundTripTime(rtt), errorMessage(error), 
              packetsTransmitted(transmitted), packetsReceived(received), 
              packetLoss(loss), statistics(stats) {}
    };

    PingResult pingHost(const QString& host, int timeoutMs = 1000);
    PingResult pingHostWithStats(const QString& host, int count = 3, int timeoutMs = 1000);

private:
    PingResult parsePingOutput(const QString& output, int exitCode);
    int calculateScore(const PingResult& result);
};

class PingWorker : public QObject {
    Q_OBJECT

public:
    explicit PingWorker(QObject *parent = nullptr);
    ~PingWorker();

public slots:
    void pingHost(const QString& host, int timeoutMs, int requestId);

signals:
    void pingResult(int requestId, const QString& host, bool success, int roundTripTime, const QString& error);

private:
    Ping* pingEngine;
};

class ContinuousPingWatcher : public QObject {
    Q_OBJECT

public:
    explicit ContinuousPingWatcher(QObject *parent = nullptr);
    ~ContinuousPingWatcher();

    void addHost(const QString& name, const QString& host);
    void removeHost(const QString& name);
    void setPingInterval(int intervalMs);
    void setTimeout(int timeoutMs);
    void startWatching();
    void stopWatching();
    bool isWatching() const { return watching; }
    HostConnectivityScore getConnectivityScore(const QString& name) const;

signals:
    void hostStatusChanged(const QString& name, bool reachable, int roundTripTime);
    void hostError(const QString& name, const QString& error);
    void connectivityScoreUpdated(const QString& name, const HostConnectivityScore& score);

private slots:
    void performPingCycle();
    void onPingResult(int requestId, const QString& host, bool success, int roundTripTime, const QString& error);

private:
    struct HostInfo {
        QString name;
        QString host;
        bool lastStatus;
        int lastRtt;
        int pendingRequestId;
        HostConnectivityScore score;
            
        HostInfo() : lastStatus(false), lastRtt(-1), pendingRequestId(-1) {}
        HostInfo(const QString& n, const QString& h) 
            : name(n), host(h), lastStatus(false), lastRtt(-1), pendingRequestId(-1) {
            score.hostName = n;
            score.hostAddress = h;
        }
    };

    QMap<QString, HostInfo> hosts;
    mutable QMutex hostsMutex;
    QTimer* pingTimer;
    QThread* workerThread;
    PingWorker* pingWorker;
    int pingInterval;
    int pingTimeout;
    bool watching;
    int nextRequestId;
};

#endif // PING_H
