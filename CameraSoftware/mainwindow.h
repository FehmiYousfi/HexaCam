#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "logging_config.h"
#include <QtWidgets/QMainWindow>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtCore/QTime>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QStackedWidget>
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
#include <QtCore/QFileSystemWatcher>
#include "servo_client.hpp"
#include "ServoWorker.h"
#include "CameraController.h"
#include "VideoReceiver/VideoReceiver.h"
#include "RoiZoomCalculator.h"
#include <QtWidgets/QPushButton>
#include <QtWidgets/QFrame>

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
    void initializeModeFileWatcher(const QString& filePath);


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
    void onOverlayModeChanged(int index);
    void setVideoCharacteristics(const QString& characteristics);
    void onBandwidthUpdated(const BandwidthStats& stats);
    void onStreamHealthUpdated(const StreamHealthInfo& info);
    void onModeFileChanged(const QString& path);
    void hideToast();
    void onRoiSelected(const QRectF &normalizedRect);
    void onRoiReset();
    void switchToVideoStream();
    void switchToRecords();
    void refreshRecordsList();
    void onRecordItemClicked(QListWidgetItem *item);


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
    int lastZoomLevel = 0;
    QElapsedTimer zoomRepeatTimer;
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

    // UI setup methods (implemented in mainwindow_ui_setup.cpp)
    void setupStyles();
    void setupLayout();
    void setupConnections();
    void setupVideoWidget();
    void setupRecordingOverlay();
    void setupConfigOverlayLabel();

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
    static const int LOW_CONNECTIVITY_THRESHOLD = 30; // Deprecated: No longer used for video shutdown. Video now only stops when connection is lost (isReachable=false)


    QVideoWidget  *splashVideo   = nullptr;
    QMediaPlayer  *splashPlayer  = nullptr;

    //std::unique_ptr<ServoControl::ServoClient> _servo;
    int _servoPosition = 0;
    std::unique_ptr<CameraController> cameraController;

    // Configuration validation and display
    bool configValid = false;
    QString configErrors;
    QLabel* configDisplayLabel = nullptr;

    // Overlay mode: 0=Off, 1=StreamConfig, 2=RealTimeStats
    enum class OverlayMode : int { Off = 0, StreamConfig = 1, RealTimeStats = 2 };
    OverlayMode currentOverlayMode_ = OverlayMode::Off;
    QTimer* realtimeStatsTimer_ = nullptr;   // 1-second timer for RealTimeStats mode

    // --- Section-based overlay controller ---
    // Each module writes to its own named section; renderOverlay() joins them in order.
    enum class OverlaySection : int {
        Config       = 0,   // Camera config (SIYI / AI / Servo from JSON)
        VideoStream  = 1,   // Video stream analysis (codec, resolution, etc.)
        Bandwidth    = 2,   // Bandwidth monitoring (kbps, frames, etc.)
        Connectivity = 3,   // Ping / connectivity status
        _Count
    };
    static constexpr int kOverlaySectionCount = static_cast<int>(OverlaySection::_Count);
    QString overlaySections_[kOverlaySectionCount];   // indexed by OverlaySection

    void setOverlaySection(OverlaySection section, const QString& text);
    void clearOverlaySection(OverlaySection section);
    void renderOverlay();                             // schedules a coalesced render
    void doRenderOverlay();                           // actual render (called by timer)
    QTimer* overlayRenderTimer_ = nullptr;            // coalescing timer

    // Dedicated section builders (each writes only its own section)
    void refreshOverlayConfigSection();
    void refreshOverlayVideoSection();
    void refreshOverlayBandwidthSection();
    void refreshOverlayConnectivitySection();

    // Video characteristics from stream analysis
    QString videoCharacteristics;

    // Bandwidth monitoring (matching RtspAnalyzer/HexaRtspCompressor)
    BandwidthStats currentBandwidthStats_;

    // Stream health (data-flow check from VideoReceiver probe)
    StreamHealthInfo currentStreamHealth_;

    // Right panel hover expansion
    QTimer* m_hoverTimer = nullptr;
    QPropertyAnimation* m_panelAnimation = nullptr;
    bool m_panelExpanded = false;

    // Mode file watchdog and toast notification
    QFileSystemWatcher* modeFileWatcher = nullptr;
    QString modeFilePath;
    QLabel* modeToastLabel = nullptr;
    QTimer* toastTimer = nullptr;
    QPropertyAnimation* toastFadeAnimation = nullptr;
    void showModeToast(int mode, const QString& description);

    // ROI zoom — uses 4 thin border QFrames + eventFilter for mouse
    RoiZoomCalculator    m_roiCalc;
    QPushButton*         m_roiToggleBtn = nullptr;
    bool                 m_roiActive = false;
    bool                 m_roiDragging = false;
    QPoint               m_roiOrigin;
    QFrame*              m_roiEdge[4] = {};  // top, bottom, left, right
    void setupRoiOverlay();
    void onRoiToggled(bool active);
    void showRoiRect(const QRect &r);
    void hideRoiRect();

    QStackedWidget* mainStackedWidget = nullptr;
    QListWidget* recordsListWidget = nullptr;
    QTimer* recordsRefreshTimer = nullptr;
    QPushButton* btnVideoStream = nullptr;
    QPushButton* btnRecords = nullptr;
};



#endif // MAINWINDOW_H

