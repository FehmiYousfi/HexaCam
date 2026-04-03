// ServoCameraController.h
#pragma once
#include "CameraController.h"
#include <memory>
#include <string>
#include "servo_client.hpp"
#include <mutex>
#include <thread>

class ServoCameraController : public CameraController {
public:
    ServoCameraController(const std::string &ip, int port);
    ~ServoCameraController() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;

    bool setGimbalSpeed(int yaw, int pitch) override; // implement mapping to servo
    bool setAbsoluteZoom(float zoomLevel, int speed = 1) override; // maybe no-op
    bool requestAutofocus() override { return false; }
    void setRtspUri(const QString &uri) override;
    bool setGimbalPosition(int yawPos, int pitchPos) override;
    bool supportsAbsolutePosition() const override;


private:
    std::unique_ptr<ServoControl::ServoClient> client;
    // If you have a ServoWorker thread, keep it here and connect signals
    bool connected = false;
    std::atomic<bool> running{false};
    std::thread receiveThread;
    mutable std::mutex clientMutex;
};
