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


//#include "servo_client.hpp"

//static const char *CONTROL_IP = "10.14.11.3";
static const int CONTROL_PORT = 37260;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      keepRunning(true),
      //sdk(nullptr),
      currentZoom(1.0f)

{
    ui->setupUi(this);
#ifdef _DEBUG
    QPushButton *dbg = new QPushButton("DBG: pan+50", this);
    dbg->setToolTip("Sends a single setGimbalSpeed(50,0) to see if gimbal moves");
    dbg->setFixedSize(110,24);
    dbg->move(10, 10); // move somewhere unobtrusive
    connect(dbg, &QPushButton::clicked, this, [this]() {
        if (cameraController && cameraController->isRunning()) {
            qDebug() << "[DBG] sending single gimbal speed 50,0";
            cameraController->setGimbalSpeed(50,0);
            QTimer::singleShot(300, this, [this]() { // stop after 300 ms
                if (cameraController) cameraController->setGimbalSpeed(0,0);
            });
        } else {
            qWarning() << "[DBG] controller not ready";
        }
    });
#endif

    qApp->installEventFilter(this);

    ui->toggleButton->setFocusPolicy(Qt::NoFocus);

    // allow children to get focus again
    this->setFocusPolicy(Qt::StrongFocus);
    // and ensure our central widget can accept focus too
    ui->centralwidget->setFocusPolicy(Qt::StrongFocus);

    // ui->lineEditIP  ->setFocusPolicy(Qt::StrongFocus);
    // ui->lineEditPort->setFocusPolicy(Qt::StrongFocus);
    // ui->lineEditPath->setFocusPolicy(Qt::StrongFocus);

    // Ensure cameraTypeStack starts on chooser page:
    if (ui->cameraTypeStack) {
        ui->cameraTypeStack->setCurrentIndex(0); // chooser page

        // Connect chooser buttons
        connect(ui->btnSelectSiyi, &QPushButton::clicked, this, &MainWindow::onSelectSiyiClicked);
        connect(ui->btnSelectServo, &QPushButton::clicked, this, &MainWindow::onSelectServoClicked);
        connect(ui->btnSelectAi, &QPushButton::clicked, this, &MainWindow::onSelectAiClicked);

        // Connect back buttons (both pages call same back slot)
        connect(ui->btnSiyiBack, &QPushButton::clicked, this, &MainWindow::onCameraChooseBack);
        connect(ui->btnServoBack, &QPushButton::clicked, this, &MainWindow::onCameraChooseBack);
        connect(ui->btnAiBack, &QPushButton::clicked, this, &MainWindow::onCameraChooseBack);

        if (ui->btnSiyiSave)
            connect(ui->btnSiyiSave, &QPushButton::clicked, this, &MainWindow::saveConfig);
        if (ui->btnServoSave)
            connect(ui->btnServoSave, &QPushButton::clicked, this, &MainWindow::saveConfig);
        if (ui->btnAiSave)
            connect(ui->btnAiSave, &QPushButton::clicked, this, &MainWindow::saveConfig);

        if (ui->btnSiyiDefault)
            connect(ui->btnSiyiDefault, &QPushButton::clicked, this, &MainWindow::onSiyiDefaultClicked);
        if (ui->btnServoDefault)
            connect(ui->btnServoDefault, &QPushButton::clicked, this, &MainWindow::onServoDefaultClicked);
        if (ui->btnAiDefault)
            connect(ui->btnAiDefault, &QPushButton::clicked, this, &MainWindow::onAiDefaultClicked);
    }

    // Connect config display checkbox
    if (ui->showConfigCheckBox) {
        connect(ui->showConfigCheckBox, &QCheckBox::toggled, this, &MainWindow::onShowConfigToggled);
    }

    // Connect video source dropdown
    if (ui->videoSourceComboBox) {
        connect(ui->videoSourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            // Skip during initialization to prevent race condition
            if (isInitializing) {
                LOG_VIDEO_SOURCE() << "Dropdown changed during initialization - skipping to prevent race condition";
                return;
            }
            
            QString selectedSource = ui->videoSourceComboBox->currentText();
            LOG_VIDEO_SOURCE() << "Dropdown changed - index:" << index << "source:" << selectedSource;
            
            // Update videoSource in JSON config directly
            LOG_VIDEO_SOURCE() << "Updating videoSource in config...";
            updateVideoSourceInConfig();
            
            // Apply the new configuration with proper shutdown/restart sequence
            LOG_VIDEO_SOURCE() << "Applying new video source with proper shutdown/restart...";
            applyConfig();
            LOG_VIDEO_SOURCE() << "Video source workflow completed";
        });
    }

    // Ensure port fields only accept numbers
    auto setPortValidator = [this](QLineEdit* le){
        if (!le) return;
        le->setValidator(new QIntValidator(1, 65535, this));
    };

    // SIYI ports
    setPortValidator(ui->siyi_lineEditPort);

    // Servo ports
    setPortValidator(ui->servo_lineEditPort);
    setPortValidator(ui->servo_lineEditServoPort);

    // When the user opens the Camera Configuration tab, populate fields
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index){
        // find tab index for Camera Configuration. If you know it's 2, check equality.
        // Here we simply check if the currently visible widget is the camera config page
        QWidget* current = ui->tabWidget->widget(index);
        if (current == ui->tabWidget /* replace with actual page widget pointer if present */) {
            populateConfigFields();
        } else {
            // Alternatively, call populateConfigFields when the user clicks the Camera Configuration button
        }
    });



    // Load configuration at startup to set dropdown correctly
    qDebug() << "[VIDEO_SOURCE] Startup: Loading configuration to initialize video source dropdown";
    isInitializing = true;
    populateConfigFields();
    isInitializing = false;


    QStatusBar* statusBarr = new QStatusBar();
    statusBarr->setStyleSheet("background-color: #2d2d44; color: #aaaaaa;");
    setStatusBar(statusBarr);

    /////////////////////////////////////////////////////
    /// \brief setWindowTitle
    /// Main styling
    setWindowTitle("Hexa5Camera");
    // if you have an application icon
    setWindowIcon(QIcon(":/hexa5.png"));


    QFont f = font();
    f.setPointSize(10);
    setFont(f);

    // Add some internal margins around your main layout
    // ui->centralwidget->layout()->setContentsMargins(8,8,8,8);
    // ui->centralwidget->layout()->setSpacing(6);

    ui->VideoRecorderSection->setFlat(true);


    /////////////////////////////////////////////////////

    /////////////////////////////////////////////////////
    /// controlsContainer Creation

    // 1) Create a horizontal layout to replace the absolute geometry
    auto *hl = new QHBoxLayout(ui->centralwidget);
    hl->setContentsMargins(0,0,0,0);
    hl->setSpacing(0);
    hl->addWidget(ui->VideoRecorderSection, 1);
    hl->addWidget(ui->toggleButton,         0);
    hl->addWidget(ui->controlsContainer,    0);

    // 2) completely kill any padding inside controlsContainer itself
    if (auto *inner = qobject_cast<QBoxLayout*>(ui->controlsContainer->layout())) {
        inner->setContentsMargins(0,0,0,0);
        inner->setSpacing(0);
    }

    // 3) make the container truly Fixed‐width so 0px is honored
    ui->controlsContainer->setSizePolicy(
        QSizePolicy::Fixed,
        ui->controlsContainer->sizePolicy().verticalPolicy()
        );
    ui->controlsContainer->setMinimumWidth(0);
    ui->controlsContainer->setMaximumWidth(0);
    ui->controlsContainer->hide();  // start fully hidden

    // 4) measure its “full” width
    int fullW = ui->controlsContainer->sizeHint().width();
    if (fullW < 10) fullW = 320; // fallback

    // 5) build a single animation on its maximumWidth
    m_panelAnimation = new QPropertyAnimation(ui->controlsContainer, "maximumWidth", this);
    m_panelAnimation->setDuration(250);
    m_panelAnimation->setStartValue(0);
    m_panelAnimation->setEndValue(fullW);

    // 6) wire up hover expansion instead of toggle button
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(300); // 300ms delay before expanding
    
    // Install event filter on the main window to detect mouse near right edge
    this->installEventFilter(this);
    this->setMouseTracking(true);
    
    connect(ui->toggleButton, &QToolButton::clicked, this, [this]() {
        // Fallback button click - toggle panel manually
        if (m_panelExpanded) {
            m_panelAnimation->setDirection(QPropertyAnimation::Backward);
            m_panelAnimation->start();
        } else {
            ui->controlsContainer->show();
            m_panelAnimation->setDirection(QPropertyAnimation::Forward);
            m_panelAnimation->start();
            m_panelExpanded = true;
        }
    });
    connect(m_panelAnimation, &QPropertyAnimation::finished, this, [this]() {
        auto *ctr  = ui->controlsContainer;
        auto *lay   = ui->centralwidget->layout();

        if (m_panelAnimation->direction() == QAbstractAnimation::Backward) {
            // fully collapse
            ctr->hide();
            m_panelExpanded = false;
        }

        // Force the parent layout to re‐do its math
        if (lay) {
            lay->invalidate();
            lay->activate();
        }
        ui->centralwidget->updateGeometry();
    });




    ////////////////////////////////////////////////////

    ////////////////////////////////////////////////////
    /// Style
    ui->tabWidget->setStyleSheet(R"(
  /* overall pane */
  QTabWidget::pane {
    border: 1px solid #4A4A4A;
    background: #2B2B2B;
    top: -1px;              /* overlap tabs’ bottom border */
  }
  /* the tabs */
  QTabBar::tab {
    background: #3C3F41;
    color: #A9B7C6;
    padding: 6px 12px;
    margin-right: 2px;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    min-width: 80px;
  }
  QTabBar::tab:selected {
    background: #4E5254;
    color: #FFF;
  }
  QTabBar::tab:hover {
    background: #505354;
  }
  /* remove focus outline */
  QTabBar::tab:focus { outline: none; }
)");


    qApp->setStyleSheet(R"(
  QPushButton {
    background: #3C3F41;
    border: 1px solid #5A5A5A;
    padding: 6px 12px;
    color: #DDD;
    border-radius: 3px;
  }
  QPushButton:hover {
    background: #505354;
  }
  QPushButton:pressed {
    background: #2A2D2F;
  }
)");

    qApp->setStyleSheet(R"(
QDockWidget {
  background: #2b2b2b;
  titlebar-close-icon: none;  /* just in case */
}

/* style its title bar */
QDockWidget::title {
  text-align: left;
  padding: 4px 8px;
  background: qlineargradient(
      x1:0, y1:0, x2:0, y2:1,
      stop:0 #393939, stop:1 #2b2b2b
  );
  color: #ffffff;
  font-weight: bold;
  border-bottom: 1px solid #444444;
}

/* when floating, give it a thin border */
QDockWidget[floating="true"] {
  border: 1px solid #555555;
}

)");



    // Joystick button: switch mode *and* select tab 0
    connect(ui->switchtojoystick, &QPushButton::clicked, this, [this]() {
        onSwitchToJoystick();                // existing logic to flip into JOYSTICK mode
        ui->tabWidget->setCurrentIndex(0);   // show the “Joystick” tab
    });

    // Keyboard button: switch mode *and* select tab 1
    connect(ui->switchtokeyboard, &QPushButton::clicked, this, [this]() {
        onSwitchToKeyboard();                // existing logic to flip into KEYBOARD mode
        ui->tabWidget->setCurrentIndex(1);   // show the “Keyboard” tab
    });

    // Camera-Config button: flip into CONFIG mode *and* select tab 2
    connect(ui->pushButtonConfiguration, &QPushButton::clicked, this, [this]() {
        onSwitchToConfiguration();
        ui->tabWidget->setCurrentIndex(2);   // show the “Camera Configuration” tab
    });



    //QApplication::instance()->installEventFilter(this);
    connect(QJoysticks::getInstance(),
            &QJoysticks::axisChanged,
            this,
            &MainWindow::onJoystickAxisChanged);
    videoWidget = new VideoRecorderWidget(this);
    //videoWidget->installEventFilter(this);
    videoWidget->setFocusPolicy(Qt::NoFocus);
    videoWidget->getReceiver()->setWindowId(videoWidget->winId());
    QVBoxLayout *videoLayout = new QVBoxLayout();
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->addWidget(videoWidget);
    if (ui->VideoRecorderSection) {
        ui->VideoRecorderSection->setLayout(videoLayout);
    } else {
        #ifdef _DEBUG
        qDebug() << "VideoRecorderWidget not found in the Video Recorder section!";
        #endif
    }

    rtspUri = videoWidget->getReceiver()->getRtspUriFromConfig();
    qDebug() << "[VideoReceiver] opening RTSP URI:" << rtspUri;


    // 1) give an initial “checking” state
    ui->lineEditCameraStatus->setText("Checking…");
    ui->lineEditCameraStatus->setStyleSheet(
        "background-color: lightgray; color: black;");

    // 2) get the receiver and connect
    auto *vr = videoWidget->getReceiver();
    vr->setWindowId(videoWidget->winId());
    connect(vr, &VideoReceiver::cameraStarted,
            this, &MainWindow::onCameraStarted);
    connect(vr, &VideoReceiver::cameraError,
            this, &MainWindow::onCameraError);

    QTimer *cameraPoll = new QTimer(this);
    connect(cameraPoll, &QTimer::timeout, this, &MainWindow::refreshAllCameraStatus);
    cameraPoll->start(5000);

    refreshAllCameraStatus(); // Monitor all cameras instead of just one

    // Connect the Rescan button and joystick signals.
    connect(ui->Rescan, &QPushButton::clicked, this, &MainWindow::updateDeviceList);
    connect(QJoysticks::getInstance(), &QJoysticks::countChanged,
            this, &MainWindow::updateDeviceList);
    connect(QJoysticks::getInstance(), &QJoysticks::buttonChanged,
            this, &MainWindow::updateButtonState);
    connect(ui->listWidget, &QListWidget::itemClicked,
            this, &MainWindow::onJoystickItemClicked);

    // Mode switch buttons.
    connect(ui->switchtokeyboard, &QPushButton::clicked, this, &MainWindow::onSwitchToKeyboard);
    connect(ui->switchtojoystick, &QPushButton::clicked, this, &MainWindow::onSwitchToJoystick);
    connect(ui->pushButtonConfiguration, &QPushButton::clicked, this, &MainWindow::onSwitchToConfiguration);

    // in MainWindow::MainWindow(...)
    connect(ui->toolButtonUp,    &QToolButton::clicked, this, &MainWindow::onFullUp);
    connect(ui->toolButtonDown,  &QToolButton::clicked, this, &MainWindow::onFullDown);
    connect(ui->toolButtonLeft,  &QToolButton::clicked, this, &MainWindow::onFullLeft);
    connect(ui->toolButtonRight, &QToolButton::clicked, this, &MainWindow::onFullRight);
    connect(ui->toolButtonStop, &QToolButton::clicked, this, &MainWindow::onStop);

    // and your zoom buttons:
    connect(ui->toolButtonZoomPlus,  &QToolButton::clicked, this, &MainWindow::onZoomMaxIn);
    connect(ui->toolButtonZoomMinus, &QToolButton::clicked, this, &MainWindow::onZoomMaxOut);


    // connect(ui->pushButtonSaveConfig, &QPushButton::clicked, this, &MainWindow::saveConfig);
    // connect(ui->DefaultConfig, &QPushButton::clicked, this, &MainWindow::saveDefaultConfig);

    // Set focus policy so that key events arrive at the main window.
    setFocusPolicy(Qt::StrongFocus);
    setFocus();

    qApp->installEventFilter(this);


    updateDeviceList();
    statusBar()->showMessage("Ready");
    #ifdef _DEBUG
    qDebug() << "Detected Joysticks:" << QJoysticks::getInstance()->deviceNames();
    #endif
    for (int i = 0; i < QJoysticks::getInstance()->count(); i++) {
        #ifdef _DEBUG
        qDebug() << "Joystick" << i << "axis count:"
                 << QJoysticks::getInstance()->getNumAxes(i);
        #endif
    }

    QTimer *pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollAxisValues);
    pollTimer->start(50);

    // Set up a timer to send gimbal commands every 100ms.
    commandTimer = new QTimer(this);
    connect(commandTimer, &QTimer::timeout, this, &MainWindow::sendGimbalCommands);
    //commandTimer->setInterval(50);
    commandTimer->start(50);
    qDebug() << "[MainWindow] gimbalTimer started (50ms)";


    // // Create the SIYI SDK instance.

    // std::string ip = "10.14.11.3";
    // int port = 37260;
    // sdk = new SIYI_SDK(ip.c_str(), 37260);
    // if (sdk->request_firmware_version()) {
    //     qDebug() << "Requested firmware version. Waiting for response...";
    //     std::this_thread::sleep_for(std::chrono::seconds(2));
    //     auto [code_version, gimbal_version, zoom_version] = sdk->get_firmware_version();
    //     qDebug() << "Code Board: " << code_version.c_str()
    //              << "  Gimbal: " << gimbal_version.c_str()
    //              << "  Zoom: " << zoom_version.c_str();
    // } else {
    //     qDebug() << "Failed to request firmware version.";
    // }
    // if (sdk->request_gimbal_center()){
    //     qDebug() << "Requested gimbal center . Waiting for response...";
    // }
    // if(sdk->request_autofocus()){
    //     qDebug() << "Requested autofocus. Waiting for response...";
    // }

    // receiveThread = std::thread([this]() {
    //     bool keepRunningLocal = keepRunning.load();
    //     sdk->receive_message_loop(keepRunningLocal);
    // });
    // #ifdef _DEBUG
    // qDebug() << "Camera control initialized";
    // #endif

    QTimer::singleShot(100, this, &MainWindow::initializeCameraController);
    createCameraControllerFromConfig();


    //Recording Video Section
    useLocalCamera = true;
    // Recording overlay + timer
    recordOverlay = new QLabel(videoWidget);
    recordOverlay->setStyleSheet(R"(
  background-color: rgba(0,0,0,128);
  color: red;
  font: bold 16px;
)");
    recordOverlay->setAlignment(Qt::AlignCenter);
    recordOverlay->setFixedHeight(30);
    recordOverlay->setFixedWidth(videoWidget->width());
    recordOverlay->move(0,0);
    recordOverlay->hide();
    recordOverlay->raise();

    // Create the timer for updating the overlay clock
    recordUiTimer = new QTimer(this);
    recordUiTimer->setInterval(500);
    connect(recordUiTimer, &QTimer::timeout,
            this,         &MainWindow::updateRecordTime);

    // Button hookup
    // connect(ui->RecordButton, &QPushButton::clicked,
    //         this,            &MainWindow::on_RecordButton_clicked);
    recordState = RecordState::Idle;
    ui->RecordButton->setText("Start Recording");

    //Screenshot
    connect(ui->ScreenshotButton, &QPushButton::clicked,
            this,               &MainWindow::on_ScreenshotButton_clicked);


    // e.g. read it from your camera‐config QLineEdits, or just hard‑code
    QString servoIp   = loadServoIp();
    int     servoPort = loadServoPort();

    // // 1) instantiate
    // _servo = std::make_unique<ServoControl::ServoClient>(
    //     servoIp.toStdString(),
    //     servoPort,
    //     /*timeout_ms=*/ 2000
    //     );

    // // 2) try to connect
    // if (!_servo->connect()) {
    //     statusBar()->showMessage(
    //         QString("Servo connect failed: %1")
    //             .arg(QString::fromStdString(_servo->getLastError())),
    //         5000
    //         );
    // } else {
    //     statusBar()->showMessage("Servo connected", 2000);
    // }

    // // 1) take ownership of your existing client and make a worker
    // auto client = std::move(_servo);
    // auto* thread = new QThread(this);
    // auto* worker = new ServoWorker(std::move(client));
    // worker->moveToThread(thread);
    // connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    // thread->start();

    // // 2) expose a signal so we can tell the worker "new position!"
    // connect(this, &MainWindow::servoPositionChanged,
    //         worker, &ServoWorker::setPosition,
    //         Qt::QueuedConnection);

    // // 3) initialize value (if you like)
    // emit servoPositionChanged(_servoPosition);
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
            QListWidgetItem *item = new QListWidgetItem(names[i]);
            item->setData(Qt::UserRole, i);
            if (axisCount == 3) {
                item->setBackground(Qt::green);
                item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            } else {
                item->setBackground(Qt::red);
                item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
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
        int steps = static_cast<int>((currentZoom - MIN_ZOOM)/ZOOM_STEP_CONSTANT + 0.5f);
        int maxSteps = static_cast<int>((MAX_ZOOM - MIN_ZOOM)/ZOOM_STEP_CONSTANT + 0.5f);
        ui->progressBar_3->setRange(0, maxSteps);
        ui->progressBar_3->setTextVisible(true);
        ui->progressBar_3->setFormat("%v");
        ui->progressBar_3->setValue(steps);
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
    // else if (axis == 2) {
    //     // Third axis → zoom
    //     float target = currentZoom + curved * ZOOM_SPEED;
    //     // Clamp to your min/max
    //     float clamped = qBound(MIN_ZOOM, target, MAX_ZOOM);
    //     if (!qFuzzyCompare(clamped, currentZoom)) {
    //         currentZoom = clamped;
    //         sdk->set_absolute_zoom(currentZoom, 1);
    //         sdk->request_autofocus();
    //     }
    //     return;   // don’t also send a gimbal‐move
    // }
    else if (axis == 2) {
        // 1) ignore all “pull back” (negative) values so center=0
        qreal v = qMax<qreal>(value, 0.0);

        // 2) choose how many steps you want: e.g. 7 steps → levels 0..7
        constexpr int ZOOM_STEPS = 5;
        //int ZOOM_STEPS = int((MAX_ZOOM - MIN_ZOOM) / ZOOM_STEP_CONSTANT + 0.5f);
        // 3) map [0..1] → [0..ZOOM_STEPS], rounding to nearest integer
        int level = int(v * ZOOM_STEPS + 0.5);
        level = qBound(0, level, ZOOM_STEPS);

        // 4) only change zoom when we actually cross into a new step
        if (level != lastZoomLevel) {
            // compute the actual zoom value for this step
            float newZoom = MIN_ZOOM + level * ZOOM_STEP_CONSTANT;
            newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);

            currentZoom = newZoom;
            //sdk->set_absolute_zoom(currentZoom, 1);
            if (cameraController) cameraController->setAbsoluteZoom(currentZoom, 1);
            //sdk->request_autofocus();

            lastZoomLevel = level;
        }

        // update your little UI slider (if you like):
        ui->progressBar_3->setRange(0, ZOOM_STEPS);
        ui->progressBar_3->setFormat("%v");
        ui->progressBar_3->setValue(level);

        return;   // don't send any yaw/pitch
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
            cameraController->setAbsoluteZoom(currentZoom, 1);
        }
            break;
        case Qt::Key_Minus:
        case Qt::Key_Underscore:
        {
            QMutexLocker locker(&commandMutex);
            currentZoom = std::max(MIN_ZOOM, currentZoom - ZOOM_SPEED);
            if(ui->toolButtonZoomMinus) ui->toolButtonZoomMinus->setStyleSheet("background-color: green;");
            cameraController->setAbsoluteZoom(currentZoom, 1);
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
            currentYawSpeed = 0;
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
        if (ui->siyi_lineEditIP) ui->siyi_lineEditIP->setText(siyiConfig.value("ip").toString(ipDefault));
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
    bool siyiValid = !siyiIP.isEmpty() && siyiPort > 0 && !siyiPath.isEmpty();
    bool aiValid = !aiCameraIP.isEmpty() && aiControlPort > 0 && !aiPath.isEmpty();
    bool servoValid = !servoIP.isEmpty() && servoPort > 0;
    
    qDebug() << "[VIDEO_SOURCE] Config validation - SIYI:" << siyiValid << "AI:" << aiValid << "Servo:" << servoValid;
    
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
        siyiConfig["ip"] = siyiIP;
        siyiConfig["port"] = siyiPort;
        siyiConfig["path"] = siyiPath;
        obj["siyiConfig"] = siyiConfig;
        qDebug() << "[VIDEO_SOURCE] SIYI config added to JSON";
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
    bool siyiValid = !siyiIP.isEmpty() && siyiPort > 0 && !siyiPath.isEmpty();
    bool aiValid = !aiCameraIP.isEmpty() && aiControlPort > 0 && !aiPath.isEmpty();
    bool servoValid = !servoIP.isEmpty() && servoPort > 0;
    
    qDebug() << "[VIDEO_SOURCE] Config validation - SIYI:" << siyiValid << "AI:" << aiValid << "Servo:" << servoValid;
    
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
        siyiConfig["ip"] = siyiIP;
        siyiConfig["port"] = siyiPort;
        siyiConfig["path"] = siyiPath;
        obj["siyiConfig"] = siyiConfig;
        qDebug() << "[VIDEO_SOURCE] SIYI config added to JSON";
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


void MainWindow::updateConfigDisplay()
{
    if (!configDisplayLabel) {
        configDisplayLabel = new QLabel(ui->VideoRecorderSection);
        configDisplayLabel->setGeometry(10, 30, 700, 300);
        configDisplayLabel->setStyleSheet("background-color: rgba(40, 40, 40, 200); color: white; padding: 10px; border-radius: 6px; font-family: 'Courier New', monospace; font-size: 11px;");
        configDisplayLabel->setWordWrap(true);
        configDisplayLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        configDisplayLabel->setVisible(false);
    }
    
    if (!showConfigOverlay) {
        configDisplayLabel->setVisible(false);
        return;
    }
    
    // Read from JSON config file instead of UI fields
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    QString cfgFile = dir.filePath("Haxa5Camera/Hexa5CameraConfig.json");
    
    QString displayText;
    
    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly)) {
        displayText = "No configuration saved yet";
        configDisplayLabel->setText(displayText);
        configDisplayLabel->setVisible(true);
        return;
    }
    
    QByteArray data = f.readAll();
    f.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        displayText = "Invalid configuration file";
        configDisplayLabel->setText(displayText);
        configDisplayLabel->setVisible(true);
        return;
    }
    
    QJsonObject obj = doc.object();
    
    // Check if this is old format for backward compatibility
    if (obj.contains("cameraType")) {
        QString cameraType = obj.value("cameraType").toString("siyi").toUpper();
        displayText = QString("LEGACY FORMAT\nCamera Type: %1\n").arg(cameraType);
        
        if (cameraType == "SIYI") {
            QString ip = obj.value("ip").toString("N/A");
            int port = obj.value("port").toInt(0);
            QString path = obj.value("path").toString("N/A");
            
            displayText += QString("IP: %1\nPort: %2\nPath: %3")
                          .arg(ip)
                          .arg(port)
                          .arg(path);
            
            // Add RTSP URL for legacy SIYI format
            if (ip != "N/A" && port > 0 && path != "N/A") {
                QString rtspUrl = QString("rtsp://%1:%2%3").arg(ip).arg(port).arg(path);
                displayText += QString("\nRTSP URL: %1").arg(rtspUrl);
            }
        } else if (cameraType == "AI") {
            QString aiCameraIP = obj.value("aiCameraIP").toString("N/A");
            int aiControlPort = obj.value("aiControlPort").toInt(0);
            QString aiPath = obj.value("path").toString("/ai/stream"); // Default path for legacy AI
            
            displayText += QString("Camera IP: %1\nControl Port: %2\nPath: %3")
                          .arg(aiCameraIP)
                          .arg(aiControlPort)
                          .arg(aiPath);
            
            // Add RTSP URL for legacy AI format if path available
            if (aiCameraIP != "N/A" && aiControlPort > 0) {
                QString rtspUrl = QString("rtsp://%1:%2%3").arg(aiCameraIP).arg(aiControlPort).arg(aiPath);
                displayText += QString("\nRTSP URL: %1").arg(rtspUrl);
            }
        }
        
        configDisplayLabel->setText(displayText);
        configDisplayLabel->setVisible(true);
        return;
    }
    
    // Display new parallel format
    QString videoSource = obj.value("videoSource").toString("siyi").toUpper();
    displayText = QString("Video Source: %1\n").arg(videoSource);
    
    // Show SIYI config if present
    if (obj.contains("siyiConfig")) {
        QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
        QString siyiIP = siyiConfig.value("ip").toString("N/A");
        int siyiPort = siyiConfig.value("port").toInt(0);
        QString siyiPath = siyiConfig.value("path").toString("N/A");
        
        displayText += QString("\n[SIYI Camera]\nIP: %1\nPort: %2\nPath: %3")
                      .arg(siyiIP)
                      .arg(siyiPort)
                      .arg(siyiPath);
        
        // Add RTSP URL for SIYI if it's the active source
        if (videoSource == "SIYI" && siyiIP != "N/A" && siyiPort > 0 && siyiPath != "N/A") {
            QString rtspUrl = QString("rtsp://%1:%2%3").arg(siyiIP).arg(siyiPort).arg(siyiPath);
            displayText += QString("\nRTSP URL: %1").arg(rtspUrl);
        }
    }
    
    // Show AI config if present
    if (obj.contains("aiConfig")) {
        QJsonObject aiConfig = obj.value("aiConfig").toObject();
        QString aiCameraIP = aiConfig.value("cameraIP").toString("N/A");
        int aiControlPort = aiConfig.value("controlPort").toInt(0);
        QString aiPath = aiConfig.value("path").toString("N/A");
        
        displayText += QString("\n[AI Camera]\nCamera IP: %1\nControl Port: %2\nPath: %3")
                      .arg(aiCameraIP)
                      .arg(aiControlPort)
                      .arg(aiPath);
        
        // Add RTSP URL for AI if it's the active source
        if (videoSource == "AI" && aiCameraIP != "N/A" && aiControlPort > 0 && aiPath != "N/A") {
            QString rtspUrl = QString("rtsp://%1:%2%3").arg(aiCameraIP).arg(aiControlPort).arg(aiPath);
            displayText += QString("\nRTSP URL: %1").arg(rtspUrl);
        }
    }
    
    // Add RTSP URL for the active video source summary
    displayText += QString("\n---\nActive RTSP: ");
    
    if (videoSource == "SIYI" && obj.contains("siyiConfig")) {
        QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
        QString siyiIP = siyiConfig.value("ip").toString("N/A");
        int siyiPort = siyiConfig.value("port").toInt(0);
        QString siyiPath = siyiConfig.value("path").toString("N/A");
        
        if (siyiIP != "N/A" && siyiPort > 0 && siyiPath != "N/A") {
            QString rtspUrl = QString("rtsp://%1:%2%3").arg(siyiIP).arg(siyiPort).arg(siyiPath);
            displayText += rtspUrl;
        } else {
            displayText += "Invalid SIYI config";
        }
    } else if (videoSource == "AI" && obj.contains("aiConfig")) {
        QJsonObject aiConfig = obj.value("aiConfig").toObject();
        QString aiCameraIP = aiConfig.value("cameraIP").toString("N/A");
        int aiControlPort = aiConfig.value("controlPort").toInt(0);
        QString aiPath = aiConfig.value("path").toString("N/A");
        
        if (aiCameraIP != "N/A" && aiControlPort > 0 && aiPath != "N/A") {
            QString rtspUrl = QString("rtsp://%1:%2%3").arg(aiCameraIP).arg(aiControlPort).arg(aiPath);
            displayText += rtspUrl;
        } else {
            displayText += "Invalid AI config";
        }
    } else {
        displayText += "No valid config";
    }
    
    // Show Servo config if present (legacy)
    if (obj.contains("servoConfig")) {
        QJsonObject servoConfig = obj.value("servoConfig").toObject();
        displayText += QString("\n[Servo]\nIP: %1\nPort: %2")
                      .arg(servoConfig.value("servoIP").toString("N/A"))
                      .arg(servoConfig.value("servoPort").toInt(0));
    }
    
    // Add connectivity information section
    displayText += QString("\n---\n[Connectivity Status]");
    
    if (!pingWatcher) {
        displayText += "\nStatus: Initializing...";
    } else {
        // Get all configured camera IPs
        QMap<QString, QString> cameraIps = loadAllCameraIps();
        
        if (cameraIps.isEmpty()) {
            displayText += "\nNo cameras configured";
        } else {
            int reachableCount = 0;
            int totalCount = cameraIps.size();
            
            for (auto it = cameraIps.begin(); it != cameraIps.end(); ++it) {
                QString cameraType = it.key();
                QString cameraIp = it.value();
                
                HostConnectivityScore score = pingWatcher->getConnectivityScore(cameraType);
                
                QString statusIcon = score.isReachable ? "🟢" : "🔴";
                QString rttInfo = score.isReachable ? QString("%1ms").arg(score.currentRtt) : QString("%1 failures").arg(score.consecutiveFailures);
                QString scoreInfo = QString("(%1%)").arg(score.overallScore);
                
                displayText += QString("\n%1 %2: %3 %4")
                              .arg(statusIcon)
                              .arg(cameraType)
                              .arg(rttInfo)
                              .arg(scoreInfo);
                
                if (score.isReachable) {
                    reachableCount++;
                }
            }
            
            // Add summary
            displayText += QString("\nSummary: %1/%2 reachable")
                          .arg(reachableCount)
                          .arg(totalCount);
            
            // Add last update time
            displayText += QString("\nLast update: %1")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"));
        }
    }
    
    configDisplayLabel->setText(displayText);
    configDisplayLabel->setVisible(true);
    configDisplayLabel->raise(); // Ensure overlay stays on top
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


void MainWindow::onShowConfigToggled(bool enabled)
{
    showConfigOverlay = enabled;
    updateConfigDisplay();
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
    FILE* pipe = popen("ps -aux | grep JoystickIdentifier | grep -v grep", "r");
    if (!pipe) {
        perror("popen failed");
        return;
    }

    std::vector<pid_t> pids;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string token;
        
        while (iss >> token) {
            tokens.push_back(token);
        }
        
        if (tokens.size() > 1) {
            try {
                pid_t pid = static_cast<pid_t>(std::stoi(tokens[1]));
                pids.push_back(pid);
            } catch (const std::exception& e) {
                // Invalid PID format, skip
            }
        }
    }
    pclose(pipe);

    for (pid_t pid : pids) {
        if (kill(pid, SIGKILL) == 0) {
            printf("Killed process %d\n", pid);
        } else {
            perror(("Failed to kill process " + std::to_string(pid)).c_str());
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
    // 1) gracefully shut down the stream
    if (videoWidget) {
        auto *rcv = videoWidget->getReceiver();
        if (rcv) {
            // asynchronously stop the pipeline
            auto future = QtConcurrent::run([rcv]() {
                rcv->stop();     // this will block—but not on the GUI thread
            });
            (void)future; // Suppress unused variable warning
        }
    }

    // 2) (optional) kill any *other* instances—but do NOT SIGKILL your own PID
    killExistingInstances_(); // ← drop this

    // 3) Finish closing
    QMainWindow::closeEvent(ev);
    QCoreApplication::quit();

    // if (_servo && _servo->isConnected()) {
    //     _servo->disconnect();
    // }
}


void MainWindow::onCameraStarted() {
    ui->lineEditCameraStatus->setText("Camera Working");
    ui->lineEditCameraStatus->setStyleSheet(
        "background-color: #ccffcc; color: darkgreen;");
    auto *vr = videoWidget->getReceiver();
    vr->setWindowId(videoWidget->winId());
}

void MainWindow::onCameraError(const QString &msg) {
    ui->lineEditCameraStatus->setText("Camera Not Working");
    ui->lineEditCameraStatus->setStyleSheet(
        "background-color: #ffcccc; color: darkred;");
    qDebug() << "GStreamer error:" << msg;
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
    
    // Extract SIYI camera IP
    if (obj.contains("siyiConfig")) {
        QJsonObject siyiConfig = obj.value("siyiConfig").toObject();
        QString siyiIp = siyiConfig.value("ip").toString();
        if (!siyiIp.isEmpty()) {
            cameraIps["SIYI"] = siyiIp;
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
    // Use the continuous ping watcher instead of one-time ping
    if (pingWatcher && pingWatcher->isWatching()) {
        qDebug() << "[PING_WATCHER] Continuous monitoring already active";
        return;
    }
    
    // Initialize ping watcher for single status check
    initializePingWatcher();
}

void MainWindow::refreshAllCameraStatus() {
    LOG_IP_WATCHDOG() << "Starting comprehensive IP monitoring for all cameras";
    
    // Use the continuous ping watcher instead of one-time pings
    initializePingWatcher();
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

    // 1) Trim any stray newline
    QString uri = rtspUri.trimmed();

    if (recordState == RecordState::Idle) {
        // ── START RECORDING ──

        // 2) prepare path
        QString dir = QDir::homePath() + "/Hexa5CameraRecordedVideos";
        QDir().mkpath(dir);
        QString fn = QDateTime::currentDateTime()
                         .toString("yyyyMMdd_hhmmss") + ".mp4";
        lastRecordPath = dir + "/" + fn;

        // 3) launch ffmpeg
        QStringList args = {
            "-rtsp_transport", "tcp",
            "-i",              uri,
            "-c",              "copy",
            "-y",
            lastRecordPath
        };
        qDebug() << "[Record] will run: ffmpeg" << args;

        delete recordProcess;
        recordProcess = new QProcess(this);
        recordProcess->setProcessChannelMode(QProcess::MergedChannels);
        connect(recordProcess, &QProcess::readyReadStandardError, [this]() {
            auto err = recordProcess->readAllStandardError();
            qDebug() << "[ffmpeg]" << err.trimmed();
        });
        recordProcess->start("ffmpeg", args);

        // 4) wait up to 2 s for it to actually start
        if (!recordProcess->waitForStarted(2000) ||
            recordProcess->state() != QProcess::Running)
        {
            QMessageBox::warning(
                this,
                tr("Recording"),
                tr("Could not start ffmpeg — check your URI and network.")
                );
            delete recordProcess;
            recordProcess = nullptr;
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

    // 2) grab the X11 window that xvimagesink is drawing into
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        statusBar()->showMessage("📸 No screen available!", 3000);
        return;
    }

    // videoWidget is your VideoRecorderWidget* embedded in the UI
    WId videoXid = this->videoWidget->winId();
    QPixmap pix = screen->grabWindow(videoXid);

    // 3) save to disk
    if (!pix.save(fullPath, "PNG")) {
        statusBar()->showMessage("📸 Screenshot failed!", 3000);
        return;
    }

    // 4) feedback
    statusBar()->showMessage(
        QString("📸 Screenshot saved to:\n%1").arg(fullPath),
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
                    ip = siyiConfig.value("ip").toString(ip);
                    port = siyiConfig.value("port").toInt(port);
                    path = siyiConfig.value("path").toString(path);
                    
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

    if (!cameraController->start()) {
        qWarning() << "Failed to start cameraController";
        // keep it null to avoid using a half-started controller
        cameraController.reset();
        statusBar()->showMessage("Camera controller failed to start", 3000);
    }

    // wire callbacks into MainWindow slots
    cameraController->onStarted = [this]() {
        QMetaObject::invokeMethod(this, "onCameraStarted", Qt::QueuedConnection);
    };
    cameraController->onError = [this](const QString &msg) {
        QMetaObject::invokeMethod(this, [this, msg]() { onCameraError(msg); }, Qt::QueuedConnection);
    };

    // wire servoPositionChanged -> controller absolute position (if supported)
    connect(this, &MainWindow::servoPositionChanged, this, [this](int newPos) {
        if (cameraController && cameraController->supportsAbsolutePosition()) {
            cameraController->setGimbalPosition(0, newPos);
        }
    }, Qt::QueuedConnection);

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
    qDebug() << "[PING_WATCHER] Initializing continuous ping watcher";
    
    // Check if ping watcher is already properly initialized
    if (pingWatcher && pingWatcher->isWatching()) {
        qDebug() << "[PING_WATCHER] Already initialized and watching, skipping reinitialization";
        return;
    }
    
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
    qDebug() << "[PING_WATCHER]" << name << "camera status:" << (reachable ? "REACHABLE" : "UNREACHABLE") 
             << "RTT:" << roundTripTime << "ms";
    
    // Update the connectivity display when status changes
    updateConnectivityDisplay();
    
    // Also update config display if it's visible
    if (showConfigOverlay) {
        updateConfigDisplay();
    }
    
    if (!reachable) {
        QString errorMsg = QStringLiteral("%1 Camera unreachable (ping failed)").arg(name);
        onCameraError(errorMsg);
    } else {
        // Camera is reachable, you might want to update UI or clear previous error states
        qDebug() << "[PING_WATCHER]" << name << "camera is reachable";
    }
}

void MainWindow::onHostError(const QString& name, const QString& error) {
    qWarning() << "[PING_WATCHER]" << name << "ping error:" << error;
    QString errorMsg = QStringLiteral("%1 Camera ping error: %2").arg(name, error);
    onCameraError(errorMsg);
}

void MainWindow::onConnectivityScoreUpdated(const QString& name, const HostConnectivityScore& score) {
    // Update the camera status display with comprehensive information
    QString statusText;
    QString statusStyle;
    
    if (score.isReachable) {
        // Camera is reachable - show detailed status
        statusText = QString("%1: 🟢 UP | RTT: %2ms | Score: %3%")
                    .arg(name)
                    .arg(score.currentRtt)
                    .arg(score.overallScore);
        
        // Color based on score
        if (score.overallScore >= 80) {
            statusStyle = "color: #00ff00; font-weight: bold;"; // Excellent - Bright green
        } else if (score.overallScore >= 60) {
            statusStyle = "color: #88ff00; font-weight: bold;"; // Good - Light green
        } else if (score.overallScore >= 40) {
            statusStyle = "color: #ffaa00; font-weight: bold;"; // Fair - Orange
        } else {
            statusStyle = "color: #ff6600; font-weight: bold;"; // Poor - Dark orange
        }
    } else {
        // Camera is unreachable - still show status with score
        statusText = QString("%1: 🔴 DOWN | Score: %2% | Failures: %3")
                    .arg(name)
                    .arg(score.overallScore)
                    .arg(score.consecutiveFailures);
        
        statusStyle = "color: #ff3333; font-weight: bold;"; // Red
    }
    
    // Add additional details in tooltip
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
    }
    
    // Update status bar with summary
    QString currentVideoSource = getCurrentVideoSource();
    if ((name.toUpper() == "SIYI" && currentVideoSource == "siyi") ||
        (name.toUpper() == "AI" && currentVideoSource == "ai") ||
        (name.toUpper() == "SERVO" && currentVideoSource == "servo")) {
        
        QString statusBarMsg = QString("%1 Camera - %2 | Reliability: %3% | Performance: %4%")
                              .arg(name)
                              .arg(score.isReachable ? "Connected" : "Disconnected")
                              .arg(score.reliabilityScore)
                              .arg(score.performanceScore);
        
        statusBar()->showMessage(statusBarMsg, 5000);
    }
    
    LOG_UI_STATUS() << "Updated" << name << "connectivity display:"
             << "Score:" << score.overallScore 
             << "Reachable:" << score.isReachable
             << "RTT:" << score.currentRtt;
    
    // Check for low connectivity and handle video shutdown
    checkAndHandleLowConnectivity(name, score);
    
    // Also update config display if it's visible
    if (showConfigOverlay) {
        updateConfigDisplay();
    }
}

void MainWindow::checkAndHandleLowConnectivity(const QString& name, const HostConnectivityScore& score) {
    bool currentlyShutdown = videoShutdownStates.value(name, false);
    bool shouldShutdown = score.overallScore < LOW_CONNECTIVITY_THRESHOLD;
    
    if (shouldShutdown && !currentlyShutdown) {
        // Score dropped below threshold - shut down video
        LOG_VIDEO_SHUTDOWN() << "Camera" << name << "score" << score.overallScore 
                 << "<" << LOW_CONNECTIVITY_THRESHOLD << "% - shutting down video display";
        
        videoShutdownStates[name] = true;
        
        // Stop video display for this camera
        if (videoWidget) {
            VideoReceiver* receiver = videoWidget->getReceiver();
            if (receiver) {
                receiver->stop();
            }
            videoWidget->hide();
        }
        
        // Show user notification
        statusBar()->showMessage(QString("Camera %1 video disabled due to poor connectivity (Score: %2%%)")
                                .arg(name).arg(score.overallScore), 5000);
        
        // Update UI to show shutdown state
        updateConnectivityDisplay();
        
    } else if (!shouldShutdown && currentlyShutdown) {
        // Score recovered above threshold - restore video
        LOG_VIDEO_RESTORE() << "Camera" << name << "score" << score.overallScore 
                 << ">=" << LOW_CONNECTIVITY_THRESHOLD << "% - restoring video display";
        
        videoShutdownStates[name] = false;
        
        // Restart video display
        if (videoWidget && !rtspUri.isEmpty()) {
            VideoReceiver* receiver = videoWidget->getReceiver();
            if (receiver) {
                receiver->setRtspUri(rtspUri);
                receiver->start();
            }
            videoWidget->show();
        }
        
        // Show user notification
        statusBar()->showMessage(QString("Camera %1 video restored (Score: %2%%)")
                                .arg(name).arg(score.overallScore), 3000);
        
        // Update UI to show restored state
        updateConnectivityDisplay();
    }
}

void MainWindow::updateConnectivityDisplay() {
    LOG_UI_STATUS() << "Starting connectivity display update";
    
    if (!pingWatcher) {
        LOG_UI_STATUS() << "Ping watcher not initialized, showing default status";
        if (ui->lineEditCameraStatus) {
            ui->lineEditCameraStatus->setText("Initializing connectivity monitor...");
            ui->lineEditCameraStatus->setStyleSheet("color: #ffaa00; font-weight: bold;");
            ui->lineEditCameraStatus->setToolTip("Connectivity monitoring is starting up");
        }
        return;
    }
    
    // Get all configured camera IPs
    QMap<QString, QString> cameraIps = loadAllCameraIps();
    
    LOG_UI_STATUS() << "Found" << cameraIps.size() << "camera configurations";
    
    if (cameraIps.isEmpty()) {
        LOG_UI_STATUS() << "No cameras configured, showing empty status";
        if (ui->lineEditCameraStatus) {
            ui->lineEditCameraStatus->setText("No cameras configured");
            ui->lineEditCameraStatus->setStyleSheet("color: #ffaa00; font-weight: bold;");
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
        
        LOG_UI_STATUS() << "Checking status for" << cameraType << "at" << cameraIp;
        
        HostConnectivityScore score = pingWatcher->getConnectivityScore(cameraType);
        
        LOG_UI_STATUS() << cameraType << "score:" << score.overallScore 
                 << "reachable:" << score.isReachable 
                 << "totalPings:" << score.totalPings;
        
        if (score.isReachable) {
            reachableCount++;
            statusList << QString("%1:🟢%2ms").arg(cameraType.left(3)).arg(score.currentRtt);
        } else {
            statusList << QString("%1:🔴%2").arg(cameraType.left(3)).arg(score.consecutiveFailures);
        }
    }
    
    // Create summary display
    QString summaryText = QString("Cameras: %1/%2 UP | %3")
                         .arg(reachableCount)
                         .arg(totalCount)
                         .arg(statusList.join(" | "));
    
    QString summaryStyle;
    if (reachableCount == totalCount) {
        summaryStyle = "color: #00ff00; font-weight: bold;"; // All good
    } else if (reachableCount > 0) {
        summaryStyle = "color: #ffaa00; font-weight: bold;"; // Some down
    } else {
        summaryStyle = "color: #ff3333; font-weight: bold;"; // All down
    }
    
    // Create detailed tooltip
    QString tooltip = QString("Camera Status Summary\n"
                             "Total Cameras: %1\n"
                             "Reachable: %2\n"
                             "Unreachable: %3\n\n")
                    .arg(totalCount)
                    .arg(reachableCount)
                    .arg(totalCount - reachableCount);
    
    for (auto it = cameraIps.begin(); it != cameraIps.end(); ++it) {
        QString cameraType = it.key();
        QString cameraIp = it.value();
        HostConnectivityScore score = pingWatcher->getConnectivityScore(cameraType);
        
        QString statusDetail;
        if (score.totalPings == 0) {
            statusDetail = "🔄 Initializing...";
        } else if (score.isReachable) {
            statusDetail = QString("🟢 Reachable (RTT: %1ms)").arg(score.currentRtt);
        } else {
            statusDetail = QString("🔴 Unreachable (%1 failures)").arg(score.consecutiveFailures);
        }
        
        tooltip += QString("\n%1 (%2):\n"
                          "  Status: %3\n"
                          "  Score: %4%\n"
                          "  Success Rate: %5%\n"
                          "  Avg RTT: %6ms\n"
                          "  Total Pings: %7")
                   .arg(cameraType)
                   .arg(cameraIp)
                   .arg(statusDetail)
                   .arg(score.overallScore)
                   .arg(score.reliabilityScore)
                   .arg(score.averageRtt)
                   .arg(score.totalPings);
    }
    
    // Update the display
    if (ui->lineEditCameraStatus) {
        ui->lineEditCameraStatus->setText(summaryText);
        ui->lineEditCameraStatus->setStyleSheet(summaryStyle);
        ui->lineEditCameraStatus->setToolTip(tooltip);
        LOG_UI_STATUS() << "Display updated with text:" << summaryText;
    } else {
        LOG_UI_STATUS() << "ERROR: lineEditCameraStatus is null!";
    }
    
    LOG_UI_STATUS() << "Connectivity summary updated:"
             << "Reachable:" << reachableCount << "/" << totalCount
             << "Display:" << summaryText;
    
    // Also update config display if it's visible
    if (showConfigOverlay) {
        updateConfigDisplay();
    }
}
