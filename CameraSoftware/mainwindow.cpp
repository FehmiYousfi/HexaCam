#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QTime>
#ifdef SDL_SUPPORTED
#include <SDL.h>
#endif
#include "QJoysticks.h"
#include "VideoRecorderWidget.h"
#include <QKeyEvent>
#include <QDockWidget>
#include <QListWidget>
#include <QProgressBar>
#include <QLabel>
#include <QMutexLocker>
#include <thread>
#include <QKeyEvent>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QListWidgetItem>
#include <QApplication>
#include <QShowEvent>
#include "thirdparty/SIYI-SDK/src/sdk.h"
#include <csignal>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <QMessageBox>
#include <QProcess>
#include <gst/video/videooverlay.h>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QtConcurrent/QtConcurrent>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include "SiyiCameraController.h"
#include "ServoCameraController.h"
#include <QPainter>
#include <QPixmap>
#include <QDialog>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QSlider>
#include <QStyle>
//#include "servo_client.hpp"

static const int CONTROL_PORT = 37260;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      keepRunning(true),
      currentZoom(1.0f)
{
    ui->setupUi(this);

#ifdef _DEBUG
    QPushButton *dbg = new QPushButton("DBG: pan+50", this);
    dbg->setToolTip("Sends a single setGimbalSpeed(50,0) to see if gimbal moves");
    dbg->setFixedSize(110,24);
    dbg->move(10, 10);
    connect(dbg, &QPushButton::clicked, this, [this]() {
        if (cameraController && cameraController->isRunning()) {
            qDebug() << "[DBG] sending single gimbal speed 50,0";
            cameraController->setGimbalSpeed(50,0);
            QTimer::singleShot(300, this, [this]() {
                if (cameraController) cameraController->setGimbalSpeed(0,0);
            });
        } else {
            qWarning() << "[DBG] controller not ready";
        }
    });
#endif

    // ── UI Setup (implemented in mainwindow_ui_setup.cpp) ──
    qApp->installEventFilter(this);
    ui->toggleButton->setFocusPolicy(Qt::NoFocus);
    this->setFocusPolicy(Qt::StrongFocus);
    ui->centralwidget->setFocusPolicy(Qt::StrongFocus);

    setupStyles();
    setupLayout();
    setupConnections();

    // Load configuration at startup (before video widget so dropdown is correct)
    isInitializing = true;
    populateConfigFields();
    isInitializing = false;

    setupVideoWidget();
    setupRecordingOverlay();
    setupConfigOverlayLabel();

    // ── Runtime initialization ──
    setFocusPolicy(Qt::StrongFocus);
    setFocus();

    updateDeviceList();
    statusBar()->showMessage("Ready");

    // Joystick axis polling timer
    QTimer *pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollAxisValues);
    pollTimer->start(50);

    // Gimbal command timer
    commandTimer = new QTimer(this);
    connect(commandTimer, &QTimer::timeout, this, &MainWindow::sendGimbalCommands);
    commandTimer->start(50);

    // Deferred camera controller initialization
    QTimer::singleShot(100, this, &MainWindow::initializeCameraController);
}


MainWindow::~MainWindow() {

    if (videoWidget && videoWidget->getReceiver())
        videoWidget->getReceiver()->stop();

    // 1. Stop command timer
    commandTimer->stop();
    
    // 2. Signal threads to stop
    keepRunning = false;
    
    // 3. Join threads (use . operator, not ->)
    if (receiveThread.joinable()) {  // CORRECTED
        receiveThread.join();
    }
    
    // 4. Delete SDK instance
    //delete sdk;
    if (cameraController) {
        cameraController->stop();
        cameraController.reset();
    }
}

void MainWindow::onJoystickItemClicked(QListWidgetItem *item) {
    int jsIndex = item->data(Qt::UserRole).toInt();
    cameraJoystickIndex = jsIndex;
    qDebug() << "Camera joystick set to index:" << cameraJoystickIndex;
}

void MainWindow::updateDeviceList() {
    ui->listWidget->clear();
    QStringList names = QJoysticks::getInstance()->deviceNames();
    if (names.isEmpty()) {
        ui->listWidget->addItem("No Joysticks Detected");
        statusBar()->showMessage("No joysticks detected", 3000);
        qDebug() << "No joysticks detected";
    } else {
        for (int i = 0; i < names.size(); ++i) {
            int axisCount = QJoysticks::getInstance()->getNumAxes(i);
            QString label = QString("%1 (%2 axes)").arg(names[i]).arg(axisCount);
            QListWidgetItem *item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, i);
            item->setForeground(Qt::white);
            if (axisCount == 3) {
                item->setBackground(QColor(46, 125, 50));   // soft green
                item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                item->setToolTip("Compatible — click to select");
            } else {
                item->setBackground(QColor(183, 28, 28));   // soft red
                item->setFlags(item->flags() | Qt::ItemIsEnabled);
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                item->setToolTip("Incompatible — requires 3 axes");
            }
            ui->listWidget->addItem(item);
            qDebug() << "Joystick" << i << "axis count:" << axisCount << "name:" << names[i];
        }
        statusBar()->showMessage(QString::number(names.size()) + " joystick(s) detected", 3000);
    }
    statusBar()->showMessage("Device list updated at " +
                             QDateTime::currentDateTime().toString("hh:mm:ss"), 3000);
}

void MainWindow::pollAxisValues() {
#ifdef SDL_SUPPORTED
    if (inputMode != InputMode::Keyboard) {
        SDL_PumpEvents();
        SDL_Joystick *sdl_joystick = SDL_JoystickOpen(cameraJoystickIndex);
        if (sdl_joystick) {
            int numAxes = SDL_JoystickNumAxes(sdl_joystick);
            for (int a = 0; a < numAxes; a++) {
                Sint16 raw = SDL_JoystickGetAxis(sdl_joystick, a);
                qreal normalized = (raw < 0) ? (raw / 32768.0) : (raw / 32767.0);
                updateAxisValues(cameraJoystickIndex, a, normalized);
            }
        }
    }
#endif
}

void MainWindow::updateAxisValues(int js, int axis, qreal value) {
    if (axis == 0 || axis == 1) {
        int percent = static_cast<int>((value + 1.0) * 50);
        if (axis == 0)
            ui->progressBar->setValue(percent);
        else if (axis == 1)
            ui->progressBar_2->setValue(percent);
    } else if (axis == 2) {
        // Incremental zoom: center = hold, forward = zoom in, backward = zoom out
        // Zoom level persists when axis returns to center.
        constexpr int    ZOOM_STEPS   = 5;
        constexpr qreal  DEAD_ZONE    = 0.25;   // ignore small axis drift
        constexpr qint64 REPEAT_MS    = 400;     // ms between repeated steps when held

        if (qAbs(value) > DEAD_ZONE) {
            // Determine direction: positive = zoom in, negative = zoom out
            int direction = (value > 0) ? 1 : -1;

            // Only step if enough time has passed (repeat rate limiter)
            if (!zoomRepeatTimer.isValid() || zoomRepeatTimer.elapsed() >= REPEAT_MS) {
                int newLevel = qBound(0, lastZoomLevel + direction, ZOOM_STEPS);
                if (newLevel != lastZoomLevel) {
                    lastZoomLevel = newLevel;
                    float newZoom = MIN_ZOOM + lastZoomLevel * ZOOM_STEP_CONSTANT;
                    newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);

                    currentZoom = newZoom;
                    if (cameraController) cameraController->setAbsoluteZoom(currentZoom, 1);
                    qDebug() << "[JS] Zoom level=" << lastZoomLevel << " zoom=" << newZoom;
                }
                zoomRepeatTimer.restart();
            }
        } else {
            // Axis in dead zone — stop repeat timer so next push steps immediately
            zoomRepeatTimer.invalidate();
        }

        // Update UI slider to reflect current level
        ui->progressBar_3->setRange(0, ZOOM_STEPS);
        ui->progressBar_3->setTextVisible(true);
        ui->progressBar_3->setFormat("%v");
        ui->progressBar_3->setValue(lastZoomLevel);
    } else {
        qDebug() << "Unknown axis index:" << axis;
    }
    if (inputMode != InputMode::Keyboard && js == cameraJoystickIndex) {
        onJoystickAxisChanged(js, axis, value);
    }
}

void MainWindow::updateButtonState(int js, int button, bool pressed) {
    if (js != 0)
        return;
    if (button == 0) {
        ui->toolButton->setText(pressed ? "Pressed" : "Released");
        ui->toolButton->setStyleSheet(pressed ? "background-color: green;" : "background-color: red;");
    }
    else if (button == 1) {
        ui->toolButton_2->setText(pressed ? "Pressed" : "Released");
        ui->toolButton_2->setStyleSheet(pressed ? "background-color: green;" : "background-color: red;");
    }
}

void MainWindow::onJoystickAxisChanged(int dev, int axis, qreal value)
{
    // Debug: log ALL axis events before filtering
    if (axis >= 2) {
        qDebug() << "[JS-DEBUG] dev=" << dev << " axis=" << axis << " value=" << value
                 << " inputMode=" << static_cast<int>(inputMode)
                 << " cameraJoystickIndex=" << cameraJoystickIndex;
    }

    // Only when in joystick‐mode and on the selected device
    if (inputMode != InputMode::Joystick || dev != cameraJoystickIndex)
        return;

    // 1) Dead-zone and cubic mapping
    constexpr qreal DEAD_ZONE = 0.05;
    auto applyCurve = [&](qreal v) {
        if (qAbs(v) < DEAD_ZONE) return 0.0;
        qreal sign = v < 0 ? -1.0 : 1.0;
        qreal m = (qAbs(v) - DEAD_ZONE) / (1.0 - DEAD_ZONE);
        return sign * (m * m * m);
    };
    qreal curved = applyCurve(value);

    // 2) Handle the three axes
    if (axis == 0) {
        // Left/right → yaw
        int yaw = static_cast<int>(curved * MOVE_SPEED);
        QMutexLocker locker(&commandMutex);
        currentYawSpeed = yaw;
    }
    else if (axis == 1) {
        // Up/down → pitch
        int pitch = static_cast<int>(curved * MOVE_SPEED);
        QMutexLocker locker(&commandMutex);
        currentPitchSpeed = pitch;
    }
    else if (axis == 2) {
        // Zoom is handled in updateAxisValues — skip here
        return;
    }

    else {
        // Other axes: ignore
        return;
    }

// 3) (Optional) fire autofocus immediately
// sdk->request_autofocus();

#ifdef _DEBUG
    qDebug() << "[JS] axis="<<axis
             << "raw="<<value
             << "curved="<<curved
             << " → YawSpeed="<<currentYawSpeed
             << " PitchSpeed="<<currentPitchSpeed
        ;
#endif
}


void MainWindow::onSwitchToKeyboard() {
    QMutexLocker locker(&commandMutex);

    if (!cameraController) {
        statusBar()->showMessage("No camera controller: cannot enter keyboard mode", 3000);
        qWarning() << "[onSwitchToKeyboard] no controller";
        return;
    }

    if (!cameraController->isRunning()) {
        qDebug() << "[onSwitchToKeyboard] controller not running; attempting start()";
        if (!cameraController->start()) {
            statusBar()->showMessage("Failed to start camera controller", 3000);
            qWarning() << "[onSwitchToKeyboard] failed to start controller";
            return;
        }
        // short, non-blocking wait for the controller to spin up
        QElapsedTimer t; t.start();
        while (t.elapsed() < 300) {
            QCoreApplication::processEvents();
            if (cameraController->isRunning()) break;
            QThread::msleep(10);
        }
        if (!cameraController->isRunning()) {
            statusBar()->showMessage("Controller did not become ready", 3000);
            qWarning() << "[onSwitchToKeyboard] controller not running after start attempt";
            return;
        }
    }
    inputMode = InputMode::Keyboard;
    currentYawSpeed = 0;
    currentPitchSpeed = 0;
    //sdk->set_gimbal_speed(0, 0);
    if (cameraController) cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
    this->setFocus(Qt::OtherFocusReason);
    if (ui->tabWidget) ui->tabWidget->setFocus(Qt::OtherFocusReason);
    this->grabKeyboard();
    statusBar()->showMessage("Keyboard mode active");
    qDebug() << "[MODE] keyboard";
    ui->switchtokeyboard->setStyleSheet("background-color: green;");
    ui->switchtojoystick->setStyleSheet("");
    ui->pushButtonConfiguration->setStyleSheet("");
    QWidget* w = ui->tabWidget->currentWidget();
    QPropertyAnimation* fade = new QPropertyAnimation(w, "windowOpacity", this);
    fade->setDuration(200);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::onSwitchToJoystick() {
    QMutexLocker locker(&commandMutex);
    inputMode = InputMode::Joystick;
    currentYawSpeed = 0;
    currentPitchSpeed = 0;
    //sdk->set_gimbal_speed(0, 0);
    if (cameraController) cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
    this->releaseKeyboard();
    statusBar()->showMessage("Joystick mode active");
    qDebug() << "[MODE] joystick";
    ui->switchtojoystick->setStyleSheet("background-color: green;");
    ui->switchtokeyboard->setStyleSheet("");
    ui->pushButtonConfiguration->setStyleSheet("");
}

void MainWindow::onSwitchToConfiguration()
{
    QMutexLocker locker(&commandMutex);
    inputMode = InputMode::Configuration;
    this->releaseKeyboard();
    statusBar()->showMessage("Configuration mode active");
    qDebug() << "[MODE] Configuration";
    ui->pushButtonConfiguration->setStyleSheet("background-color: green;");
    ui->switchtojoystick->setStyleSheet("");
    ui->switchtokeyboard->setStyleSheet("");

}
void MainWindow::keyPressEvent(QKeyEvent* event) {
    qDebug() << "[keyPressEvent] Detailed debug -"
             << "key:" << event->key()
             << "text:" << event->text()
             << "autoRepeat:" << event->isAutoRepeat()
             << "inputMode:" << static_cast<int>(inputMode)
             << "cameraController:" << (cameraController ? "exists" : "null")
             << "controllerRunning:" << (cameraController ? cameraController->isRunning() : false)
             << "focusWidget:" << (qApp->focusWidget() ? qApp->focusWidget()->metaObject()->className() : "null");

    // Let QLineEdits handle their own typing
    if (qobject_cast<QLineEdit*>(qApp->focusWidget())) {
        qDebug() << "[keyPressEvent] Ignoring - QLineEdit has focus";
        return QMainWindow::keyPressEvent(event);
    }

    if (inputMode == InputMode::Keyboard) {
        int oldPos = _servoPosition;

        bool isServo = false;
        if (cameraController) {
            isServo = (dynamic_cast<ServoCameraController*>(cameraController.get()) != nullptr);
        }

        switch (event->key()) {
        case Qt::Key_Z:  // tilt up
            if (isServo) {
                // servo: change absolute position
                _servoPosition = qBound(0, _servoPosition - 5, 180);
                emit servoPositionChanged(_servoPosition);
                ui->toolButtonUp->setStyleSheet("background-color: green;");
            } else {
                // gimbal: set pitch speed (negative for up)
                QMutexLocker locker(&commandMutex);
                currentPitchSpeed = -MOVE_SPEED;
                if (cameraController) {
                    bool okImmediate = cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
                    qDebug() << "[keyPressEvent] immediate setGimbalSpeed returned:" << (okImmediate ? "OK" : "FAIL");
                }
                ui->toolButtonUp->setStyleSheet("background-color: green;");
            }
            break;
        case Qt::Key_S:  // tilt down
            if (isServo) {
                _servoPosition = qBound(0, _servoPosition + 5, 180);
                emit servoPositionChanged(_servoPosition);
                ui->toolButtonDown->setStyleSheet("background-color: green;");
            } else {
                QMutexLocker locker(&commandMutex);
                currentPitchSpeed = MOVE_SPEED;
                if (cameraController) {
                    bool okImmediate = cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
                    qDebug() << "[keyPressEvent] immediate setGimbalSpeed returned:" << (okImmediate ? "OK" : "FAIL");
                }
                ui->toolButtonDown->setStyleSheet("background-color: green;");
            }
            break;
        case Qt::Key_Q:  // pan left
        {
            QMutexLocker locker(&commandMutex);
            currentYawSpeed = -MOVE_SPEED;
            if (cameraController) {
                bool okImmediate = cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
                qDebug() << "[keyPressEvent] immediate setGimbalSpeed returned:" << (okImmediate ? "OK" : "FAIL");
            }
            ui->toolButtonLeft->setStyleSheet("background-color: green;");
        }
        break;
        case Qt::Key_D:  // pan right
        {
            QMutexLocker locker(&commandMutex);
            currentYawSpeed = MOVE_SPEED;
            if (cameraController) {
                bool okImmediate = cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
                qDebug() << "[keyPressEvent] immediate setGimbalSpeed returned:" << (okImmediate ? "OK" : "FAIL");
            }
            ui->toolButtonRight->setStyleSheet("background-color: green;");
        }
        break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
        {
            QMutexLocker locker(&commandMutex);
            currentZoom = std::min(MAX_ZOOM, currentZoom + ZOOM_SPEED);
            if(ui->toolButtonZoomPlus) ui->toolButtonZoomPlus->setStyleSheet("background-color: green;");
            if (cameraController) cameraController->setAbsoluteZoom(currentZoom, 1);
        }
            break;
        case Qt::Key_Minus:
        case Qt::Key_Underscore:
        {
            QMutexLocker locker(&commandMutex);
            currentZoom = std::max(MIN_ZOOM, currentZoom - ZOOM_SPEED);
            if(ui->toolButtonZoomMinus) ui->toolButtonZoomMinus->setStyleSheet("background-color: green;");
            if (cameraController) cameraController->setAbsoluteZoom(currentZoom, 1);
        }
            break;
        default:
            return QMainWindow::keyPressEvent(event);
        }
        event->accept();

        // Only emit if the position actually changed
        if (_servoPosition != oldPos) {
            emit servoPositionChanged(_servoPosition);
        }
    } else {
        QMainWindow::keyPressEvent(event);
    }
}


void MainWindow::keyReleaseEvent(QKeyEvent *event) {

    qDebug() << "[keyReleaseEvent]"
             << "key:" << event->key()
             << "text:" << event->text()
             << "autoRepeat:" << event->isAutoRepeat()
             << "inputMode:" << static_cast<int>(inputMode)
             << "focusWidget:" << (qApp->focusWidget()? qApp->focusWidget()->metaObject()->className() : "null");

    // same “let line‑edit” guard
    if (qobject_cast<QLineEdit*>(qApp->focusWidget())) {
        return QMainWindow::keyReleaseEvent(event);
    }

    if (inputMode == InputMode::Keyboard) {

        bool isServo = false;
        if (cameraController) {
            isServo = (dynamic_cast<ServoCameraController*>(cameraController.get()) != nullptr);
        }
        switch (event->key()) {
        case Qt::Key_Z:
        case Qt::Key_S:
            ui->toolButtonUp->setStyleSheet("");
            ui->toolButtonDown->setStyleSheet("");
            if (!isServo) {
                QMutexLocker locker(&commandMutex);
                if (!isServo) currentPitchSpeed = 0;
                if (cameraController) {
                    bool ok = cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
                    qDebug() << "[keyReleaseEvent] immediate stop setGimbalSpeed returned:" << (ok ? "OK" : "FAIL");
                }
            }
            break;
        case Qt::Key_Q:
        case Qt::Key_D:
        {
            QMutexLocker locker(&commandMutex);
            if (!isServo) currentYawSpeed = 0;
            if (cameraController) {
                bool ok = cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
                qDebug() << "[keyReleaseEvent] immediate stop setGimbalSpeed returned:" << (ok ? "OK" : "FAIL");
            }
        }
            ui->toolButtonLeft->setStyleSheet("");
            ui->toolButtonRight->setStyleSheet("");
            break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
        case Qt::Key_Minus:
        case Qt::Key_Underscore:
        {
            QMutexLocker locker(&commandMutex);
        }
            if(ui->toolButtonZoomPlus) ui->toolButtonZoomPlus->setStyleSheet("");
            if(ui->toolButtonZoomMinus) ui->toolButtonZoomMinus->setStyleSheet("");
            break;
        default:
            QMainWindow::keyReleaseEvent(event);
        }
        event->accept();
    } else {
        QMainWindow::keyReleaseEvent(event);
    }
}

void MainWindow::onFullUp() {
    currentYawSpeed = 0;
    currentPitchSpeed = -MOVE_SPEED;
    if (cameraController) cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
}

void MainWindow::onFullDown() {
    currentYawSpeed = 0;
    currentPitchSpeed = MOVE_SPEED;
    if (cameraController) cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
}

void MainWindow::onFullLeft() {
    currentYawSpeed = -MOVE_SPEED;
    currentPitchSpeed = 0;
    if (cameraController) cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
}

void MainWindow::onFullRight() {
    currentYawSpeed = MOVE_SPEED;
    currentPitchSpeed = 0;
    if (cameraController) cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
}

// Zoom - jump straight to min/max
void MainWindow::onZoomMaxIn() {
    currentZoom = MAX_ZOOM;
    // sdk->set_absolute_zoom(currentZoom, 1);
    // sdk->request_autofocus();
    if (cameraController) cameraController->setAbsoluteZoom(currentZoom, 1);
}

void MainWindow::onZoomMaxOut() {
    currentZoom = MIN_ZOOM;
    // sdk->set_absolute_zoom(currentZoom, 1);
    // sdk->request_autofocus();
    if (cameraController) cameraController->setAbsoluteZoom(currentZoom, 1);
}

void MainWindow::onStop() {
    currentYawSpeed = 0;
    currentPitchSpeed = 0;
    if (cameraController) cameraController->setGimbalSpeed(0, 0);
}

void MainWindow::sendGimbalCommands() {
    if ((inputMode == InputMode::None) || (inputMode == InputMode::Configuration)) {
        // Skip logging when mode is inactive to reduce log spam
        return;
    }

    int yaw, pitch;
    {
        QMutexLocker locker(&commandMutex);
        yaw = currentYawSpeed;
        pitch = currentPitchSpeed;
    }

    if (!cameraController) {
        static bool noControllerLogged = false;
        if (!noControllerLogged) {
            qDebug() << "[sendGimbalCommands] no cameraController";
            noControllerLogged = true;
        }
        return;
    }

    static int lastYaw = INT_MIN;
    static int lastPitch = INT_MIN;

    // Always send the command, but log only when changed
    bool ok = cameraController->setGimbalSpeed(yaw, pitch);
    if (!ok) {
        qWarning() << "[sendGimbalCommands] controller rejected gimbal speed:" << yaw << pitch;
    } else {
        if (yaw != lastYaw || pitch != lastPitch) {
            qDebug() << "[sendGimbalCommands] cmd -> yaw:" << yaw << " pitch:" << pitch;
            lastYaw = yaw;
            lastPitch = pitch;
        }
    }
}


// void MainWindow::sendGimbalCommands() {

//     if (inputMode == InputMode::None)
//         return;
//     QMutexLocker locker(&commandMutex);
//     // Always send commands regardless of speed values
//     bool success = cameraController->setGimbalSpeed(currentYawSpeed, currentPitchSpeed);
//     //sdk->request_autofocus();
//     //qDebug() << "Command sent - Yaw:" << currentYawSpeed << "Pitch:" << currentPitchSpeed << "Success:" << success;
//     //qDebug() << "Command:" << currentYawSpeed << "," << currentPitchSpeed
//     //         << (success ? "Succeeded" : "Failed");
//     //#ifdef _DEBUG
//     //qDebug() << "Command sent - Yaw:" << currentYawSpeed << "Pitch:" << currentPitchSpeed << "Success:" << success;
//     //#endif
//     if (inputMode != InputMode::Joystick) {
//         currentYawSpeed   = 0;
//         currentPitchSpeed = 0;
//     }
//     if (inputMode != InputMode::Keyboard) {
//         currentYawSpeed   = 0;
//         currentPitchSpeed = 0;
//     }
// }


void MainWindow::onSelectSiyiClicked()
{
    if (!ui->cameraTypeStack) return;
    ui->cameraTypeStack->setCurrentWidget(ui->page_siyi);
}

void MainWindow::onSelectServoClicked()
{
    if (!ui->cameraTypeStack) return;
    ui->cameraTypeStack->setCurrentWidget(ui->page_servo);
}

void MainWindow::onSelectAiClicked()
{
    if (!ui->cameraTypeStack) return;
    ui->cameraTypeStack->setCurrentWidget(ui->page_ai);
}

void MainWindow::onCameraChooseBack()
{
    if (!ui->cameraTypeStack) return;
    ui->cameraTypeStack->setCurrentWidget(ui->page_choose_type);
}

void MainWindow::populateConfigFields()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    QString cfgFile = dir.filePath("Haxa5Camera/Hexa5CameraConfig.json");

    // defaults
    QString ipDefault("192.168.1.64");
    int portDefault = 554;
    QString pathDefault("/main.264");
    QString servoIpDefault("10.14.11.1");
    int servoPortDefault = 8000;
    QString aiCameraIpDefault("192.168.1.100");
    int aiControlPortDefault = 8080;
    QString aiPathDefault("/ai/stream");

    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly)) {
        // set UI defaults for first run
        if (ui->siyi_lineEditIP) ui->siyi_lineEditIP->setText(ipDefault);
        if (ui->siyi_lineEditPort) ui->siyi_lineEditPort->setText(QString::number(portDefault));
        if (ui->siyi_lineEditPath) ui->siyi_lineEditPath->setText(pathDefault);

        if (ui->servo_lineEditIP) ui->servo_lineEditIP->setText(ipDefault);
        if (ui->servo_lineEditPort) ui->servo_lineEditPort->setText(QString::number(portDefault));
        if (ui->servo_lineEditPath) ui->servo_lineEditPath->setText(pathDefault);
        if (ui->servo_lineEditServoIP) ui->servo_lineEditServoIP->setText(servoIpDefault);
        if (ui->servo_lineEditServoPort) ui->servo_lineEditServoPort->setText(QString::number(servoPortDefault));

        if (ui->ai_lineEditCameraIP) ui->ai_lineEditCameraIP->setText(aiCameraIpDefault);
        if (ui->ai_lineEditControlPort) ui->ai_lineEditControlPort->setText(QString::number(aiControlPortDefault));
        if (ui->ai_lineEditPath) ui->ai_lineEditPath->setText(aiPathDefault);
        
        // Set default video source
        if (ui->videoSourceComboBox) ui->videoSourceComboBox->setCurrentIndex(0); // SIYI
        return;
    }

    QByteArray data = f.readAll();
    f.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        // Invalid JSON - set defaults
        statusBar()->showMessage("Invalid config file - using defaults", 3000);
        return;
    }
    
    QJsonObject obj = doc.object();
    
    // Check if this is old format (has cameraType field) for backward compatibility
    if (obj.contains("cameraType")) {
        // Migrate old format to new parallel format
        QString cameraType = obj.value("cameraType").toString("siyi").toLower();
        
        if (cameraType == "siyi") {
            // Migrate SIYI config
            if (ui->siyi_lineEditIP) ui->siyi_lineEditIP->setText(obj.value("ip").toString(ipDefault));
            if (ui->siyi_lineEditPort) ui->siyi_lineEditPort->setText(QString::number(obj.value("port").toInt(portDefault)));
            if (ui->siyi_lineEditPath) ui->siyi_lineEditPath->setText(obj.value("path").toString(pathDefault));
            if (ui->videoSourceComboBox) ui->videoSourceComboBox->setCurrentIndex(0); // SIYI
            
        } else if (cameraType == "ai") {
            // Migrate AI config
            if (ui->ai_lineEditCameraIP) ui->ai_lineEditCameraIP->setText(obj.value("aiCameraIP").toString(aiCameraIpDefault));
            if (ui->ai_lineEditControlPort) ui->ai_lineEditControlPort->setText(QString::number(obj.value("aiControlPort").toInt(aiControlPortDefault)));
            if (ui->ai_lineEditPath) ui->ai_lineEditPath->setText(obj.value("path").toString(aiPathDefault));
            if (ui->videoSourceComboBox) ui->videoSourceComboBox->setCurrentIndex(1); // AI
            
        } else if (cameraType == "servo") {
            // Migrate Servo config
            if (ui->servo_lineEditIP) ui->servo_lineEditIP->setText(obj.value("ip").toString(ipDefault));
            if (ui->servo_lineEditPort) ui->servo_lineEditPort->setText(QString::number(obj.value("port").toInt(portDefault)));
            if (ui->servo_lineEditPath) ui->servo_lineEditPath->setText(obj.value("path").toString(pathDefault));
            if (ui->servo_lineEditServoIP) ui->servo_lineEditServoIP->setText(obj.value("servoIP").toString(servoIpDefault));
            if (ui->servo_lineEditServoPort) ui->servo_lineEditServoPort->setText(QString::number(obj.value("servoPort").toInt(servoPortDefault)));
            if (ui->videoSourceComboBox) ui->videoSourceComboBox->setCurrentIndex(0); // SIYI
        }
        
        // Auto-migrate by saving in new format
        statusBar()->showMessage("Migrated old config format to new parallel structure", 3000);
        saveConfig();
        return;
    }
    
    // Load new parallel format
    QString videoSource = obj.value("videoSource").toString("siyi");
    
    // Load SIYI config if present
    if (obj.contains("siyiConfig")) {
        QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
        bool compressorMode = siyiConfig.value("compressorMode").toBool(false);
        if (ui->siyi_checkBoxCompressor) ui->siyi_checkBoxCompressor->setChecked(compressorMode);
        if (compressorMode) {
            if (ui->siyi_lineEditVideoIP) ui->siyi_lineEditVideoIP->setText(siyiConfig.value("videoIP").toString(""));
            if (ui->siyi_lineEditControlIP) ui->siyi_lineEditControlIP->setText(siyiConfig.value("controlIP").toString(""));
            if (ui->siyi_lineEditIP) ui->siyi_lineEditIP->setText(ipDefault);
        } else {
            if (ui->siyi_lineEditIP) ui->siyi_lineEditIP->setText(siyiConfig.value("ip").toString(ipDefault));
        }
        if (ui->siyi_lineEditPort) ui->siyi_lineEditPort->setText(QString::number(siyiConfig.value("port").toInt(portDefault)));
        if (ui->siyi_lineEditPath) ui->siyi_lineEditPath->setText(siyiConfig.value("path").toString(pathDefault));
    }
    
    // Load AI config if present
    if (obj.contains("aiConfig")) {
        QJsonObject aiConfig = obj.value("aiConfig").toObject();
        if (ui->ai_lineEditCameraIP) ui->ai_lineEditCameraIP->setText(aiConfig.value("cameraIP").toString(aiCameraIpDefault));
        if (ui->ai_lineEditControlPort) ui->ai_lineEditControlPort->setText(QString::number(aiConfig.value("controlPort").toInt(aiControlPortDefault)));
        if (ui->ai_lineEditPath) ui->ai_lineEditPath->setText(aiConfig.value("path").toString(aiPathDefault));
    }
    
    // Load Servo config if present (legacy)
    if (obj.contains("servoConfig")) {
        QJsonObject servoConfig = obj.value("servoConfig").toObject();
        if (ui->servo_lineEditServoIP) ui->servo_lineEditServoIP->setText(servoConfig.value("servoIP").toString(servoIpDefault));
        if (ui->servo_lineEditServoPort) ui->servo_lineEditServoPort->setText(QString::number(servoConfig.value("servoPort").toInt(servoPortDefault)));
    }
    
    // Set video source selection
    if (ui->videoSourceComboBox) {
        if (videoSource == "ai") {
            ui->videoSourceComboBox->setCurrentIndex(1); // AI
            qDebug() << "[VIDEO_SOURCE] Startup: Set dropdown to AI (index 1) from config";
        } else {
            ui->videoSourceComboBox->setCurrentIndex(0); // SIYI
            qDebug() << "[VIDEO_SOURCE] Startup: Set dropdown to SIYI (index 0) from config";
        }
        qDebug() << "[VIDEO_SOURCE] Startup: Current videoSource from config:" << videoSource;
    }
    
    // Validate configuration after loading
    configValid = validateConfiguration("");
    updateConfigDisplay();
}


void MainWindow::updateVideoSourceInConfig() {
    qDebug() << "[VIDEO_SOURCE] updateVideoSourceInConfig() called";
    
    // Get selected video source from dropdown
    QString videoSource = "siyi"; // default
    int currentIndex = 0;
    if (ui->videoSourceComboBox) {
        QString sourceText = ui->videoSourceComboBox->currentText();
        currentIndex = ui->videoSourceComboBox->currentIndex();
        if (sourceText.contains("AI", Qt::CaseInsensitive)) {
            videoSource = "ai";
        } else {
            videoSource = "siyi";
        }
    }
    qDebug() << "[VIDEO_SOURCE] Updating videoSource to:" << videoSource << "(index:" << currentIndex << ")";
    
    // Read existing config
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString subFolder = "Haxa5Camera";
    QDir d(configDir);
    d.mkpath(subFolder);
    QString cfgFile = d.filePath(subFolder + "/Hexa5CameraConfig.json");
    
    QFile file(cfgFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[VIDEO_SOURCE] Failed to open config file for reading:" << cfgFile;
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qWarning() << "[VIDEO_SOURCE] Invalid JSON config, creating new one";
        // Create new config with just the videoSource
        QJsonObject newObj;
        newObj["videoSource"] = videoSource;
        QJsonDocument newDoc(newObj);
        
        if (file.open(QIODevice::WriteOnly)) {
            file.write(newDoc.toJson(QJsonDocument::Indented));
            file.close();
            qDebug() << "[VIDEO_SOURCE] Created new config with videoSource:" << videoSource;
        }
        return;
    }
    
    // Update videoSource field
    QJsonObject obj = doc.object();
    QString oldVideoSource = obj.value("videoSource").toString("unknown");
    obj["videoSource"] = videoSource;
    
    // Write back to file
    QJsonDocument updatedDoc(obj);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[VIDEO_SOURCE] Failed to open config file for writing:" << cfgFile;
        return;
    }
    
    file.write(updatedDoc.toJson(QJsonDocument::Indented));
    file.close();
    
    qDebug() << "[VIDEO_SOURCE] videoSource updated successfully:" << oldVideoSource << "->" << videoSource;
    qDebug() << "[VIDEO_SOURCE] Updated JSON:" << updatedDoc.toJson(QJsonDocument::Compact);
    
    // Update display to reflect the change
    updateConfigDisplay();
}

void MainWindow::saveConfigWithoutApply() {
    qDebug() << "[VIDEO_SOURCE] saveConfigWithoutApply() called";
    
    // Read all configuration values from UI
    QString siyiIP = ui->siyi_lineEditIP ? ui->siyi_lineEditIP->text().trimmed() : "";
    int siyiPort = ui->siyi_lineEditPort ? ui->siyi_lineEditPort->text().toInt() : 0;
    QString siyiPath = ui->siyi_lineEditPath ? ui->siyi_lineEditPath->text().trimmed() : "";
    bool siyiCompressor = ui->siyi_checkBoxCompressor ? ui->siyi_checkBoxCompressor->isChecked() : false;
    QString siyiVideoIP = ui->siyi_lineEditVideoIP ? ui->siyi_lineEditVideoIP->text().trimmed() : "";
    QString siyiControlIP = ui->siyi_lineEditControlIP ? ui->siyi_lineEditControlIP->text().trimmed() : "";
    
    QString aiCameraIP = ui->ai_lineEditCameraIP ? ui->ai_lineEditCameraIP->text().trimmed() : "";
    int aiControlPort = ui->ai_lineEditControlPort ? ui->ai_lineEditControlPort->text().toInt() : 0;
    QString aiPath = ui->ai_lineEditPath ? ui->ai_lineEditPath->text().trimmed() : "";
    
    QString servoIP = ui->servo_lineEditServoIP ? ui->servo_lineEditServoIP->text().trimmed() : "";
    int servoPort = ui->servo_lineEditServoPort ? ui->servo_lineEditServoPort->text().toInt() : 0;
    
    // Get video source selection
    QString videoSource = "siyi"; // default
    if (ui->videoSourceComboBox) {
        int currentIndex = ui->videoSourceComboBox->currentIndex();
        if (currentIndex == 1) videoSource = "ai";
    }
    qDebug() << "[VIDEO_SOURCE] Selected video source for saving:" << videoSource << "(index:" << (ui->videoSourceComboBox ? ui->videoSourceComboBox->currentIndex() : -1) << ")";
    
    // Validate configurations
    bool siyiValid = false;
    if (siyiCompressor) {
        siyiValid = !siyiVideoIP.isEmpty() && !siyiControlIP.isEmpty() && siyiPort > 0 && !siyiPath.isEmpty();
    } else {
        siyiValid = !siyiIP.isEmpty() && siyiPort > 0 && !siyiPath.isEmpty();
    }
    bool aiValid = !aiCameraIP.isEmpty() && aiControlPort > 0 && !aiPath.isEmpty();
    bool servoValid = !servoIP.isEmpty() && servoPort > 0;
    
    qDebug() << "[VIDEO_SOURCE] Config validation - SIYI:" << siyiValid << "AI:" << aiValid << "Servo:" << servoValid << "Compressor:" << siyiCompressor;
    
    // At least one configuration must be valid
    if (!siyiValid && !aiValid) {
        statusBar()->showMessage("At least one valid camera configuration required", 3000);
        return;
    }
    
    // Build new parallel config structure
    QJsonObject obj;
    obj["videoSource"] = videoSource;
    
    // Add SIYI config if valid
    if (siyiValid) {
        QJsonObject siyiConfig;
        siyiConfig["port"] = siyiPort;
        siyiConfig["path"] = siyiPath;
        siyiConfig["compressorMode"] = siyiCompressor;
        if (siyiCompressor) {
            siyiConfig["videoIP"] = siyiVideoIP;
            siyiConfig["controlIP"] = siyiControlIP;
        } else {
            siyiConfig["ip"] = siyiIP;
        }
        obj["siyiConfig"] = siyiConfig;
        qDebug() << "[VIDEO_SOURCE] SIYI config added to JSON (compressor:" << siyiCompressor << ")";
    }
    
    // Add AI config if valid
    if (aiValid) {
        QJsonObject aiConfig;
        aiConfig["cameraIP"] = aiCameraIP;
        aiConfig["controlPort"] = aiControlPort;
        aiConfig["path"] = aiPath;
        obj["aiConfig"] = aiConfig;
        qDebug() << "[VIDEO_SOURCE] AI config added to JSON";
    }
    
    // Add Servo config if valid (legacy support)
    if (!servoIP.isEmpty() && servoPort > 0) {
        QJsonObject servoConfig;
        servoConfig["servoIP"] = servoIP;
        servoConfig["servoPort"] = servoPort;
        obj["servoConfig"] = servoConfig;
        qDebug() << "[VIDEO_SOURCE] Servo config added to JSON";
    }

    QJsonDocument doc(obj);
    qDebug() << "[VIDEO_SOURCE] JSON to save:" << doc.toJson(QJsonDocument::Compact);
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString subFolder = "Haxa5Camera";
    QDir d(configDir);
    d.mkpath(subFolder);
    QString cfgFile = d.filePath(subFolder + "/Hexa5CameraConfig.json");

    QFile file(cfgFile);
    if (!file.open(QIODevice::WriteOnly)) {
        statusBar()->showMessage("Failed to open config file for writing", 3000);
        qWarning() << "Failed to write config to" << cfgFile;
        return;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    statusBar()->showMessage("Configuration saved", 3000);

    qDebug() << "[VIDEO_SOURCE] Config file written successfully to:" << cfgFile;

    // Re-validate and update display after saving
    configValid = validateConfiguration("");
    updateConfigDisplay();
    qDebug() << "[VIDEO_SOURCE] saveConfigWithoutApply() completed - no applyConfig() called";
}

void MainWindow::saveConfig() {
    qDebug() << "[VIDEO_SOURCE] saveConfig() called";
    
    // Read all configuration values from UI
    QString siyiIP = ui->siyi_lineEditIP ? ui->siyi_lineEditIP->text().trimmed() : "";
    int siyiPort = ui->siyi_lineEditPort ? ui->siyi_lineEditPort->text().toInt() : 0;
    QString siyiPath = ui->siyi_lineEditPath ? ui->siyi_lineEditPath->text().trimmed() : "";
    bool siyiCompressor = ui->siyi_checkBoxCompressor ? ui->siyi_checkBoxCompressor->isChecked() : false;
    QString siyiVideoIP = ui->siyi_lineEditVideoIP ? ui->siyi_lineEditVideoIP->text().trimmed() : "";
    QString siyiControlIP = ui->siyi_lineEditControlIP ? ui->siyi_lineEditControlIP->text().trimmed() : "";
    
    QString aiCameraIP = ui->ai_lineEditCameraIP ? ui->ai_lineEditCameraIP->text().trimmed() : "";
    int aiControlPort = ui->ai_lineEditControlPort ? ui->ai_lineEditControlPort->text().toInt() : 0;
    QString aiPath = ui->ai_lineEditPath ? ui->ai_lineEditPath->text().trimmed() : "";
    
    QString servoIP = ui->servo_lineEditServoIP ? ui->servo_lineEditServoIP->text().trimmed() : "";
    int servoPort = ui->servo_lineEditServoPort ? ui->servo_lineEditServoPort->text().toInt() : 0;
    
    // Get video source selection
    QString videoSource = "siyi"; // default
    if (ui->videoSourceComboBox) {
        int currentIndex = ui->videoSourceComboBox->currentIndex();
        if (currentIndex == 1) videoSource = "ai";
    }
    qDebug() << "[VIDEO_SOURCE] Selected video source for saving:" << videoSource << "(index:" << (ui->videoSourceComboBox ? ui->videoSourceComboBox->currentIndex() : -1) << ")";
    
    // Validate configurations
    bool siyiValid = false;
    if (siyiCompressor) {
        siyiValid = !siyiVideoIP.isEmpty() && !siyiControlIP.isEmpty() && siyiPort > 0 && !siyiPath.isEmpty();
    } else {
        siyiValid = !siyiIP.isEmpty() && siyiPort > 0 && !siyiPath.isEmpty();
    }
    bool aiValid = !aiCameraIP.isEmpty() && aiControlPort > 0 && !aiPath.isEmpty();
    bool servoValid = !servoIP.isEmpty() && servoPort > 0;
    
    qDebug() << "[VIDEO_SOURCE] Config validation - SIYI:" << siyiValid << "AI:" << aiValid << "Servo:" << servoValid << "Compressor:" << siyiCompressor;
    
    // At least one configuration must be valid
    if (!siyiValid && !aiValid) {
        statusBar()->showMessage("At least one valid camera configuration required", 3000);
        return;
    }
    
    // Build new parallel config structure
    QJsonObject obj;
    obj["videoSource"] = videoSource;
    
    // Add SIYI config if valid
    if (siyiValid) {
        QJsonObject siyiConfig;
        siyiConfig["port"] = siyiPort;
        siyiConfig["path"] = siyiPath;
        siyiConfig["compressorMode"] = siyiCompressor;
        if (siyiCompressor) {
            siyiConfig["videoIP"] = siyiVideoIP;
            siyiConfig["controlIP"] = siyiControlIP;
        } else {
            siyiConfig["ip"] = siyiIP;
        }
        obj["siyiConfig"] = siyiConfig;
        qDebug() << "[VIDEO_SOURCE] SIYI config added to JSON (compressor:" << siyiCompressor << ")";
    }
    
    // Add AI config if valid
    if (aiValid) {
        QJsonObject aiConfig;
        aiConfig["cameraIP"] = aiCameraIP;
        aiConfig["controlPort"] = aiControlPort;
        aiConfig["path"] = aiPath;
        obj["aiConfig"] = aiConfig;
        qDebug() << "[VIDEO_SOURCE] AI config added to JSON";
    }
    
    // Add Servo config if valid (legacy support)
    if (!servoIP.isEmpty() && servoPort > 0) {
        QJsonObject servoConfig;
        servoConfig["servoIP"] = servoIP;
        servoConfig["servoPort"] = servoPort;
        obj["servoConfig"] = servoConfig;
        qDebug() << "[VIDEO_SOURCE] Servo config added to JSON";
    }

    QJsonDocument doc(obj);
    qDebug() << "[VIDEO_SOURCE] JSON to save:" << doc.toJson(QJsonDocument::Compact);
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString subFolder = "Haxa5Camera";
    QDir d(configDir);
    d.mkpath(subFolder);
    QString cfgFile = d.filePath(subFolder + "/Hexa5CameraConfig.json");

    QFile file(cfgFile);
    if (!file.open(QIODevice::WriteOnly)) {
        statusBar()->showMessage("Failed to open config file for writing", 3000);
        qWarning() << "Failed to write config to" << cfgFile;
        return;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    statusBar()->showMessage("Configuration saved", 3000);

    qDebug() << "[VIDEO_SOURCE] Config file written successfully to:" << cfgFile;

    // Re-validate and update display after saving
    configValid = validateConfiguration("");
    updateConfigDisplay();
    qDebug() << "[VIDEO_SOURCE] saveConfig() calling applyConfig() - this may cause double restart";
    qDebug() << "Saved config to" << cfgFile;

    // Re-apply config (restart VideoReceiver and recreate camera controller)
    applyConfig();
    // Also re-create controller specifically (ensure createCameraControllerFromConfig reads cameraType)
    //createCameraControllerFromConfig();
    qDebug() << "[VIDEO_SOURCE] saveConfig() completed";
    return;
}


void MainWindow::onSiyiDefaultClicked()
{
    // Default SIYI values:
    const QString defaultIP = QStringLiteral("192.168.144.25");
    const int     defaultPort = 8554;
    const QString defaultPath = QStringLiteral("/main.264");

    if (ui->siyi_lineEditIP)   ui->siyi_lineEditIP->setText(defaultIP);
    if (ui->siyi_lineEditPort) ui->siyi_lineEditPort->setText(QString::number(defaultPort));
    if (ui->siyi_lineEditPath) ui->siyi_lineEditPath->setText(defaultPath);

    // Reset compressor mode to off
    if (ui->siyi_checkBoxCompressor) ui->siyi_checkBoxCompressor->setChecked(false);
    if (ui->siyi_lineEditVideoIP)    ui->siyi_lineEditVideoIP->clear();
    if (ui->siyi_lineEditControlIP)  ui->siyi_lineEditControlIP->clear();

    // Ensure we show the SIYI page
    if (ui->cameraTypeStack) ui->cameraTypeStack->setCurrentWidget(ui->page_siyi);

    // Persist defaults and apply them
    saveDefaultConfig();
}

void MainWindow::onServoDefaultClicked()
{
    // Default Servo + Camera values:
    const QString defaultIP = QStringLiteral("192.168.144.25");
    const int     defaultPort = 8554;
    const QString defaultPath = QStringLiteral("/main.264");
    const QString defaultServoIP = QStringLiteral("10.14.11.1");
    const int     defaultServoPort = 8000;

    if (ui->servo_lineEditIP)      ui->servo_lineEditIP->setText(defaultIP);
    if (ui->servo_lineEditPort)    ui->servo_lineEditPort->setText(QString::number(defaultPort));
    if (ui->servo_lineEditPath)    ui->servo_lineEditPath->setText(defaultPath);
    if (ui->servo_lineEditServoIP) ui->servo_lineEditServoIP->setText(defaultServoIP);
    if (ui->servo_lineEditServoPort) ui->servo_lineEditServoPort->setText(QString::number(defaultServoPort));

    // Ensure we show the Servo page
    if (ui->cameraTypeStack) ui->cameraTypeStack->setCurrentWidget(ui->page_servo);
}

void MainWindow::onAiDefaultClicked()
{
    // Default AI Configuration values:
    const QString defaultCameraIP = QStringLiteral("192.168.1.100");
    const int     defaultControlPort = 8080;

    if (ui->ai_lineEditCameraIP) ui->ai_lineEditCameraIP->setText(defaultCameraIP);
    if (ui->ai_lineEditControlPort) ui->ai_lineEditControlPort->setText(QString::number(defaultControlPort));

    // Ensure we show the AI page
    if (ui->cameraTypeStack) ui->cameraTypeStack->setCurrentWidget(ui->page_ai);
}


bool MainWindow::validateConfiguration(const QString& cameraType)
{
    configErrors.clear();
    
    // Read all configuration values from UI for validation
    QString siyiIP = ui->siyi_lineEditIP ? ui->siyi_lineEditIP->text().trimmed() : "";
    int siyiPort = ui->siyi_lineEditPort ? ui->siyi_lineEditPort->text().toInt() : 0;
    QString siyiPath = ui->siyi_lineEditPath ? ui->siyi_lineEditPath->text().trimmed() : "";
    
    QString aiCameraIP = ui->ai_lineEditCameraIP ? ui->ai_lineEditCameraIP->text().trimmed() : "";
    int aiControlPort = ui->ai_lineEditControlPort ? ui->ai_lineEditControlPort->text().toInt() : 0;
    QString aiPath = ui->ai_lineEditPath ? ui->ai_lineEditPath->text().trimmed() : "";
    
    QString servoIP = ui->servo_lineEditServoIP ? ui->servo_lineEditServoIP->text().trimmed() : "";
    int servoPort = ui->servo_lineEditServoPort ? ui->servo_lineEditServoPort->text().toInt() : 0;
    
    // Validate SIYI configuration
    bool siyiValid = !siyiIP.isEmpty() && siyiPort > 0 && !siyiPath.isEmpty();
    if (!siyiValid && (!siyiIP.isEmpty() || siyiPort > 0 || !siyiPath.isEmpty())) {
        if (siyiIP.isEmpty()) configErrors += "SIYI Camera IP missing; ";
        if (siyiPort <= 0) configErrors += "SIYI Camera Port invalid; ";
        if (siyiPath.isEmpty()) configErrors += "SIYI Camera Path missing; ";
    }
    
    // Validate AI configuration
    bool aiValid = !aiCameraIP.isEmpty() && aiControlPort > 0 && !aiPath.isEmpty();
    if (!aiValid && (!aiCameraIP.isEmpty() || aiControlPort > 0 || !aiPath.isEmpty())) {
        if (aiCameraIP.isEmpty()) configErrors += "AI Camera IP missing; ";
        if (aiControlPort <= 0) configErrors += "AI Control Port invalid; ";
        if (aiPath.isEmpty()) configErrors += "AI Stream Path missing; ";
    }
    
    // Validate Servo configuration
    bool servoValid = !servoIP.isEmpty() && servoPort > 0;
    if (!servoValid && (!servoIP.isEmpty() || servoPort > 0)) {
        if (servoIP.isEmpty()) configErrors += "Servo IP missing; ";
        if (servoPort <= 0) configErrors += "Servo Port invalid; ";
    }
    
    // Check if at least one valid configuration exists
    bool hasValidConfig = siyiValid || aiValid;
    
    // Update camera status display
    if (ui->lineEditCameraStatus) {
        if (configErrors.isEmpty() && hasValidConfig) {
            QString activeSource = "SIYI";
            if (ui->videoSourceComboBox && ui->videoSourceComboBox->currentIndex() == 1) {
                activeSource = "AI";
            }
            ui->lineEditCameraStatus->setText(QString("%1 configuration OK").arg(activeSource));
            ui->lineEditCameraStatus->setStyleSheet("color: green;");
        } else if (!hasValidConfig) {
            ui->lineEditCameraStatus->setText("No valid camera configuration");
            ui->lineEditCameraStatus->setStyleSheet("color: red;");
        } else {
            ui->lineEditCameraStatus->setText("Configuration errors: " + configErrors);
            ui->lineEditCameraStatus->setStyleSheet("color: red;");
        }
    }
    
    // Update VideoRecorderSection border based on validation
    if (ui->VideoRecorderSection) {
        if (configErrors.isEmpty() && hasValidConfig) {
            ui->VideoRecorderSection->setStyleSheet("");
        } else {
            ui->VideoRecorderSection->setStyleSheet("QGroupBox { border: 2px solid red; }");
        }
    }
    
    return configErrors.isEmpty() && hasValidConfig;
}


// ===========================================================================
// Section-based overlay controller
// ===========================================================================

void MainWindow::setOverlaySection(OverlaySection section, const QString& text)
{
    int idx = static_cast<int>(section);
    if (idx < 0 || idx >= kOverlaySectionCount) return;
    overlaySections_[idx] = text;
    renderOverlay();
}

void MainWindow::clearOverlaySection(OverlaySection section)
{
    int idx = static_cast<int>(section);
    if (idx < 0 || idx >= kOverlaySectionCount) return;
    overlaySections_[idx].clear();
    renderOverlay();
}

void MainWindow::renderOverlay()
{
    if (!overlayRenderTimer_) {
        overlayRenderTimer_ = new QTimer(this);
        overlayRenderTimer_->setSingleShot(true);
        overlayRenderTimer_->setInterval(0);
        connect(overlayRenderTimer_, &QTimer::timeout, this, &MainWindow::doRenderOverlay);
    }
    if (!overlayRenderTimer_->isActive())
        overlayRenderTimer_->start();
}

void MainWindow::doRenderOverlay()
{
    if (!configDisplayLabel) {
        setupConfigOverlayLabel();
    }

    if (currentOverlayMode_ == OverlayMode::Off) {
        configDisplayLabel->setVisible(false);
        return;
    }

    // Determine which sections to show based on mode
    // StreamConfig  → Config (0) + VideoStream (1)
    // RealTimeStats → Bandwidth (2) + Connectivity (3)
    int startIdx = 0, endIdx = kOverlaySectionCount;
    if (currentOverlayMode_ == OverlayMode::StreamConfig) {
        startIdx = static_cast<int>(OverlaySection::Config);
        endIdx   = static_cast<int>(OverlaySection::Bandwidth);   // exclusive
    } else if (currentOverlayMode_ == OverlayMode::RealTimeStats) {
        startIdx = static_cast<int>(OverlaySection::Bandwidth);
        endIdx   = kOverlaySectionCount;
    }

    // Join non-empty sections with separator lines
    QString displayText;
    for (int i = startIdx; i < endIdx; ++i) {
        if (overlaySections_[i].isEmpty()) continue;
        if (!displayText.isEmpty()) {
            displayText += QStringLiteral("\n---\n");
        }
        displayText += overlaySections_[i];
    }

    if (displayText.isEmpty()) {
        displayText = QStringLiteral("No data available");
    }

    configDisplayLabel->setText(displayText);

    // Calculate required height from text, clamp to max
    QFontMetrics fm(configDisplayLabel->font());
    int padding = 20; // 10px padding top + bottom from stylesheet
    int textHeight = fm.boundingRect(
        QRect(0, 0, configDisplayLabel->width() - padding, 10000),
        Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
        displayText).height();
    int labelHeight = qBound(100, textHeight + padding, 800);
    configDisplayLabel->setFixedHeight(labelHeight);

    configDisplayLabel->setVisible(true);
    configDisplayLabel->raise();
    configDisplayLabel->update(); // force full repaint to clear ghost pixels
}

// ---------------------------------------------------------------------------
// updateConfigDisplay — refreshes sections relevant to the current mode
// ---------------------------------------------------------------------------
void MainWindow::updateConfigDisplay()
{
    if (!configDisplayLabel) {
        setupConfigOverlayLabel();
    }

    if (currentOverlayMode_ == OverlayMode::Off) {
        configDisplayLabel->setVisible(false);
        return;
    }

    // Refresh only sections relevant to the active mode
    if (currentOverlayMode_ == OverlayMode::StreamConfig) {
        refreshOverlayConfigSection();
        refreshOverlayVideoSection();
    } else if (currentOverlayMode_ == OverlayMode::RealTimeStats) {
        refreshOverlayBandwidthSection();
        refreshOverlayConnectivitySection();
    }

    // Single render pass
    renderOverlay();
}

// ---------------------------------------------------------------------------
// Section 0 — Camera configuration (from JSON file)
// ---------------------------------------------------------------------------
void MainWindow::refreshOverlayConfigSection()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    QString cfgFile = dir.filePath("Haxa5Camera/Hexa5CameraConfig.json");

    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly)) {
        overlaySections_[static_cast<int>(OverlaySection::Config)] =
            QStringLiteral("No configuration saved yet");
        return;
    }

    QByteArray data = f.readAll();
    f.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        overlaySections_[static_cast<int>(OverlaySection::Config)] =
            QStringLiteral("Invalid configuration file");
        return;
    }

    QJsonObject obj = doc.object();
    QString text;

    // Legacy format
    if (obj.contains("cameraType")) {
        QString cameraType = obj.value("cameraType").toString("siyi").toUpper();
        text = QString("LEGACY FORMAT\nCamera Type: %1\n").arg(cameraType);

        if (cameraType == "SIYI") {
            QString ip   = obj.value("ip").toString("N/A");
            int    port  = obj.value("port").toInt(0);
            QString path = obj.value("path").toString("N/A");
            text += QString("IP: %1\nPort: %2\nPath: %3").arg(ip).arg(port).arg(path);
            if (ip != "N/A" && port > 0 && path != "N/A")
                text += QString("\nRTSP: rtsp://%1:%2%3").arg(ip).arg(port).arg(path);
        } else if (cameraType == "AI") {
            QString ip   = obj.value("aiCameraIP").toString("N/A");
            int    port  = obj.value("aiControlPort").toInt(0);
            QString path = obj.value("path").toString("/ai/stream");
            text += QString("Camera IP: %1\nControl Port: %2\nPath: %3").arg(ip).arg(port).arg(path);
            if (ip != "N/A" && port > 0)
                text += QString("\nRTSP: rtsp://%1:%2%3").arg(ip).arg(port).arg(path);
        }

        overlaySections_[static_cast<int>(OverlaySection::Config)] = text;
        return;
    }

    // New parallel format
    QString videoSource = obj.value("videoSource").toString("siyi").toUpper();
    text = QString("[Configuration]\nVideo Source: %1").arg(videoSource);

    // SIYI
    if (obj.contains("siyiConfig")) {
        QJsonObject sc = obj.value("siyiConfig").toObject();
        bool compressor = sc.value("compressorMode").toBool(false);
        int    port  = sc.value("port").toInt(0);
        QString path = sc.value("path").toString("N/A");

        text += QStringLiteral("\n\n[SIYI Camera]");
        if (compressor) {
            QString videoIp   = sc.value("videoIP").toString("N/A");
            QString controlIp = sc.value("controlIP").toString("N/A");
            text += QString("\nMode: Compressor\nVideo IP: %1\nControl IP: %2\nPort: %3\nPath: %4")
                        .arg(videoIp).arg(controlIp).arg(port).arg(path);
            if (videoIp != "N/A" && port > 0 && path != "N/A")
                text += QString("\nRTSP: rtsp://%1:%2%3").arg(videoIp).arg(port).arg(path);
        } else {
            QString ip = sc.value("ip").toString("N/A");
            text += QString("\nIP: %1\nPort: %2\nPath: %3").arg(ip).arg(port).arg(path);
            if (ip != "N/A" && port > 0 && path != "N/A")
                text += QString("\nRTSP: rtsp://%1:%2%3").arg(ip).arg(port).arg(path);
        }
        if (videoSource == "SIYI")
            text += QStringLiteral("\n>> Active Source");
    }

    // AI
    if (obj.contains("aiConfig")) {
        QJsonObject ac = obj.value("aiConfig").toObject();
        QString ip   = ac.value("cameraIP").toString("N/A");
        int    port  = ac.value("controlPort").toInt(0);
        QString path = ac.value("path").toString("N/A");

        text += QString("\n\n[AI Camera]\nCamera IP: %1\nControl Port: %2\nPath: %3").arg(ip).arg(port).arg(path);
        if (ip != "N/A" && port > 0 && path != "N/A")
            text += QString("\nRTSP: rtsp://%1:%2%3").arg(ip).arg(port).arg(path);
        if (videoSource == "AI")
            text += QStringLiteral("\n>> Active Source");
    }

    // Servo (legacy)
    if (obj.contains("servoConfig")) {
        QJsonObject sv = obj.value("servoConfig").toObject();
        text += QString("\n\n[Servo]\nIP: %1\nPort: %2")
                .arg(sv.value("servoIP").toString("N/A"))
                .arg(sv.value("servoPort").toInt(0));
    }

    overlaySections_[static_cast<int>(OverlaySection::Config)] = text;
}

// ---------------------------------------------------------------------------
// Section 1 — Video stream analysis
// ---------------------------------------------------------------------------
void MainWindow::refreshOverlayVideoSection()
{
    int idx = static_cast<int>(OverlaySection::VideoStream);

    bool playing = videoWidget && videoWidget->getReceiver()
                   && videoWidget->getReceiver()->isPlaying();

    if (!playing || videoCharacteristics.isEmpty()) {
        overlaySections_[idx].clear();
        return;
    }

    overlaySections_[idx] = QString("[Video Stream Analysis]\n%1").arg(videoCharacteristics);
}

// ---------------------------------------------------------------------------
// Section 2 — Bandwidth monitoring
// ---------------------------------------------------------------------------
void MainWindow::refreshOverlayBandwidthSection()
{
    int idx = static_cast<int>(OverlaySection::Bandwidth);

    if (currentBandwidthStats_.frame_count == 0) {
        overlaySections_[idx].clear();
        return;
    }

    const auto& s = currentBandwidthStats_;
    QString text = QStringLiteral("[Bandwidth Monitoring]");
    text += QString("\nCurrent: %1 kbps (%2 Mbps)")
            .arg(s.current_kbps, 0, 'f', 1)
            .arg(s.current_kbps / 1000.0, 0, 'f', 2);
    text += QString("\nAverage: %1 kbps (%2 Mbps)")
            .arg(s.average_kbps, 0, 'f', 1)
            .arg(s.average_kbps / 1000.0, 0, 'f', 2);
    text += QString("\nPeak: %1 kbps (%2 Mbps)")
            .arg(s.peak_kbps, 0, 'f', 1)
            .arg(s.peak_kbps / 1000.0, 0, 'f', 2);
    text += QString("\nTotal: %1 KB (%2 MB)")
            .arg(s.total_bytes / 1024)
            .arg(s.total_bytes / (1024 * 1024));
    text += QString("\nFrames: %1").arg(s.frame_count);
    text += QString("\nElapsed: %1 s").arg(s.elapsed_seconds, 0, 'f', 1);

    overlaySections_[idx] = text;
}

// ---------------------------------------------------------------------------
// Section 3 — Connectivity status
// ---------------------------------------------------------------------------
void MainWindow::refreshOverlayConnectivitySection()
{
    int idx = static_cast<int>(OverlaySection::Connectivity);

    QString text = QStringLiteral("[Connectivity Status]");

    // --- Stream Health (from VideoReceiver buffer probe) ---
    {
        bool streamActive = videoWidget && videoWidget->getReceiver()
                            && videoWidget->getReceiver()->isPlaying();

        if (streamActive && currentStreamHealth_.totalBuffersReceived > 0) {
            QString flowIcon = currentStreamHealth_.dataFlowing
                               ? QStringLiteral("🟢") : QStringLiteral("🔴");
            text += QString("\n%1 Stream: %2  (last buf %3s ago)")
                    .arg(flowIcon)
                    .arg(currentStreamHealth_.dataFlowing
                         ? QStringLiteral("data flowing")
                         : QStringLiteral("stalled"))
                    .arg(currentStreamHealth_.secondsSinceLastBuffer, 0, 'f', 1);
        } else if (streamActive) {
            text += QStringLiteral("\n⏳ Stream: waiting for data...");
        } else {
            text += QStringLiteral("\n⚫ Stream: not active");
        }
    }

    // --- Host Reachability (ping-based) ---
    if (!pingWatcher) {
        text += QStringLiteral("\nPing: initializing...");
        overlaySections_[idx] = text;
        return;
    }

    QMap<QString, QString> cameraIps = loadAllCameraIps();
    if (cameraIps.isEmpty()) {
        text += QStringLiteral("\nNo cameras configured");
        overlaySections_[idx] = text;
        return;
    }

    int reachableCount = 0;
    int totalCount = cameraIps.size();

    for (auto it = cameraIps.begin(); it != cameraIps.end(); ++it) {
        HostConnectivityScore score = pingWatcher->getConnectivityScore(it.key());

        QString icon = score.isReachable ? QStringLiteral("🟢") : QStringLiteral("🔴");
        QString info = score.isReachable
                       ? QString("%1ms").arg(score.currentRtt)
                       : QString("%1 failures").arg(score.consecutiveFailures);

        text += QString("\n%1 %2: %3 (%4%)")
                .arg(icon, it.key(), info)
                .arg(score.overallScore);

        if (score.isReachable) reachableCount++;
    }

    text += QString("\nSummary: %1/%2 reachable").arg(reachableCount).arg(totalCount);
    text += QString("\nUpdated: %1").arg(QDateTime::currentDateTime().toString("hh:mm:ss"));

    overlaySections_[idx] = text;
}


bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Let focused QLineEdit handle typing - pass through to allow text input
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) &&
        qobject_cast<QLineEdit*>(qApp->focusWidget()))
    {
        return QMainWindow::eventFilter(watched, event);
    }

    // Only intercept keys when in keyboard mode for camera control
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        && inputMode == InputMode::Keyboard)
    {
        if (event->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent*>(event));
            return true;
        } else {
            keyReleaseEvent(static_cast<QKeyEvent*>(event));
            return true;
        }
    }
    
    // ── ROI mouse handling on videoWidget ──
    if (m_roiActive && watched == videoWidget) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_roiDragging = true;
                m_roiOrigin = me->pos();
                showRoiRect(QRect(m_roiOrigin, QSize(1, 1)));
                return true;
            } else if (me->button() == Qt::RightButton) {
                m_roiDragging = false;
                hideRoiRect();
                onRoiReset();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_roiDragging) {
            auto *me = static_cast<QMouseEvent*>(event);
            showRoiRect(QRect(m_roiOrigin, me->pos()).normalized());
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_roiDragging) {
                m_roiDragging = false;
                QRect sel = QRect(m_roiOrigin, me->pos()).normalized();
                hideRoiRect();
                // Ignore tiny accidental clicks
                if (sel.width() >= 10 && sel.height() >= 10) {
                    qreal w = videoWidget->width();
                    qreal h = videoWidget->height();
                    QRectF norm(sel.x() / w, sel.y() / h,
                                sel.width() / w, sel.height() / h);
                    norm = norm.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
                    onRoiSelected(norm);
                }
                return true;
            }
        }
    }

    // Handle hover expansion for right panel
    if (event->type() == QEvent::MouseMove && watched == this) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        int mouseX = mouseEvent->position().x();
        int windowWidth = this->width();
        int edgeThreshold = 50; // 50px from right edge
        
        // Check if mouse is near right edge
        if (mouseX > (windowWidth - edgeThreshold)) {
            // Start hover timer if not already expanded
            if (!m_panelExpanded && m_hoverTimer && !m_hoverTimer->isActive()) {
                m_hoverTimer->start();
            }
        } else {
            // Stop hover timer if mouse moved away from edge
            if (m_hoverTimer && m_hoverTimer->isActive()) {
                m_hoverTimer->stop();
            }
            
            // Collapse panel if mouse is far from edge and panel is expanded
            if (m_panelExpanded && mouseX < (windowWidth - edgeThreshold - 100)) {
                if (m_panelAnimation) {
                    m_panelAnimation->setDirection(QPropertyAnimation::Backward);
                    m_panelAnimation->start();
                }
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}


void MainWindow::onOverlayModeChanged(int index)
{
    currentOverlayMode_ = static_cast<OverlayMode>(index);

    // Manage the 1-second timer for RealTimeStats
    if (currentOverlayMode_ == OverlayMode::RealTimeStats) {
        if (!realtimeStatsTimer_) {
            realtimeStatsTimer_ = new QTimer(this);
            realtimeStatsTimer_->setInterval(1000);
            connect(realtimeStatsTimer_, &QTimer::timeout, this, [this]() {
                if (currentOverlayMode_ == OverlayMode::RealTimeStats) {
                    refreshOverlayBandwidthSection();
                    refreshOverlayConnectivitySection();
                    renderOverlay();
                }
            });
        }
        realtimeStatsTimer_->start();
    } else {
        if (realtimeStatsTimer_) {
            realtimeStatsTimer_->stop();
        }
    }

    // Clear all sections before switching
    for (int i = 0; i < kOverlaySectionCount; ++i)
        overlaySections_[i].clear();

    updateConfigDisplay();
}

void MainWindow::setVideoCharacteristics(const QString& characteristics)
{
    videoCharacteristics = characteristics;
    if (currentOverlayMode_ == OverlayMode::StreamConfig) {
        refreshOverlayVideoSection();
        renderOverlay();
    }
}

void MainWindow::onBandwidthUpdated(const BandwidthStats& stats)
{
    currentBandwidthStats_ = stats;
    // Note: RealTimeStats updates are handled by the 1-second timer.
    // No need to render on every bandwidth callback to avoid excessive redraws.
}

void MainWindow::onStreamHealthUpdated(const StreamHealthInfo& info)
{
    currentStreamHealth_ = info;
    // Consumed by refreshOverlayConnectivitySection via the 1-second timer.
}

void MainWindow::saveDefaultConfig() {
    // Default configuration values:
    const QString defaultIP = QStringLiteral("192.168.144.25");
    const int defaultPort = 8554;
    const QString defaultPath = QStringLiteral("/main.264");
    const QString defaultServoIP = QStringLiteral("10.14.11.1");
    const int defaultServoPort = 8000;

    // Determine which page is currently active so we can set cameraType properly
    QString cameraType = "siyi"; // default

    if (ui->cameraTypeStack) {
        QWidget* cur = ui->cameraTypeStack->currentWidget();
        if (cur == ui->page_servo) cameraType = "servo";
        else cameraType = "siyi";
    }

    // Build JSON object using values from the UI (if present) or the defaults
    QJsonObject obj;

    if (cameraType == "servo") {
        // Use servo page values if available, otherwise use defaults
        QString ip = (ui->servo_lineEditIP && !ui->servo_lineEditIP->text().isEmpty()) ? ui->servo_lineEditIP->text().trimmed() : defaultIP;
        int port = (ui->servo_lineEditPort && ui->servo_lineEditPort->text().toInt() > 0) ? ui->servo_lineEditPort->text().toInt() : defaultPort;
        QString path = (ui->servo_lineEditPath && !ui->servo_lineEditPath->text().isEmpty()) ? ui->servo_lineEditPath->text().trimmed() : defaultPath;
        QString sIP = (ui->servo_lineEditServoIP && !ui->servo_lineEditServoIP->text().isEmpty()) ? ui->servo_lineEditServoIP->text().trimmed() : defaultServoIP;
        int sPort = (ui->servo_lineEditServoPort && ui->servo_lineEditServoPort->text().toInt() > 0) ? ui->servo_lineEditServoPort->text().toInt() : defaultServoPort;

        obj["cameraType"] = cameraType;
        obj["ip"] = ip;
        obj["port"] = port;
        obj["path"] = path;
        obj["servoIP"] = sIP;
        obj["servoPort"] = sPort;
    } else {
        // SIYI defaults
        QString ip = (ui->siyi_lineEditIP && !ui->siyi_lineEditIP->text().isEmpty()) ? ui->siyi_lineEditIP->text().trimmed() : defaultIP;
        int port = (ui->siyi_lineEditPort && ui->siyi_lineEditPort->text().toInt() > 0) ? ui->siyi_lineEditPort->text().toInt() : defaultPort;
        QString path = (ui->siyi_lineEditPath && !ui->siyi_lineEditPath->text().isEmpty()) ? ui->siyi_lineEditPath->text().trimmed() : defaultPath;

        obj["cameraType"] = cameraType;
        obj["ip"] = ip;
        obj["port"] = port;
        obj["path"] = path;
        // ensure servo fields removed so config matches SIYI-only
    }

    QJsonDocument doc(obj);

    // Get the configuration directory (e.g., ~/.config)
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString subFolder = "Haxa5Camera";
    QDir configDirectory(configDir);
    if (!configDirectory.exists(subFolder)) {
        if (!configDirectory.mkdir(subFolder)) {
            statusBar()->showMessage("Failed to create config subdirectory", 3000);
            qWarning() << "Could not create subfolder" << subFolder << "in" << configDir;
            return;
        }
    }

    QString configFile = configDirectory.filePath(subFolder + "/Hexa5CameraConfig.json");

    QFile file(configFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        statusBar()->showMessage("Default configuration saved", 3000);
        qDebug() << "Saved default config to " << configFile;
    } else {
        statusBar()->showMessage("Failed to open config file for writing", 3000);
        qWarning() << "Failed to open file" << configFile;
    }

    // Apply config (video & controller will be restarted)
    applyConfig();
    // Recreate controller to pick up cameraType immediately
    //createCameraControllerFromConfig();
    return;
}



void MainWindow::handleCommandFeedback(const QString& commandId, bool success) {
    QString cmdName;
    if (commandId == "07") {  // From message.h's GIMBAL_ROTATION
        cmdName = "Gimbal Rotation";
    } else if (commandId == "0f") {  // From message.h's ABSOLUTE_ZOOM
        cmdName = "Absolute Zoom";
    } else {
        cmdName = "Unknown Command";
    }
    statusBar()->showMessage(QString("%1: %2").arg(cmdName).arg(success ? "Success" : "Failed"), 3000);
}


void killExistingInstances_() {
    // Get our own binary name from /proc/self/exe so it works regardless
    // of what the binary is called (JoystickIdentifier, HexaCam, etc.)
    pid_t self = getpid();
    char exePath[4096] = {};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) return;
    exePath[len] = '\0';

    // Extract just the filename (basename)
    std::string fullPath(exePath);
    std::string binaryName = fullPath.substr(fullPath.rfind('/') + 1);

    // Find all processes matching this binary name, excluding ourselves
    std::string cmd = "pgrep -f " + binaryName;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    std::vector<pid_t> pids;
    char buffer[64];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        try {
            pid_t pid = static_cast<pid_t>(std::stoi(std::string(buffer)));
            if (pid != self) pids.push_back(pid);
        } catch (...) {}
    }
    pclose(pipe);

    // First try SIGTERM for graceful shutdown
    for (pid_t pid : pids) {
        kill(pid, SIGTERM);
    }

    // Give them 2 seconds, then SIGKILL any survivors
    if (!pids.empty()) {
        usleep(2000000);
        for (pid_t pid : pids) {
            if (kill(pid, 0) == 0) {  // still alive?
                kill(pid, SIGKILL);
                qDebug() << "[Shutdown] Force-killed lingering process" << pid;
            }
        }
    }
}

void MainWindow::showEvent(QShowEvent *event) {
    // 1) always forward to base
    QMainWindow::showEvent(event);

    // 2) your existing grabKeyboard()
    //grabKeyboard();

}

void MainWindow::resizeEvent(QResizeEvent *ev) {
    QMainWindow::resizeEvent(ev);
    // always keep the splash sized to fill:
    if (splashVideo && splashVideo->isVisible()) {
        splashVideo->setGeometry(ui->centralwidget->rect());
    }

    if (recordOverlay && recordOverlay->isVisible()) {
        recordOverlay->setFixedWidth(videoWidget->width());
        recordOverlay->move(0, 0);
    }

}

void MainWindow::closeEvent(QCloseEvent* ev)
{
    {
        QMutexLocker lk(&shutdownMutex);
        if (isShuttingDown) { ev->accept(); return; }
        isShuttingDown = true;
    }

    qDebug() << "[Shutdown] closeEvent: starting graceful shutdown";

    // 1) Stop the recording pipeline if active
    if (recordPipeline) {
        gst_element_send_event(recordPipeline, gst_event_new_eos());
        GstBus *bus = gst_element_get_bus(recordPipeline);
        if (bus) {
            GstMessage *msg = gst_bus_timed_pop_filtered(bus, 2 * GST_SECOND,
                static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            if (msg) gst_message_unref(msg);
            gst_object_unref(bus);
        }
        gst_element_set_state(recordPipeline, GST_STATE_NULL);
        gst_object_unref(recordPipeline);
        recordPipeline = nullptr;
    }

    // 2) Stop the ping watcher
    if (pingWatcher) {
        pingWatcher->stopWatching();
    }

    // 3) Stop the video pipeline
    if (videoWidget) {
        auto *rcv = videoWidget->getReceiver();
        if (rcv) rcv->stop();
    }

    // 4) Kill other instances of this binary (not ourselves)
    killExistingInstances_();

    // 5) Finish closing
    QMainWindow::closeEvent(ev);
    QCoreApplication::quit();
}


void MainWindow::onCameraStarted() {
    ui->lineEditCameraStatus->setText("Camera Working");
    ui->lineEditCameraStatus->setStyleSheet(
        "background-color: #ccffcc; color: darkgreen;");
    auto *vr = videoWidget->getReceiver();
    vr->setWindowId(videoWidget->winId());
}

void MainWindow::onCameraError(const QString &message) {
    ui->lineEditCameraStatus->setText(message);
    ui->lineEditCameraStatus->setStyleSheet(
        "background-color: #ff4444; color: white;");
    // Clear video characteristics and bandwidth stats when stream stops
    videoCharacteristics.clear();
    currentBandwidthStats_ = BandwidthStats{};
    if (currentOverlayMode_ != OverlayMode::Off) {
        overlaySections_[static_cast<int>(OverlaySection::VideoStream)].clear();
        overlaySections_[static_cast<int>(OverlaySection::Bandwidth)].clear();
        renderOverlay();
    }
}

#include <QProcess>

QString MainWindow::loadControlIp() const
{
    // where VideoReceiver already looks:
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString subFolder = "Haxa5Camera";
    QDir dir(configDir);
    QString cfgFile = dir.filePath(subFolder + "/Hexa5CameraConfig.json");

    // defaults in case JSON is missing or invalid
    const QString defaultIp = QString::fromUtf8("192.168.144.25");

    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly))
        return defaultIp;

    auto data = f.readAll();
    f.close();

    auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return defaultIp;

    auto obj = doc.object();

    // Handle new parallel format
    if (obj.contains("siyiConfig")) {
        QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
        bool compressorMode = siyiConfig.value("compressorMode").toBool(false);
        if (compressorMode) {
            return siyiConfig.value("controlIP").toString(defaultIp);
        }
        return siyiConfig.value("ip").toString(defaultIp);
    }

    // Legacy fallback
    return obj.value("ip").toString(defaultIp);
}

QMap<QString, QString> MainWindow::loadAllCameraIps() const
{
    QMap<QString, QString> cameraIps;
    
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString subFolder = "Haxa5Camera";
    QDir dir(configDir);
    QString cfgFile = dir.filePath(subFolder + "/Hexa5CameraConfig.json");

    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly))
        return cameraIps;

    auto data = f.readAll();
    f.close();

    auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return cameraIps;

    auto obj = doc.object();
    
    // Extract SIYI camera IP(s)
    if (obj.contains("siyiConfig")) {
        QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
        bool compressorMode = siyiConfig.value("compressorMode").toBool(false);
        if (compressorMode) {
            QString videoIp = siyiConfig.value("videoIP").toString();
            QString controlIp = siyiConfig.value("controlIP").toString();
            if (!videoIp.isEmpty()) {
                cameraIps["SIYI-Video"] = videoIp;
            }
            if (!controlIp.isEmpty()) {
                cameraIps["SIYI-Control"] = controlIp;
            }
        } else {
            QString siyiIp = siyiConfig.value("ip").toString();
            if (!siyiIp.isEmpty()) {
                cameraIps["SIYI"] = siyiIp;
            }
        }
    }
    
    // Extract AI camera IP
    if (obj.contains("aiConfig")) {
        QJsonObject aiConfig = obj.value("aiConfig").toObject();
        QString aiIp = aiConfig.value("cameraIP").toString();
        if (!aiIp.isEmpty()) {
            cameraIps["AI"] = aiIp;
        }
    }
    
    // Extract Servo camera IP
    if (obj.contains("servoConfig")) {
        QJsonObject servoConfig = obj.value("servoConfig").toObject();
        QString servoIp = servoConfig.value("ip").toString();
        if (!servoIp.isEmpty()) {
            cameraIps["Servo"] = servoIp;
        }
    }
    
    LOG_IP_WATCHDOG() << "Found" << cameraIps.size() << "camera IPs:" << cameraIps;
    return cameraIps;
}

QString MainWindow::getCurrentVideoSource() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString subFolder = "Haxa5Camera";
    QDir dir(configDir);
    QString cfgFile = dir.filePath(subFolder + "/Hexa5CameraConfig.json");

    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly))
        return "siyi"; // default

    auto data = f.readAll();
    f.close();

    auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return "siyi"; // default

    auto obj = doc.object();
    return obj.value("videoSource").toString("siyi").toLower();
}

void MainWindow::refreshCameraStatus() {
    // The continuous ping watcher handles monitoring autonomously.
    // Only (re-)initialize if it isn't running yet.
    if (!pingWatcher || !pingWatcher->isWatching()) {
        initializePingWatcher();
    }
}

void MainWindow::refreshAllCameraStatus() {
    // Same as refreshCameraStatus — the watcher is self-sustaining.
    if (!pingWatcher || !pingWatcher->isWatching()) {
        initializePingWatcher();
    }
}

#include <signal.h>  // for SIGINT
#include <unistd.h>  // for ::kill

void MainWindow::on_RecordButton_clicked()
{
    // 0) if the camera isn’t active, warn and bail
    if (auto *vr = videoWidget->getReceiver()) {
        if (!vr->isPlaying()) {
            QMessageBox::warning(
                this,
                tr("Recording unavailable"),
                tr("The camera stream is not active.\nRecording is unavailable.")
                );
            return;
        }
    }

    // Always use the current URI from the receiver (rtspUri member may be stale)
    QString uri;
    if (auto *vr = videoWidget->getReceiver()) {
        uri = vr->getRtspUriFromConfig().trimmed();
    }
    if (uri.isEmpty()) {
        QMessageBox::warning(this, tr("Recording"), tr("No RTSP URI configured."));
        return;
    }

    if (recordState == RecordState::Idle) {
        // ── START RECORDING ──

        // 2) prepare path
        QString dir = QDir::homePath() + "/Hexa5CameraRecordedVideos";
        QDir().mkpath(dir);
        QString fn = QDateTime::currentDateTime()
                         .toString("yyyyMMdd_hhmmss") + ".mp4";
        lastRecordPath = dir + "/" + fn;

        // 3) Build a GStreamer recording pipeline that opens its own RTSP
        //    TCP session and remuxes the H.264 stream into MP4 (no re-encode).
        //    fragment-duration enables fragmented MP4 so the file is playable
        //    even if recording is interrupted unexpectedly.
        {
        QString pipelineDesc = QString(
            "rtspsrc location=%1 protocols=tcp latency=200 ! "
            "rtph264depay ! h264parse ! "
            "mp4mux fragment-duration=1000 ! "
            "filesink location=%2"
        ).arg(uri, lastRecordPath);

        qDebug() << "[Record] launching GStreamer pipeline:" << pipelineDesc;

        GError *gstErr = nullptr;
        recordPipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &gstErr);
        if (!recordPipeline || gstErr) {
            QString errMsg = gstErr ? QString::fromUtf8(gstErr->message) : "Unknown error";
            if (gstErr) g_error_free(gstErr);
            QMessageBox::warning(this, tr("Recording"),
                tr("Could not create recording pipeline:\n%1").arg(errMsg));
            recordPipeline = nullptr;
            return;
        }
        }

        GstStateChangeReturn ret = gst_element_set_state(recordPipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            QMessageBox::warning(this, tr("Recording"),
                tr("Could not start recording — check URI and network."));
            gst_object_unref(recordPipeline);
            recordPipeline = nullptr;
            return;
        }

        // 5) update UI
        recordOverlay->setFixedWidth(videoWidget->width());
        recordOverlay->move(0, 0);
        recordState = RecordState::Recording;
        ui->RecordButton->setText(tr("Stop Recording"));
        recordClock.start();
        recordOverlay->setText(tr("● REC   00:00"));
        recordOverlay->show();
        recordUiTimer->start();
        statusBar()->showMessage(tr("🔴 Recording started"), 2000);

    } else {
        // ── STOP RECORDING ──

        recordUiTimer->stop();

        if (recordProcess) {
            // ask ffmpeg to finish cleanly (SIGINT == Ctrl+C)
            qint64 pid = recordProcess->processId();
            if (pid > 0) ::kill(pid, SIGINT);

            // give it up to 5 s to write the trailer
            if (!recordProcess->waitForFinished(5000)) {
                recordProcess->kill();
                recordProcess->waitForFinished();
            }

            delete recordProcess;
            recordProcess = nullptr;
        }

        // restore UI
        recordOverlay->hide();
        recordState = RecordState::Idle;
        ui->RecordButton->setText(tr("Start Recording"));
        statusBar()->showMessage(
            tr("Recording saved to:\n%1").arg(lastRecordPath),
            5000
            );
    }
}


void MainWindow::updateRecordTime()
{
    int total = recordClock.elapsed() / 1000;
    int m = (total / 60) % 60;
    int s = total % 60;
    recordOverlay->setText(
        QString("● REC   %1:%2")
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0')));
}



void MainWindow::onRecordingFinished(int exitCode,
                                     QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);

    // Clean up the process object
    recProcess->deleteLater();
    recProcess = nullptr;

    isRecording = false;
    ui->RecordButton->setEnabled(true);
    ui->RecordButton->setText("Start Recording");
    recordOverlay->hide();

    QMessageBox::information(
        this,
        "Recording Complete",
        QString("Saved to:\n%1").arg(currentRecordPath));
}


#include <QGuiApplication>
#include <QScreen>


void MainWindow::on_ScreenshotButton_clicked()
{
    // 1) prepare directory & filename
    QString dir = QDir::homePath() + "/Hexa5CameraScreenshots";
    QDir().mkpath(dir);
    QString fn = QDateTime::currentDateTime()
                     .toString("yyyyMMdd_hhmmss") + ".png";
    QString fullPath = dir + "/" + fn;

    // 2) grab the current frame from the GStreamer pipeline
    //    (QScreen::grabWindow cannot capture xvimagesink's X11 overlay)
    VideoReceiver *vr = videoWidget ? videoWidget->getReceiver() : nullptr;
    if (!vr) {
        statusBar()->showMessage("No video receiver available!", 3000);
        return;
    }

    QImage frame = vr->grabFrame();
    if (frame.isNull()) {
        statusBar()->showMessage("Screenshot failed — no frame available!", 3000);
        return;
    }

    // 3) save to disk
    if (!frame.save(fullPath, "PNG")) {
        statusBar()->showMessage("Screenshot failed — could not save file!", 3000);
        return;
    }

    // 4) feedback
    statusBar()->showMessage(
        QString("Screenshot saved to:\n%1").arg(fullPath),
        5000
        );
}




void MainWindow::playIntro(const QString& splashUrl, const QString& css) {
    QUrl videoUrl;

    // Always try local file first
    if (QFile::exists(splashUrl)) {
        videoUrl = QUrl::fromLocalFile(splashUrl);
        qDebug() << "Using local file:" << splashUrl;
    }
    // Then try resource path
    else if (splashUrl.startsWith("qrc:") || QFile::exists(":" + splashUrl)) {
        videoUrl = QUrl(splashUrl);
        qDebug() << "Using resource path:" << splashUrl;
    }
    // Fallback to embedded resource
    else {
        videoUrl = QUrl("qrc:/intro.mp4");
        qWarning() << "Using fallback resource video";
    }

    qDebug() << "Final video URL:" << videoUrl;
    qDebug() << "Video exists:" << QFile::exists(videoUrl.toLocalFile());

    // 1) overlay video widget
    auto* vw = new QVideoWidget(this);
    vw->setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
    vw->setGeometry(this->rect());
    vw->show();

    // 2) player
    auto* player = new QMediaPlayer(this);
    player->setVideoOutput(vw);
    player->setSource(videoUrl);  // Use the validated URL

    // 3) when it’s done...
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this, vw, player, css](auto st){
                if (st == QMediaPlayer::EndOfMedia) {
                    player->stop();
                    vw->deleteLater();
                    player->deleteLater();

                    // now reveal and style
                    this->showMaximized();
                    this->setStyleSheet(css);
                    ui->controlsContainer->show();
                    ui->toggleButton     ->show();
                    statusBar()         ->show();
                    this->releaseKeyboard();
                }
            });

    // 4) kick it off
    player->play();

    //Error Handling
    connect(player, &QMediaPlayer::errorOccurred, this, [](auto error, auto errorString) {
        qWarning() << "Media player error:" << error << "-" << errorString;
    });
}

QString MainWindow::loadServoIp() const
{
    // same config file as camera
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    QString cfgFile = dir.filePath(QStringLiteral("Haxa5Camera/Hexa5CameraConfig.json"));

    const QString defaultIp = QStringLiteral("10.14.11.1");
    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly)) return defaultIp;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())    return defaultIp;
    return doc.object().value(QStringLiteral("servoIP")).toString(defaultIp);
}

int MainWindow::loadServoPort() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    QString cfgFile = dir.filePath(QStringLiteral("Haxa5Camera/Hexa5CameraConfig.json"));

    const int defaultPort = 8000;
    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly)) return defaultPort;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())    return defaultPort;
    return doc.object().value(QStringLiteral("servoPort")).toInt(defaultPort);
}


// Synchronous, safe apply: do everything on the GUI thread in a deterministic order.
void MainWindow::applyConfig()
{
    static int callCount = 0;
    qDebug() << "[VIDEO_SOURCE] applyConfig() called - call #" << ++callCount;
    
    // Simple guard to avoid re-entrancy
    static bool applying = false;
    if (applying) {
        qDebug() << "[VIDEO_SOURCE] applyConfig() already in progress, skipping";
        statusBar()->showMessage("Apply already in progress", 2000);
        return;
    }
    applying = true;

    // Disable UI save/default buttons while applying to prevent concurrent clicks
    if (ui->btnSiyiDefault)      ui->btnSiyiDefault->setEnabled(false);
    if (ui->btnServoDefault)     ui->btnServoDefault->setEnabled(false);
    if (ui->btnSiyiSave)         ui->btnSiyiSave->setEnabled(false);
    if (ui->btnServoSave)        ui->btnServoSave->setEnabled(false);

    statusBar()->showMessage("Applying configuration...", 2000);
    qDebug() << "[CONFIG] applyConfig: starting - shutting down previous video source";

    // 1) Stop all command timers and processes
    if (commandTimer && commandTimer->isActive()) {
        commandTimer->stop();
        qDebug() << "[CONFIG] applyConfig: commandTimer stopped";
    }

    // 2) Stop VideoReceiver synchronously (safe on main thread)
    if (videoWidget) {
        if (auto *rcv = videoWidget->getReceiver()) {
            qDebug() << "[CONFIG] applyConfig: stopping VideoReceiver pipeline";
            // This is synchronous; it sets pipeline to NULL and unrefs elements.
            rcv->stop();

            // Give a short breathing room for GStreamer threads to tear down;
            // this reduces races when we immediately create a new pipeline.
            QThread::msleep(200);
            qDebug() << "[CONFIG] applyConfig: VideoReceiver stopped, pipeline destroyed";
        }
    }

    // 3) Stop and destroy existing camera controller (if any)
    if (cameraController) {
        qDebug() << "[CONFIG] applyConfig: stopping existing cameraController";
        try {
            cameraController->stop();
            qDebug() << "[CONFIG] applyConfig: cameraController stopped successfully";
        } catch (...) {
            qWarning() << "[CONFIG] applyConfig: exception while stopping cameraController";
        }
        cameraController.reset();
        qDebug() << "[CONFIG] applyConfig: cameraController destroyed";
    }

    // 4) Ensure all IP pinging and monitoring processes are stopped
    qDebug() << "[CONFIG] applyConfig: all previous video source processes stopped";

    // 5) Recreate the controller from persisted config
    qDebug() << "[CONFIG] applyConfig: creating new cameraController from config";
    try {
        createCameraControllerFromConfig();
        qDebug() << "[CONFIG] applyConfig: cameraController created successfully";
    } catch (const std::exception &ex) {
        qWarning() << "[CONFIG] createCameraControllerFromConfig threw:" << ex.what();
        QMessageBox::warning(this, tr("Camera Error"),
                             tr("Failed to create camera controller:\n%1").arg(ex.what()));
    } catch (...) {
        qWarning() << "[CONFIG] createCameraControllerFromConfig unknown exception";
        QMessageBox::warning(this, tr("Camera Error"),
                             tr("Failed to create camera controller (unknown error)."));
    }

    // 6) Restart VideoReceiver synchronously with new RTSP URI
    if (videoWidget) {
        if (auto *rcv = videoWidget->getReceiver()) {
            QString newUri = rcv->getRtspUriFromConfig().trimmed();
            qDebug() << "[CONFIG] applyConfig: setting RTSP URI to" << newUri;
            // set the new URI and start pipeline
            rcv->setRtspUri(newUri);

            qDebug() << "[CONFIG] applyConfig: starting VideoReceiver with new video source";
            rcv->start();
            qDebug() << "[CONFIG] applyConfig: VideoReceiver started with new RTSP URI";
        }
    }

    // Re-enable UI buttons
    if (ui->btnSiyiDefault)      ui->btnSiyiDefault->setEnabled(true);
    if (ui->btnServoDefault)     ui->btnServoDefault->setEnabled(true);
    if (ui->btnSiyiSave)         ui->btnSiyiSave->setEnabled(true);
    if (ui->btnServoSave)        ui->btnServoSave->setEnabled(true);

    // Restart command timer if controller present
    if (commandTimer && !commandTimer->isActive()) {
        commandTimer->start(50);
        qDebug() << "[CONFIG] applyConfig: commandTimer restarted";
    }


    statusBar()->showMessage("Configuration applied", 3000);
    qDebug() << "[CONFIG] applyConfig: finished";

    applying = false;
    
    // Initialize ping watcher after configuration is applied
    initializePingWatcher();
}



void MainWindow::createCameraControllerFromConfig()
{
    qDebug() << "[VIDEO_SOURCE] createCameraControllerFromConfig() called";
    
    // config file path (same folder VideoReceiver used)
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    dir.mkpath("Haxa5Camera");
    QString cfgFile = dir.filePath("Haxa5Camera/Hexa5CameraConfig.json");
    qDebug() << "[VIDEO_SOURCE] Reading config from:" << cfgFile;

    QString chosenType = "siyi"; // default
    QString ip = "10.14.11.3";
    int port = 8554;
    QString path = "/main.264";
    QString servoIP;
    int servoPort = 0;

    QFile f(cfgFile);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray data = f.readAll();
        f.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();

            // Check if this is old format for backward compatibility
            if (obj.contains("cameraType")) {
                // Handle old single-camera format
                chosenType = obj.value("cameraType").toString("siyi").toLower();
                ip = obj.value("ip").toString(ip);
                port = obj.value("port").toInt(port);
                path = obj.value("path").toString(path);
                servoIP = obj.value("servoIP").toString();
                servoPort = obj.value("servoPort").toInt();
                
                // Auto-migrate to new format
                qDebug() << "[VIDEO_SOURCE] createCameraControllerFromConfig() calling saveConfig() for old format migration";
                saveConfig();
                
            } else {
                // Handle new parallel format
                QString videoSource = obj.value("videoSource").toString("siyi").toLower();
                chosenType = videoSource;
                
                if (videoSource == "ai" && obj.contains("aiConfig")) {
                    // Load AI configuration
                    QJsonObject aiConfig = obj.value("aiConfig").toObject();
                    ip = aiConfig.value("cameraIP").toString(ip);
                    port = aiConfig.value("controlPort").toInt(port);
                    path = aiConfig.value("path").toString(path);
                    
                } else if (videoSource == "siyi" && obj.contains("siyiConfig")) {
                    // Load SIYI configuration
                    QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
                    bool compressorMode = siyiConfig.value("compressorMode").toBool(false);
                    if (compressorMode) {
                        // In compressor mode, controlIP is used for SDK
                        ip = siyiConfig.value("controlIP").toString(ip);
                    } else {
                        ip = siyiConfig.value("ip").toString(ip);
                    }
                    port = siyiConfig.value("port").toInt(port);
                    path = siyiConfig.value("path").toString(path);
                    qDebug() << "[CONFIG] SIYI compressorMode:" << compressorMode << "SDK IP:" << ip;
                    
                } else {
                    // Fallback to SIYI defaults if selected config not found
                    qWarning() << "[CONFIG] Selected video source" << videoSource << "not found in config, using SIYI defaults";
                    chosenType = "siyi";
                }
                
                // Load servo config if present (legacy support)
                if (obj.contains("servoConfig")) {
                    QJsonObject servoConfig = obj.value("servoConfig").toObject();
                    servoIP = servoConfig.value("servoIP").toString();
                    servoPort = servoConfig.value("servoPort").toInt();
                }
            }
        }
    } else {
        qWarning() << "Could not open config file:" << cfgFile << "; using defaults";
    }

    // Tear down existing controller
    if (cameraController) {
        cameraController->stop();
        cameraController.reset();
    }

    if (chosenType == "servo" && !servoIP.isEmpty()) {
        cameraController = std::make_unique<ServoCameraController>(servoIP.toStdString(), servoPort ? servoPort : port);
    } else {
        // default to SIYI
        cameraController = std::make_unique<SiyiCameraController>(ip.toStdString(), 37260);
    }

    // Wire callbacks BEFORE start() so early events are not missed
    cameraController->onStarted = [this]() {
        QMetaObject::invokeMethod(this, "onCameraStarted", Qt::QueuedConnection);
    };
    cameraController->onError = [this](const QString &msg) {
        QMetaObject::invokeMethod(this, [this, msg]() { onCameraError(msg); }, Qt::QueuedConnection);
    };

    if (!cameraController->start()) {
        qWarning() << "Failed to start cameraController";
        // keep it null to avoid using a half-started controller
        cameraController.reset();
        statusBar()->showMessage("Camera controller failed to start", 3000);
    }

}

void MainWindow::initializeCameraController()
{
    createCameraControllerFromConfig();

    // Verify controller started successfully
    if (cameraController && cameraController->isRunning()) {
        qDebug() << "Camera controller started successfully";
        statusBar()->showMessage("Camera controller ready", 3000);
    } else {
        qWarning() << "Failed to start camera controller";
        statusBar()->showMessage("Camera controller failed to start", 3000);
    }
}

void MainWindow::initializePingWatcher() {
    // Check if ping watcher is already properly initialized
    if (pingWatcher && pingWatcher->isWatching()) {
        return;
    }

    qDebug() << "[PING_WATCHER] Initializing continuous ping watcher";
    
    // Clean up existing ping watcher only if it exists
    if (pingWatcher) {
        qDebug() << "[PING_WATCHER] Cleaning up existing ping watcher";
        pingWatcher->stopWatching();
        pingWatcher->deleteLater();
        pingWatcher = nullptr;
    }
    
    // Create new ping watcher
    pingWatcher = new ContinuousPingWatcher(this);
    
    // Configure ping settings
    pingWatcher->setPingInterval(3000); // Ping every 3 seconds
    pingWatcher->setTimeout(1000);      // 1 second timeout per ping
    
    // Connect signals
    connect(pingWatcher, &ContinuousPingWatcher::hostStatusChanged,
            this, &MainWindow::onHostStatusChanged);
    connect(pingWatcher, &ContinuousPingWatcher::hostError,
            this, &MainWindow::onHostError);
    connect(pingWatcher, &ContinuousPingWatcher::connectivityScoreUpdated,
            this, &MainWindow::onConnectivityScoreUpdated);
    
    // Load all camera IPs from config and add them to watcher
    QMap<QString, QString> cameraIps = loadAllCameraIps();
    for (auto it = cameraIps.begin(); it != cameraIps.end(); ++it) {
        QString cameraType = it.key();
        QString cameraIp = it.value();
        pingWatcher->addHost(cameraType, cameraIp);
    }
    
    // Start continuous monitoring
    if (!cameraIps.isEmpty()) {
        pingWatcher->startWatching();
        qDebug() << "[PING_WATCHER] Started monitoring" << cameraIps.size() << "cameras";
    }
    
    // Initial display update
    updateConnectivityDisplay();
    
    // Set up periodic display updates (every 10 seconds) - only create once
    static QTimer* displayTimer = nullptr;
    if (!displayTimer) {
        displayTimer = new QTimer(this);
        connect(displayTimer, &QTimer::timeout, this, &MainWindow::updateConnectivityDisplay);
        displayTimer->start(10000); // Update every 10 seconds
        qDebug() << "[PING_WATCHER] Display timer initialized";
    }
}

void MainWindow::onHostStatusChanged(const QString& name, bool reachable, int roundTripTime) {
    // This signal now only fires when status actually changes (reachable ↔ unreachable)
    qDebug() << "[PING_WATCHER]" << name << (reachable ? "became REACHABLE" : "became UNREACHABLE")
             << "RTT:" << roundTripTime << "ms";
    
    // Update the connectivity display when status changes
    updateConnectivityDisplay();
    
    // Update connectivity section in overlay
    if (currentOverlayMode_ == OverlayMode::RealTimeStats) {
        refreshOverlayConnectivitySection();
        renderOverlay();
    }
    
    // Notify the VideoReceiver about reachability of the active camera.
    // This suppresses useless pipeline restarts when the host is down,
    // and triggers an immediate pipeline restart when it comes back.
    QString currentVideoSource = getCurrentVideoSource().toUpper();
    if (name.toUpper() == currentVideoSource && videoWidget) {
        VideoReceiver* receiver = videoWidget->getReceiver();
        if (receiver) {
            receiver->setStreamReachable(reachable);
        }
    }

    if (!reachable) {
        QString errorMsg = QStringLiteral("%1 Camera unreachable (ping failed)").arg(name);
        onCameraError(errorMsg);
    }
}

void MainWindow::onHostError(const QString& name, const QString& error) {
    if (error.isEmpty()) return;  // reachable transition, nothing to report
    qWarning() << "[PING_WATCHER]" << name << "ping error:" << error;
    QString errorMsg = QStringLiteral("%1 Camera ping error: %2").arg(name, error);
    onCameraError(errorMsg);
}

void MainWindow::onConnectivityScoreUpdated(const QString& name, const HostConnectivityScore& score) {
    // Update the camera status display with comprehensive information
    QString statusText;
    QString statusStyle;

    if (score.isReachable) {
        // Camera is reachable - show clean status
        statusText = QString("%1: 🟢 Connected | Ping: %2ms")
                    .arg(name)
                    .arg(score.currentRtt);

        // Color based on score/performance (using theme colors)
        if (score.overallScore >= 80) {
            statusStyle = "color: #9ece6a; font-weight: bold;"; // Success Green
        } else if (score.overallScore >= 60) {
            statusStyle = "color: #bb9af7; font-weight: bold;"; // Good Purple
        } else if (score.overallScore >= 40) {
            statusStyle = "color: #e0af68; font-weight: bold;"; // Warning Orange
        } else {
            statusStyle = "color: #f7768e; font-weight: bold;"; // Error Red
        }
    } else {
        // Camera is unreachable
        statusText = QString("%1: 🔴 Disconnected | Failures: %2")
                    .arg(name)
                    .arg(score.consecutiveFailures);

        statusStyle = "color: #f7768e; font-weight: bold;"; // Error Red
    }

    // Add additional details in tooltip (Keep detailed info here)
    QString tooltip = QString("Host: %1 (%2)\n"
                             "Status: %3\n"
                             "Overall Score: %4/100\n"
                             "Reliability: %5% (%6/%7 pings)\n"
                             "Performance: %8% (Avg RTT: %9ms)\n"
                             "Stability: %10% (%11 consecutive %12)\n"
                             "Last seen: %13")
                    .arg(score.hostName)
                    .arg(score.hostAddress)
                    .arg(score.isReachable ? "Reachable" : "Unreachable")
                    .arg(score.overallScore)
                    .arg(score.reliabilityScore)
                    .arg(score.successfulPings)
                    .arg(score.totalPings)
                    .arg(score.performanceScore)
                    .arg(score.averageRtt)
                    .arg(score.stabilityScore)
                    .arg(score.isReachable ? score.consecutiveSuccesses : score.consecutiveFailures)
                    .arg(score.isReachable ? "successes" : "failures")
                    .arg(QDateTime::fromMSecsSinceEpoch(score.lastSeen).toString("hh:mm:ss"));

    // Update the status display
    if (ui->lineEditCameraStatus) {
        ui->lineEditCameraStatus->setText(statusText);
        ui->lineEditCameraStatus->setStyleSheet(statusStyle);
        ui->lineEditCameraStatus->setToolTip(tooltip);
        // Ensure cursor is at start to show the most relevant info if truncated
        ui->lineEditCameraStatus->setCursorPosition(0);
    }

    // Update status bar with summary
    QString currentVideoSource = getCurrentVideoSource();
    if ((name.toUpper() == "SIYI" && currentVideoSource == "siyi") ||
        (name.toUpper() == "AI" && currentVideoSource == "ai") ||
        (name.toUpper() == "SERVO" && currentVideoSource == "servo")) {

        QString statusBarMsg = QString("%1 Camera - %2 | Reliability: %3%")
                              .arg(name)
                              .arg(score.isReachable ? "Connected" : "Disconnected")
                              .arg(score.reliabilityScore);

        statusBar()->showMessage(statusBarMsg, 5000);
    }

    LOG_UI_STATUS() << "Updated" << name << "connectivity display:"
             << "Score:" << score.overallScore
             << "Reachable:" << score.isReachable
             << "RTT:" << score.currentRtt;

    // Check for low connectivity and handle video shutdown
    checkAndHandleLowConnectivity(name, score);

    // Update connectivity section in overlay
    if (currentOverlayMode_ == OverlayMode::RealTimeStats) {
        refreshOverlayConnectivitySection();
        renderOverlay();
    }
}

void MainWindow::checkAndHandleLowConnectivity(const QString& name, const HostConnectivityScore& score) {
    // Only apply shutdown logic to the currently selected video source
    QString currentVideoSource = getCurrentVideoSource().toUpper();
    if (name.toUpper() != currentVideoSource) {
        return;
    }

    bool currentlyShutdown = videoShutdownStates.value(name, false);

    // Minimum consecutive failures required before shutting down video
    static const int MIN_CONSECUTIVE_FAILURES_FOR_SHUTDOWN = 9999;

    bool shouldShutdown = !score.isReachable &&
                          score.consecutiveFailures >= MIN_CONSECUTIVE_FAILURES_FOR_SHUTDOWN;

    if (shouldShutdown && !currentlyShutdown) {
        LOG_VIDEO_SHUTDOWN() << "Camera" << name << "connection lost - shutting down video";

        videoShutdownStates[name] = true;

        if (videoWidget) {
            VideoReceiver* receiver = videoWidget->getReceiver();
            if (receiver) {
                receiver->stop();
            }
            videoWidget->hide();
        }

        statusBar()->showMessage(QString("Camera %1 video disabled - connection lost").arg(name), 5000);
        updateConnectivityDisplay();

    } else if (score.isReachable && currentlyShutdown) {
        LOG_VIDEO_RESTORE() << "Camera" << name << "connection restored - restoring video";

        videoShutdownStates[name] = false;

        if (videoWidget && !rtspUri.isEmpty()) {
            VideoReceiver* receiver = videoWidget->getReceiver();
            if (receiver) {
                receiver->setRtspUri(rtspUri);
                receiver->start();
            }
            videoWidget->show();
        }

        statusBar()->showMessage(QString("Camera %1 video restored").arg(name), 3000);
        updateConnectivityDisplay();
    }
}

void MainWindow::updateConnectivityDisplay() {
    LOG_UI_STATUS() << "Starting connectivity display update";

    if (!pingWatcher) {
        if (ui->lineEditCameraStatus) {
            ui->lineEditCameraStatus->setText("Initializing connectivity monitor...");
            ui->lineEditCameraStatus->setStyleSheet("color: #e0af68; font-weight: bold;"); // Warning Orange
            ui->lineEditCameraStatus->setToolTip("Connectivity monitoring is starting up");
        }
        return;
    }

    // Get all configured camera IPs
    QMap<QString, QString> cameraIps = loadAllCameraIps();

    if (cameraIps.isEmpty()) {
        if (ui->lineEditCameraStatus) {
            ui->lineEditCameraStatus->setText("No cameras configured");
            ui->lineEditCameraStatus->setStyleSheet("color: #e0af68; font-weight: bold;"); // Warning
            ui->lineEditCameraStatus->setToolTip("No camera configurations found in settings");
        }
        return;
    }

    // Show summary of all camera statuses
    QStringList statusList;
    int reachableCount = 0;
    int totalCount = cameraIps.size();

    for (auto it = cameraIps.begin(); it != cameraIps.end(); ++it) {
        QString cameraType = it.key();
        QString cameraIp = it.value();

        HostConnectivityScore score = pingWatcher->getConnectivityScore(cameraType);

        if (score.isReachable) {
            reachableCount++;
            statusList << QString("%1:🟢%2ms").arg(cameraType.left(3)).arg(score.currentRtt);
        } else {
            statusList << QString("%1:🔴").arg(cameraType.left(3));
        }
    }

    // Create summary display
    QString summaryText = QString("Cameras: %1/%2 Online | %3")
                         .arg(reachableCount)
                         .arg(totalCount)
                         .arg(statusList.join(" | "));

    QString summaryStyle;
    if (reachableCount == totalCount) {
        summaryStyle = "color: #9ece6a; font-weight: bold;"; // Success Green
    } else if (reachableCount > 0) {
        summaryStyle = "color: #e0af68; font-weight: bold;"; // Warning Orange
    } else {
        summaryStyle = "color: #f7768e; font-weight: bold;"; // Error Red
    }

    // Update the display
    if (ui->lineEditCameraStatus) {
        ui->lineEditCameraStatus->setText(summaryText);
        ui->lineEditCameraStatus->setStyleSheet(summaryStyle);
        ui->lineEditCameraStatus->setCursorPosition(0);
        LOG_UI_STATUS() << "Display updated with text:" << summaryText;
    }

    // Update connectivity section in overlay
    if (currentOverlayMode_ == OverlayMode::RealTimeStats) {
        refreshOverlayConnectivitySection();
        renderOverlay();
    }
}

// Mode file watchdog implementation
void MainWindow::initializeModeFileWatcher(const QString& filePath) {
    if (filePath.isEmpty()) {
        qDebug() << "[MODE_WATCHER] No mode file path provided";
        return;
    }
    
    modeFilePath = filePath;
    
    // Create file system watcher
    if (!modeFileWatcher) {
        modeFileWatcher = new QFileSystemWatcher(this);
        connect(modeFileWatcher, &QFileSystemWatcher::fileChanged,
                this, &MainWindow::onModeFileChanged);
    }
    
    // Add file to watch list
    if (QFile::exists(filePath)) {
        modeFileWatcher->addPath(filePath);
        qDebug() << "[MODE_WATCHER] Watching mode file:" << filePath;
        
        // Read initial mode
        onModeFileChanged(filePath);
    } else {
        qDebug() << "[MODE_WATCHER] Mode file does not exist yet:" << filePath;
        // Watch the directory instead to detect when file is created
        QFileInfo fileInfo(filePath);
        QString dir = fileInfo.absolutePath();
        if (QDir(dir).exists()) {
            modeFileWatcher->addPath(dir);
        }
    }
    
    // Create toast label
    if (!modeToastLabel) {
        modeToastLabel = new QLabel(this);
        modeToastLabel->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
        modeToastLabel->setAttribute(Qt::WA_TranslucentBackground);
        modeToastLabel->setStyleSheet(R"(
            QLabel {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(45, 52, 68, 240),
                    stop:1 rgba(30, 35, 48, 240));
                color: #ffffff;
                font-size: 16px;
                font-weight: bold;
                padding: 16px 24px;
                border-radius: 12px;
                border: 2px solid rgba(100, 180, 255, 0.6);
            }
        )");
        modeToastLabel->setAlignment(Qt::AlignCenter);
        modeToastLabel->setMinimumWidth(350);
        modeToastLabel->hide();
    }
    
    // Create toast timer
    if (!toastTimer) {
        toastTimer = new QTimer(this);
        toastTimer->setSingleShot(true);
        connect(toastTimer, &QTimer::timeout, this, &MainWindow::hideToast);
    }
    
    // Create fade animation for toast
    if (!toastFadeAnimation) {
        toastFadeAnimation = new QPropertyAnimation(modeToastLabel, "windowOpacity", this);
        toastFadeAnimation->setDuration(300);
    }
}

void MainWindow::onModeFileChanged(const QString& path) {
    // Re-add the file to watch list (some systems remove it after change)
    if (modeFileWatcher && !modeFileWatcher->files().contains(path)) {
        if (QFile::exists(path)) {
            modeFileWatcher->addPath(path);
        }
    }
    
    // If it was a directory change, check if our file now exists
    if (QFileInfo(path).isDir()) {
        if (QFile::exists(modeFilePath)) {
            modeFileWatcher->addPath(modeFilePath);
            onModeFileChanged(modeFilePath);
        }
        return;
    }
    
    // Read and parse the JSON file
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "[MODE_WATCHER] Could not open mode file:" << path;
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "[MODE_WATCHER] Invalid JSON in mode file";
        return;
    }
    
    QJsonObject obj = doc.object();
    int mode = obj.value("mode").toInt(0);
    QString description = obj.value("description").toString();
    
    qDebug() << "[MODE_WATCHER] Mode changed - Mode:" << mode << "Description:" << description;
    
    // Show toast notification
    showModeToast(mode, description);
}

void MainWindow::showModeToast(int mode, const QString& description) {
    if (!modeToastLabel) {
        return;
    }
    
    // Style based on mode
    QString modeIcon;
    QString modeColor;
    QString modeName;
    
    if (mode == 1) {
        modeIcon = " ";
        modeColor = "#4CAF50";  // Green for drone mode
        modeName = "DRONE MODE";
    } else if (mode == 2) {
        modeIcon = " ";
        modeColor = "#2196F3";  // Blue for camera mode
        modeName = "CAMERA MODE";
    } else {
        modeIcon = " ";
        modeColor = "#FF9800";  // Orange for unknown
        modeName = QString("MODE %1").arg(mode);
    }
    
    // Update toast style with mode-specific color
    modeToastLabel->setStyleSheet(QString(R"(
        QLabel {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(45, 52, 68, 245),
                stop:1 rgba(30, 35, 48, 245));
            color: #ffffff;
            font-size: 14px;
            font-weight: bold;
            padding: 16px 24px;
            border-radius: 12px;
            border: 3px solid %1;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
        }
    )").arg(modeColor));
    
    // Set toast content
    QString toastHtml = QString(
        "<div style='text-align: center;'>"
        "<span style='font-size: 28px;'>%1</span><br>"
        "<span style='color: %2; font-size: 18px;'>%3</span><br>"
        "<span style='color: #aaaaaa; font-size: 12px;'>%4</span>"
        "</div>"
    ).arg(modeIcon).arg(modeColor).arg(modeName).arg(description);
    
    modeToastLabel->setText(toastHtml);
    modeToastLabel->adjustSize();
    
    // Position toast at top center of the main window
    QPoint windowCenter = this->geometry().center();
    int toastX = windowCenter.x() - modeToastLabel->width() / 2;
    int toastY = this->geometry().top() + 60;
    modeToastLabel->move(toastX, toastY);
    
    // Show toast with fade-in effect
    modeToastLabel->setWindowOpacity(0);
    modeToastLabel->show();
    modeToastLabel->raise();
    
    toastFadeAnimation->stop();
    toastFadeAnimation->setStartValue(0.0);
    toastFadeAnimation->setEndValue(1.0);
    toastFadeAnimation->start();
    
    // Start timer to hide after 3 seconds
    toastTimer->start(3000);
    
    // Also show in status bar
    statusBar()->showMessage(QString("Mode switched to: %1").arg(modeName), 3000);
}

void MainWindow::hideToast() {
    if (!modeToastLabel || !toastFadeAnimation) {
        return;
    }
    
    // Fade out animation
    toastFadeAnimation->stop();
    toastFadeAnimation->setStartValue(1.0);
    toastFadeAnimation->setEndValue(0.0);
    toastFadeAnimation->start();
    
    // Hide after animation completes
    connect(toastFadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (modeToastLabel && toastFadeAnimation->direction() == QPropertyAnimation::Forward) {
            // Only hide if we were fading out (endValue was 0)
            if (toastFadeAnimation->endValue().toDouble() < 0.5) {
                modeToastLabel->hide();
            }
        }
    });
}

// ── ROI Zoom ─────────────────────────────────────────────────────────────────
// Uses QRubberBand (lightweight, no background paint) + eventFilter for mouse.
// QRubberBand draws only a border rectangle and never covers the video surface.

void MainWindow::setupRoiOverlay()
{
    m_roiCalc.setBaseFov(62.0f, 37.0f);
    m_roiCalc.setZoomRange(MIN_ZOOM, MAX_ZOOM);
    m_roiActive = false;
    m_roiDragging = false;
}

void MainWindow::onRoiToggled(bool active)
{
    m_roiActive = active;
    m_roiDragging = false;

    if (!videoWidget) return;

    // Lazy-create the 4 border edges and install event filter once
    if (!m_roiEdge[0]) {
        const QString edgeStyle = "background-color: rgba(70, 130, 230, 220);";
        for (int i = 0; i < 4; ++i) {
            m_roiEdge[i] = new QFrame(videoWidget);
            m_roiEdge[i]->setStyleSheet(edgeStyle);
            m_roiEdge[i]->setFrameShape(QFrame::NoFrame);
            m_roiEdge[i]->hide();
        }
        videoWidget->installEventFilter(this);
        videoWidget->setMouseTracking(true);
    }

    if (active) {
        videoWidget->setCursor(Qt::CrossCursor);
        statusBar()->showMessage("ROI Zoom: drag a rectangle on the video. Right-click to reset.", 5000);
    } else {
        hideRoiRect();
        videoWidget->setCursor(Qt::ArrowCursor);
        statusBar()->showMessage("ROI Zoom deactivated", 2000);
    }

    if (m_roiToggleBtn) {
        m_roiToggleBtn->setChecked(active);
    }
}

void MainWindow::showRoiRect(const QRect &r)
{
    if (!m_roiEdge[0]) return;
    const int t = 2; // border thickness
    // top edge
    m_roiEdge[0]->setGeometry(r.x(), r.y(), r.width(), t);
    // bottom edge
    m_roiEdge[1]->setGeometry(r.x(), r.bottom() - t + 1, r.width(), t);
    // left edge
    m_roiEdge[2]->setGeometry(r.x(), r.y(), t, r.height());
    // right edge
    m_roiEdge[3]->setGeometry(r.right() - t + 1, r.y(), t, r.height());
    for (int i = 0; i < 4; ++i) {
        m_roiEdge[i]->show();
        m_roiEdge[i]->raise();
    }
}

void MainWindow::hideRoiRect()
{
    for (int i = 0; i < 4; ++i)
        if (m_roiEdge[i]) m_roiEdge[i]->hide();
}

void MainWindow::onRoiSelected(const QRectF &normalizedRect)
{
    if (!cameraController || !cameraController->supportsRoiZoom()) {
        statusBar()->showMessage("ROI zoom not supported for this camera", 3000);
        return;
    }

    (void)QtConcurrent::run([this, normalizedRect]() {
        auto [yaw, pitch, roll] = cameraController->getGimbalAttitude();
        Q_UNUSED(roll);

        RoiZoomCommand cmd = m_roiCalc.compute(yaw, pitch, currentZoom, normalizedRect);

        qDebug() << "[ROI] Current: yaw=" << yaw << " pitch=" << pitch << " zoom=" << currentZoom;
        qDebug() << "[ROI] Target:  yaw=" << cmd.targetYaw << " pitch=" << cmd.targetPitch
                 << " zoom=" << cmd.targetZoom;

        cameraController->setGimbalAngles(cmd.targetYaw, cmd.targetPitch);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        cameraController->setAbsoluteZoom(cmd.targetZoom, 1);

        float finalYaw   = cmd.targetYaw;
        float finalPitch = cmd.targetPitch;
        float finalZoom  = cmd.targetZoom;
        QMetaObject::invokeMethod(this, [this, finalYaw, finalPitch, finalZoom]() {
            currentZoom = finalZoom;
            statusBar()->showMessage(
                QString("ROI zoom: yaw=%1° pitch=%2° zoom=%3×")
                    .arg(finalYaw, 0, 'f', 1)
                    .arg(finalPitch, 0, 'f', 1)
                    .arg(finalZoom, 0, 'f', 1),
                4000);
        }, Qt::QueuedConnection);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        cameraController->requestAutofocus();
    });
}

void MainWindow::onRoiReset()
{
    hideRoiRect();

    if (!cameraController) return;

    (void)QtConcurrent::run([this]() {
        cameraController->requestGimbalCenter();
        cameraController->setAbsoluteZoom(MIN_ZOOM, 1);

        QMetaObject::invokeMethod(this, [this]() {
            currentZoom = MIN_ZOOM;
            statusBar()->showMessage("ROI reset: 1× zoom, gimbal centered", 3000);
        }, Qt::QueuedConnection);

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cameraController->requestAutofocus();
    });
}

void MainWindow::switchToVideoStream() {
    if (mainStackedWidget && btnVideoStream && btnRecords) {
        mainStackedWidget->setCurrentIndex(0);
        btnVideoStream->setStyleSheet("background-color: #3d59a1; color: white; font-weight: bold; padding: 6px 15px; border-radius: 4px;");
        btnRecords->setStyleSheet("background-color: #24283b; color: #a9b1d6; font-weight: bold; padding: 6px 15px; border-radius: 4px;");
    }
    if (recordsRefreshTimer) {
        recordsRefreshTimer->stop();
    }
}

void MainWindow::switchToRecords() {
    if (mainStackedWidget && btnVideoStream && btnRecords) {
        mainStackedWidget->setCurrentIndex(1);
        btnRecords->setStyleSheet("background-color: #3d59a1; color: white; font-weight: bold; padding: 6px 15px; border-radius: 4px;");
        btnVideoStream->setStyleSheet("background-color: #24283b; color: #a9b1d6; font-weight: bold; padding: 6px 15px; border-radius: 4px;");
    }
    refreshRecordsList();
    if (recordsRefreshTimer) {
        recordsRefreshTimer->start(1000);
    }
}

void MainWindow::refreshRecordsList() {
    if (!recordsListWidget) return;

    QString screensDir = QDir::homePath() + "/Hexa5CameraScreenshots";
    QString videosDir = QDir::homePath() + "/Hexa5CameraRecordedVideos";

    QDir sDir(screensDir);
    QDir vDir(videosDir);

    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.mp4" << "*.avi" << "*.mkv";

    QFileInfoList fileList;
    if (sDir.exists()) fileList += sDir.entryInfoList(filters, QDir::Files, QDir::Time);
    if (vDir.exists()) fileList += vDir.entryInfoList(filters, QDir::Files, QDir::Time);

    std::sort(fileList.begin(), fileList.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });

    // Only rebuild if there's a file list change
    bool dirty = false;
    if (fileList.size() != recordsListWidget->count()) {
        dirty = true;
    } else {
        for (int i = 0; i < fileList.size(); ++i) {
            if (recordsListWidget->item(i)->data(Qt::UserRole).toString() != fileList[i].absoluteFilePath()) {
                dirty = true;
                break;
            }
        }
    }

    if (!dirty) return;

    recordsListWidget->clear();
    for (const QFileInfo& fi : fileList) {
        QListWidgetItem *item = new QListWidgetItem(recordsListWidget);
        item->setText(fi.fileName());
        QString ext = fi.suffix().toLower();
        if (ext == "mp4" || ext == "avi" || ext == "mkv") {
            QPixmap pix(160, 120);
            pix.fill(QColor(30, 30, 30));
            QPainter p(&pix);
            p.setPen(Qt::white);
            QFont font = p.font();
            font.setBold(true);
            font.setPointSize(12);
            p.setFont(font);
            p.drawText(pix.rect(), Qt::AlignCenter, "VIDEO\n" + ext.toUpper());
            item->setIcon(QIcon(pix));
        } else {
            QPixmap pix(fi.absoluteFilePath());
            item->setIcon(QIcon(pix.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        recordsListWidget->addItem(item);
    }
}

void MainWindow::onRecordItemClicked(QListWidgetItem *item) {
    if (!item) return;
    QString filePath = item->data(Qt::UserRole).toString();
    QFileInfo fi(filePath);
    if (!fi.exists()) return;
    QString ext = fi.suffix().toLower();

    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(fi.fileName());
    dialog->setWindowFlags(dialog->windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(5);

    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        QLabel *label = new QLabel(dialog);
        QPixmap pix(filePath);
        label->setPixmap(pix.scaled(1024, 768, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        dialog->resize(1024, 768);
        
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    } else if (ext == "mp4" || ext == "avi" || ext == "mkv") {
        layout->setContentsMargins(5, 5, 5, 5);
        layout->setSpacing(5);

        QVideoWidget *videoWidget = new QVideoWidget(dialog);
        layout->addWidget(videoWidget, 1);
        
        QMediaPlayer *player = new QMediaPlayer(dialog);
        QAudioOutput *audioOutput = new QAudioOutput(dialog);
        player->setAudioOutput(audioOutput);
        player->setVideoOutput(videoWidget);
        player->setSource(QUrl::fromLocalFile(filePath));

        QHBoxLayout *controlLayout = new QHBoxLayout();
        controlLayout->setContentsMargins(0, 0, 0, 0);
        
        QSlider *positionSlider = new QSlider(Qt::Horizontal, dialog);
        positionSlider->setRange(0, 0);

        QPushButton *playPauseBtn = new QPushButton(dialog);
        playPauseBtn->setIcon(dialog->style()->standardIcon(QStyle::SP_MediaPause));
        
        QPushButton *skipBtn = new QPushButton(dialog);
        skipBtn->setIcon(dialog->style()->standardIcon(QStyle::SP_MediaSeekForward));

        controlLayout->addWidget(playPauseBtn);
        controlLayout->addWidget(skipBtn);
        controlLayout->addWidget(positionSlider);

        layout->addLayout(controlLayout);
        
        auto togglePlayPause = [player]() {
            if (player->playbackState() == QMediaPlayer::PlayingState) {
                player->pause();
            } else {
                player->play();
            }
        };

        connect(playPauseBtn, &QPushButton::clicked, togglePlayPause);

        connect(player, &QMediaPlayer::playbackStateChanged, [dialog, playPauseBtn](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::PlayingState) {
                playPauseBtn->setIcon(dialog->style()->standardIcon(QStyle::SP_MediaPause));
            } else {
                playPauseBtn->setIcon(dialog->style()->standardIcon(QStyle::SP_MediaPlay));
            }
        });

        std::shared_ptr<bool> wasPlaying = std::make_shared<bool>(false);

        connect(positionSlider, &QSlider::sliderPressed, [player, wasPlaying]() {
            *wasPlaying = (player->playbackState() == QMediaPlayer::PlayingState);
            player->pause();
        });

        connect(positionSlider, &QSlider::sliderMoved, [player](int position) {
            player->setPosition(position);
        });

        connect(positionSlider, &QSlider::sliderReleased, [player, wasPlaying]() {
            if (*wasPlaying) {
                player->play();
            }
        });

        connect(player, &QMediaPlayer::positionChanged, [positionSlider](qint64 position) {
            if (!positionSlider->isSliderDown()) {
                positionSlider->setValue(position);
            }
        });

        connect(player, &QMediaPlayer::durationChanged, [positionSlider](qint64 duration) {
            positionSlider->setMaximum(duration);
        });

        connect(skipBtn, &QPushButton::clicked, [player]() {
            player->setPosition(player->position() + 10000); // 10s skip
        });

        // Ensure player is stopped/deleted on close
        connect(dialog, &QDialog::finished, [player]() {
            player->stop();
        });

        dialog->resize(1024, 768);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        player->play();
    } else {
        delete dialog;
        return;
    }
}


