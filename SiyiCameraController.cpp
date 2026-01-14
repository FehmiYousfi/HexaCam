#include "SiyiCameraController.h"
#include <QDebug>
#include <chrono>
#include <thread>

static constexpr int STOP_WAIT_MS = 300;
static constexpr int STOP_POLL_MS = 10;

SiyiCameraController::SiyiCameraController(const std::string &ip, int port)
    : sdkIp_(ip), sdkPort_(port)
{
    qDebug() << "[SiyiCameraController] created with ip:" << QString::fromStdString(ip)
    << " port:" << port;
}

SiyiCameraController::~SiyiCameraController() {
    stop();
}

void SiyiCameraController::parseRtsp(const QString &uri, std::string &outIp, int &outPort)
{
    // naive but robust parse for "rtsp://<ip>[:port][/<path>]"
    outIp.clear();
    outPort = 37260; // fallback (sdk default)
    QString s = uri.trimmed();
    if (!s.startsWith("rtsp://", Qt::CaseInsensitive)) return;
    s = s.mid(7); // strip rtsp://
    int slash = s.indexOf('/');
    QString hostport = (slash >= 0) ? s.left(slash) : s;
    int colon = hostport.indexOf(':');
    if (colon >= 0) {
        outIp = hostport.left(colon).toStdString();
        bool ok = false;
        int p = hostport.mid(colon+1).toInt(&ok);
        if (ok && p>0) outPort = p;
    } else {
        outIp = hostport.toStdString();
    }
    if (outIp.empty()) {
        // fallback ip: maybe SDK device uses different default; leave as-is
        outIp = std::string("10.14.11.3");
    }
}

bool SiyiCameraController::start()
{
    std::lock_guard<std::mutex> lk(lifeMutex);

    if (running.load()) {
        qDebug() << "[SiyiCameraController] start() called but already running";
        return true;
    }

    qDebug() << "[SiyiCameraController] creating SIYI_SDK instance at"
             << QString::fromStdString(sdkIp_) << sdkPort_;

    try {
        sdkPtr = std::make_shared<SIYI_SDK>(sdkIp_.c_str(), sdkPort_);

        // Test connection with a simple command
        if (!sdkPtr->request_firmware_version()) {
            qWarning() << "[SiyiCameraController] Failed to communicate with camera";
            sdkPtr.reset();
            return false;
        }

    } catch (const std::exception& e) {
        qWarning() << "[SiyiCameraController] Exception creating SIYI_SDK:" << e.what();
        sdkPtr.reset();
        return false;
    } catch (...) {
        qWarning() << "[SiyiCameraController] Unknown exception creating SIYI_SDK";
        sdkPtr.reset();
        return false;
    }

    // Rest of your existing start code...
    sdkLoopFlagPtr = std::make_shared<bool>(true);
    threadExited.store(false);

    auto capturedSdk = sdkPtr;
    auto capturedFlag = sdkLoopFlagPtr;
    receiveThread = std::thread([capturedSdk, capturedFlag, this]() {
        qDebug() << "[SiyiCameraController] receive thread started";
        if (capturedSdk && capturedFlag) {
            try {
                capturedSdk->receive_message_loop(*capturedFlag);
            } catch (...) {
                qWarning() << "[SiyiCameraController] receive_message_loop threw";
            }
        }
        qDebug() << "[SiyiCameraController] receive thread exiting";
        threadExited.store(true);
    });

    // initial polite queries with error checking
    if (sdkPtr) {
        if (!sdkPtr->request_follow_mode()) {
            qWarning() << "[SiyiCameraController] Failed to set follow mode";
        }
        if (!sdkPtr->request_firmware_version()) {
            qWarning() << "[SiyiCameraController] Failed to request firmware version";
        }
        if (!sdkPtr->request_gimbal_center()) {
            qWarning() << "[SiyiCameraController] Failed to request gimbal center";
        }
        if (!sdkPtr->request_autofocus()) {
            qWarning() << "[SiyiCameraController] Failed to request autofocus";
        }
    }

    running.store(true);
    qDebug() << "[SiyiCameraController] started successfully";
    return true;
}


void SiyiCameraController::stop()
{
    std::lock_guard<std::mutex> lk(lifeMutex);
    qDebug() << "[SiyiCameraController] stop() enter";

    if (sdkLoopFlagPtr) {
        *sdkLoopFlagPtr = false;
    }

    // rename 'start' -> 'tstart' to avoid potential shadowing
    auto tstart = std::chrono::steady_clock::now();
    bool joined = false;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - tstart).count() < STOP_WAIT_MS)
    {
        if (threadExited.load()) {
            if (receiveThread.joinable()) {
                try {
                    receiveThread.join();
                } catch (const std::system_error &e) {
                    qWarning() << "[SiyiCameraController] join threw:" << e.what();
                }
            }
            joined = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(STOP_POLL_MS));
    }

    if (!joined && receiveThread.joinable()) {
        qWarning() << "[SiyiCameraController] thread didn't exit quickly — detaching";
        receiveThread.detach();
    }

    sdkPtr.reset();
    sdkLoopFlagPtr.reset();
    running.store(false);
    qDebug() << "[SiyiCameraController] stop() exit";
}


bool SiyiCameraController::isRunning() const
{
    return running.load();
}

bool SiyiCameraController::setGimbalSpeed(int yawSpeed, int pitchSpeed)
{
    auto sp = sdkPtr;
    if (!sp) {
        qWarning() << "[SiyiCameraController] setGimbalSpeed: not connected";
        return false;
    }

    // If we're about to send a non-zero command, ensure the camera is in follow mode.
    // Use a small flag so we only hammer the follow request when needed.
    static std::atomic<bool> follow_mode_asserted{false};

    if ((yawSpeed != 0 || pitchSpeed != 0) && !follow_mode_asserted.load()) {
        qDebug() << "[SiyiCameraController] asserting follow mode before movement";
        sp->request_follow_mode();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        follow_mode_asserted.store(true);
    }

    qDebug() << "[SiyiCameraController] set_gimbal_speed(" << yawSpeed << "," << pitchSpeed << ")";
    bool ok = sp->set_gimbal_speed(yawSpeed, pitchSpeed);

    qDebug() << "[SiyiCameraController] set_gimbal_speed() returned:" << (ok ? "OK" : "FAIL");

    if (!ok) {
        // try to re-assert follow mode then retry once
        qWarning() << "[SiyiCameraController] set_gimbal_speed failed — retrying after request_follow_mode()";
        sp->request_follow_mode();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        ok = sp->set_gimbal_speed(yawSpeed, pitchSpeed);
        qDebug() << "[SiyiCameraController] retry set_gimbal_speed() returned:" << (ok ? "OK" : "FAIL");
    }

    if (!ok) {
        // If still failing, clear asserted flag so we can try again later
        follow_mode_asserted.store(false);
    }

    return ok;
}


bool SiyiCameraController::setGimbalPosition(int, int)
{
    qDebug() << "[SiyiCameraController] setGimbalPosition not supported by SDK";
    return false;
}

bool SiyiCameraController::setAbsoluteZoom(float zoomLevel, int speed)
{
    (void) speed;
    auto sp = sdkPtr;
    if (!sp) return false;
    int integer = static_cast<int>(zoomLevel);
    int fractional = static_cast<int>((zoomLevel - integer) * 1000.0f);
    bool ok = sp->set_absolute_zoom(integer, fractional);
    qDebug() << "[SiyiCameraController] set_absolute_zoom(" << zoomLevel << ") -> " << ok;
    return ok;
}

bool SiyiCameraController::requestAutofocus()
{
    auto sp = sdkPtr;
    if (!sp) return false;
    return sp->request_autofocus();
}

void SiyiCameraController::setRtspUri(const QString &uri)
{
    // Called by MainWindow when config changes. We parse the URI to extract the SDK IP/port
    qDebug() << "[SiyiCameraController] setRtspUri called with" << uri;
    std::string newIp;
    int newPort = 37260;
    parseRtsp(uri, newIp, newPort);

    std::lock_guard<std::mutex> lk(lifeMutex);

    // If same as current, nothing to do
    if (newIp == sdkIp_ && newPort == sdkPort_) {
        rtspUri_ = uri;
        return;
    }

    // store new values
    rtspUri_ = uri;
    sdkIp_ = newIp;
    sdkPort_ = newPort;

    // If running, perform a restart so the SDK connects to new IP/port
    if (running.load()) {
        qDebug() << "[SiyiCameraController] restarting controller for new SDK IP/port";
        // stop (will signal flag and cleanup)
        if (sdkLoopFlagPtr) {
            *sdkLoopFlagPtr = false;
        }

        // rename 'start' -> 'tstart' here as well
        auto tstart = std::chrono::steady_clock::now();
        bool joined = false;
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - tstart).count() < STOP_WAIT_MS)
        {
            if (threadExited.load()) {
                if (receiveThread.joinable()) {
                    try { receiveThread.join(); } catch(...) {}
                }
                joined = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(STOP_POLL_MS));
        }
        if (!joined && receiveThread.joinable()) {
            receiveThread.detach();
        }

        // release old SDK object and create new one
        sdkPtr.reset();
        sdkLoopFlagPtr.reset();
        running.store(false);

        // now start fresh (calls the member function start())
        start();
    }
}
