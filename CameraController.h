#pragma once
#include <QString>
#include <functional>

class CameraController {
public:
    virtual ~CameraController() = default;

    // lifecycle
    virtual bool start() = 0;            // start communications / threads
    virtual void stop() = 0;            // stop and cleanup
    virtual bool isRunning() const = 0; // status

    // movement commands (yaw, pitch are integers or normalized)
    virtual bool setGimbalSpeed(int yawSpeed, int pitchSpeed) = 0;
    virtual bool setGimbalPosition(int yawPos, int pitchPos) = 0; // optional absolute

    // zoom
    virtual bool setAbsoluteZoom(float zoomLevel, int speed = 1) = 0;

    // focus / auxiliary actions
    virtual bool requestAutofocus() = 0;

    // video-related (optional, some controllers may change RTSP)
    virtual void setRtspUri(const QString &uri) = 0;

    // callbacks for controller->UI events
    std::function<void()> onStarted;
    std::function<void(const QString&)> onError;
    virtual bool supportsAbsolutePosition() const { return false; }
};
