#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "logging_config.h"
#include <QtWidgets/QMainWindow>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtCore/QTime>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidgetItem>
#include <memory>
#include <thread>
#include "thirdparty/SIYI-SDK/src/sdk.h"
#include <QtGui/QCloseEvent>
#include <QtCore/QEvent>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimediaWidgets/QVideoWidget>
#include <mutex>
#include <QtCore/QElapsedTimer>
#include <gst/gst.h>
#include <QtCore/QProcess>
#include "ping.h"
#include <QtWidgets/QToolButton>
#include <QtCore/QPropertyAnimation>
#include "servo_client.hpp"
#include "ServoWorker.h"
#include "CameraController.h"



using ServoControl::ServoClient;
namespace Ui {
class MainWindow;
}

// mainwindow.h, at the top with the other includes
class VideoRecorderWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void playIntro(const QString& splashUrl, const QString& css);


protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent* ev) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateDeviceList();
    void pollAxisValues();
    void updateAxisValues(int js, int axis, qreal value);
    void updateButtonState(int js, int button, bool pressed);
    void onJoystickItemClicked(QListWidgetItem *item);
    void onSwitchToKeyboard();
    void onSwitchToJoystick();
    void onSwitchToConfiguration();
    void sendGimbalCommands();
    void saveConfig();
    void saveConfigWithoutApply();
    void updateVideoSourceInConfig();
    void saveDefaultConfig();
    void handleCommandFeedback(const QString& commandId, bool success);
    void onCameraStarted();
    void onCameraError(const QString &msg);
    void refreshCameraStatus();
    void refreshAllCameraStatus(); // Monitor all IPs in config
    void onHostStatusChanged(const QString& name, bool reachable, int roundTripTime);
    void onHostError(const QString& name, const QString& error);
    void onConnectivityScoreUpdated(const QString& name, const HostConnectivityScore& score);
    void checkAndHandleLowConnectivity(const QString& name, const HostConnectivityScore& score);
    void initializePingWatcher();
    void updateConnectivityDisplay();
    void onJoystickAxisChanged(int device, int axis, qreal value);
    void on_RecordButton_clicked();
    void updateRecordTime();
    void onRecordingFinished(int exitCode, QProcess::ExitStatus status);
    void on_ScreenshotButton_clicked();
    void onFullUp();
    void onFullDown();
    void onFullLeft();
    void onFullRight();
    void onZoomMaxIn();
    void onZoomMaxOut();
    void onStop();
    //void onTabClicked(int index);
    QString loadServoIp() const;
    int loadServoPort() const;
    void applyConfig();
    void createCameraControllerFromConfig();
    void onSelectSiyiClicked();
    void onSelectServoClicked();
    void onSelectAiClicked();
    void onCameraChooseBack();
    void populateConfigFields();
    void onSiyiDefaultClicked();
    void onServoDefaultClicked();
    void onAiDefaultClicked();
    void initializeCameraController();
    bool validateConfiguration(const QString& cameraType);
    void updateConfigDisplay();
    void onShowConfigToggled(bool enabled);


signals:
    void servoPositionChanged(int newPosition);

private:
    Ui::MainWindow *ui;

    // Mode and joystick selection:
    enum class InputMode { None, Keyboard, Joystick, Configuration};
    InputMode inputMode = InputMode::None;
    int cameraJoystickIndex;

    // Gimbal control shared state:
    //int currentYawSpeed;
    //int currentPitchSpeed;
    float currentZoom;

    // Constants for control increments:
    static constexpr int YAW_SPEED_CONSTANT = 50;
    static constexpr int PITCH_SPEED_CONSTANT = 50;
    static constexpr float ZOOM_STEP_CONSTANT = 1.0f;
    const int MAX_SPEED = 50;
    const int MOVE_SPEED = 50; 
    const float ACCELERATION_STEP = 0.5f;
    float currentYawAccel = 0.0f;
    float currentPitchAccel = 0.0f;
    QElapsedTimer keyHoldTimer;

    QMutex commandMutex;
    QTimer *commandTimer;
    bool isInitializing = false;

    // Pointer to the SIYI SDK instance
    //SIYI_SDK* sdk;

    std::thread receiveThread;
    std::atomic<bool> keepRunning;
    std::atomic<int> currentYawSpeed{0};
    std::atomic<int> currentPitchSpeed{0};
    QMutex shutdownMutex;
    bool isShuttingDown = false;
    VideoRecorderWidget *videoWidget = nullptr;

    static constexpr float MIN_ZOOM = 1.0f;
    static constexpr float MAX_ZOOM = 30.0f;
    static constexpr float ZOOM_SPEED = 1.0f;
    int lastZoomAxis = 0;
    //const int   ZOOM_LEVELS = 5;
    int ZOOM_LEVELS = int((MAX_ZOOM - MIN_ZOOM) / ZOOM_STEP_CONSTANT + 0.5f);
    int lastZoomSign = 0;
    int lastZoomIndex = -1;
    int lastZoomLevel = -1;
    GstElement *recordPipeline = nullptr;
    QString     rtspUri;

    // For the on‐screen timer:
    QProcess         *recProcess    = nullptr;
    QElapsedTimer     recordTimer;
    //QTimer           *recordUiTimer = nullptr;
    //QLabel           *recordOverlay = nullptr;
    bool              isRecording   = false;
    QString           currentRecordPath;


    enum class RecordState { Idle, Recording };
    RecordState recordState{RecordState::Idle};
    QProcess*   recordProcess = nullptr;
    QElapsedTimer       recordClock;
    QTimer*     recordUiTimer = nullptr;
    QLabel*     recordOverlay = nullptr;
    QString     lastRecordPath;
    bool useLocalCamera = false;
    QString loadControlIp() const;
    QMap<QString, QString> loadAllCameraIps() const; // Extract all IPs from config
    QString getCurrentVideoSource() const; // Get current video source from config


    bool controlsCollapsed = true;
    bool controlsMeasured = false;
    int  controlsFullWidth = 0;
    QPropertyAnimation* controlsAnim = nullptr;
    QToolButton*        toggleControlsBtn = nullptr;
    QWidget*            controlsContainer = nullptr;
    ContinuousPingWatcher* pingWatcher = nullptr;
    QMap<QString, HostConnectivityScore> connectivityScores;
    QMap<QString, bool> videoShutdownStates; // Track which cameras have video shut down
    static const int LOW_CONNECTIVITY_THRESHOLD = 30; // Score below 30% triggers shutdown


    QVideoWidget  *splashVideo   = nullptr;
    QMediaPlayer  *splashPlayer  = nullptr;

    //std::unique_ptr<ServoControl::ServoClient> _servo;
    int _servoPosition = 0;
    std::unique_ptr<CameraController> cameraController;

    // Configuration validation and display
    bool configValid = false;
    QString configErrors;
    QLabel* configDisplayLabel = nullptr;
    bool showConfigOverlay = false;

    // Right panel hover expansion
    QTimer* m_hoverTimer = nullptr;
    QPropertyAnimation* m_panelAnimation = nullptr;
    bool m_panelExpanded = false;


};



#endif // MAINWINDOW_H

