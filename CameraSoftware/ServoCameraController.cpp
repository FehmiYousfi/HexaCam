// ServoCameraController.cpp
#include "ServoCameraController.h"
#include <QDebug>

ServoCameraController::ServoCameraController(const std::string &ip, int port)
{
    client = std::make_unique<ServoControl::ServoClient>(ip, port, 2000);
}

ServoCameraController::~ServoCameraController()
{
    stop();
}

bool ServoCameraController::start()
{
    if (running.load()) return true;

    // Try to connect; connect is allowed to be blocking briefly
    {
        std::lock_guard<std::mutex> lg(clientMutex);
        if (!client) return false;
        if (!client->connect()) {
            qWarning() << "[ServoCameraController] connect failed:" << QString::fromStdString(client->getLastError());
            return false;
        }
    }

    running.store(true);
    receiveThread = std::thread([this]() {
        // Example receive loop: poll until running == false.
        while (running.load()) {
            // read status or handle keep-alive; avoid blocking forever
            // use client->poll or similar with timeout; if none exists, use sleeps shorter
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                // If your ServoClient has a receiver method, call it with nonblocking behavior
            } catch (...) {
                // swallow exceptions; ensure thread can end
            }
        }

        // On thread exit, ensure client disconnected
        {
            std::lock_guard<std::mutex> lg(clientMutex);
            if (client && client->isConnected())
                client->disconnect();
        }
    });

    return true;
}

void ServoCameraController::stop()
{
    // 1) set flag
    running.store(false);

    // 2) join the thread
    if (receiveThread.joinable()) {
        receiveThread.join();
    }

    // 3) ensure client disconnected and freed
    {
        std::lock_guard<std::mutex> lg(clientMutex);
        if (client && client->isConnected()) {
            client->disconnect();
        }
        client.reset();
    }
}

bool ServoCameraController::isRunning() const {
    return running.load();
}
// This depends on how your servo API accepts positions vs speeds.
// Example: convert yaw/pitch speed to servo position commands or call ServoWorker
bool ServoCameraController::setGimbalSpeed(int yaw, int pitch)
{
    if (!client || !connected) return false;
    // For simple approach: map yaw/pitch speed into a relative movement call or position
    // Here we use a placeholder: client->setPosition(...);
    // Replace with actual API calls that your ServoClient provides.
    return true;
}

void ServoCameraController::setRtspUri(const QString &uri) {
    Q_UNUSED(uri);
    // servo controller typically not responsible for RTSP, keep no-op or forward to video logic
}

bool ServoCameraController::setGimbalPosition(int yawPos, int pitchPos)
{
    if (!client || !isRunning()) return false;
    // Map absolute yaw/pitch to servo positions if you have an API.
    // Placeholder:
    qDebug() << "[ServoCameraController] Setting absolute position ("
             << yawPos << "," << pitchPos << ")";
    // If your ServoClient has a method name, call it here, e.g.:
    // return client->setPosition(yawPos, pitchPos);
    return true; // or false if not supported
}

bool ServoCameraController::setAbsoluteZoom(float zoomLevel, int speed)
{
    Q_UNUSED(zoomLevel);
    Q_UNUSED(speed);
    // If the servo camera supports zoom via a command, call it here (e.g. client->setZoom(...)).
    // For now, implement as a no-op that returns true to indicate "handled".
    qDebug() << "[ServoCameraController] setAbsoluteZoom called (no-op)";
    return true;
}

bool ServoCameraController::supportsAbsolutePosition() const {
    return true;
}
