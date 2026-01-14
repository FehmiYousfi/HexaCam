#pragma once

#include "CameraController.h"        // your base interface
#include "thirdparty/SIYI-SDK/src/sdk.h"
#include <QString>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

class SiyiCameraController : public CameraController {
public:
    SiyiCameraController(const std::string &ip, int port);
    ~SiyiCameraController() override;

    // lifecycle
    bool start() override;
    void stop() override;
    bool isRunning() const override;

    // control API
    bool setGimbalSpeed(int yawSpeed, int pitchSpeed) override;
    bool setGimbalPosition(int yawPos, int pitchPos) override;
    bool setAbsoluteZoom(float zoomLevel, int speed = 1) override;
    bool requestAutofocus() override;

    // Important: CameraController declared this pure virtual — implement it
    void setRtspUri(const QString &uri) override;

private:
    // parse RTSP into ip/port if needed
    void parseRtsp(const QString &uri, std::string &outIp, int &outPort);

    QString rtspUri_;
    std::string sdkIp_;
    int sdkPort_ = 37260;

    // SDK ownership and thread control
    std::shared_ptr<SIYI_SDK> sdkPtr;
    std::shared_ptr<bool> sdkLoopFlagPtr;
    std::thread receiveThread;

    mutable std::mutex lifeMutex;
    std::atomic<bool> running{false};
    std::atomic<bool> threadExited{false};
    int lastSentYaw = 50;
    int lastSentPitch = 50;
};
