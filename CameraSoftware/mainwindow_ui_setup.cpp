// mainwindow_ui_setup.cpp
// UI setup, styling, layout, and signal/slot wiring — separated from business logic.

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "VideoRecorderWidget.h"
#include "QJoysticks.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QIntValidator>
#include <QLineEdit>
#include <QLabel>
#include <QFile>
#include <QDockWidget>
#include <QGroupBox>
#include <QTabBar>
#include <QDebug>
#include <QStackedWidget>
#include <QListWidget>
#include <QPushButton>

// ---------------------------------------------------------------------------
// setupStyles — window title, icon, fonts, all stylesheets
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// setupStyles — window title, icon, fonts, all stylesheets
// ---------------------------------------------------------------------------
void MainWindow::setupStyles()
{
    setWindowTitle("Hexa5Camera - Premium Edition");
    setWindowIcon(QIcon(":/hexa5.png"));

    // Set a modern font if available, fallback to system default
    QFont f("Inter");
    if (f.exactMatch()) {
        f.setPointSize(10);
        setFont(f);
    } else {
        // Fallback
        QFont f2 = font();
        f2.setPointSize(10);
        setFont(f2);
    }

    // Configure Status Bar
    QStatusBar* bar = new QStatusBar();
    // Use stylesheet for styling instead of inline
    bar->setSizeGripEnabled(false);
    setStatusBar(bar);

    if (ui->VideoRecorderSection) {
        ui->VideoRecorderSection->setFlat(true);
    }

    // Configure Camera Status Display (Make it read-only and centered)
    if (ui->lineEditCameraStatus) {
        ui->lineEditCameraStatus->setReadOnly(true);
        ui->lineEditCameraStatus->setAlignment(Qt::AlignCenter);
        ui->lineEditCameraStatus->setFocusPolicy(Qt::NoFocus);
    }

    // Load external stylesheet
    QFile styleFile("styles/hexa5.css");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString css = styleFile.readAll();
        qApp->setStyleSheet(css);
        styleFile.close();
    } else {
        qWarning() << "Could not load styles/hexa5.css";
    }
}

// ---------------------------------------------------------------------------
// setupLayout — central layout, controls container, panel animation
// ---------------------------------------------------------------------------
void MainWindow::setupLayout()
{
    // ── 1. VideoRecorderSection — remove Designer min-size so it fills space ──
    ui->VideoRecorderSection->setMinimumSize(0, 0);
    ui->VideoRecorderSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // ── 2. Toggle button — narrow vertical strip ──
    ui->toggleButton->setFixedWidth(20);
    ui->toggleButton->setMinimumHeight(40);
    ui->toggleButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // ── 3. Central horizontal layout: video | toggle | controls ──
    auto *hl = new QHBoxLayout(ui->centralwidget);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    hl->addWidget(ui->VideoRecorderSection, 1);
    hl->addWidget(ui->toggleButton,         0);
    hl->addWidget(ui->controlsContainer,    0);

    // ── 4. Controls container — redesigned layout ──
    //    Designer's verticalLayout_9 put everything in one flat list.
    //    We reparent widgets into a structured layout with visual grouping.

    if (ui->controlsContainer->layout())
        delete ui->controlsContainer->layout();

    auto *mainVBox = new QVBoxLayout(ui->controlsContainer);
    mainVBox->setContentsMargins(6, 6, 6, 6);
    mainVBox->setSpacing(6);

    // ── Row 1: Record + Screenshot side by side ──
    {
        auto *recRow = new QHBoxLayout();
        recRow->setSpacing(4);
        ui->RecordButton->setParent(ui->controlsContainer);
        ui->ScreenshotButton->setParent(ui->controlsContainer);
        ui->RecordButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->ScreenshotButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->RecordButton->setFixedHeight(30);
        ui->ScreenshotButton->setFixedHeight(30);
        recRow->addWidget(ui->RecordButton);
        recRow->addWidget(ui->ScreenshotButton);
        mainVBox->addLayout(recRow, 0);
    }

    // ── Row 2: ROI Zoom toggle + Video source combo + config checkbox ──
    {
        auto *srcRow = new QHBoxLayout();
        srcRow->setSpacing(4);

        // ROI Zoom toggle button — checkable
        m_roiToggleBtn = new QPushButton("ROI Zoom", ui->controlsContainer);
        m_roiToggleBtn->setCheckable(true);
        m_roiToggleBtn->setChecked(false);
        m_roiToggleBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_roiToggleBtn->setFixedHeight(26);
        m_roiToggleBtn->setToolTip("Toggle ROI Zoom: draw a rectangle on the video to zoom into it");
        m_roiToggleBtn->setStyleSheet(
            "QPushButton { padding: 2px 8px; }"
            "QPushButton:checked { background-color: #2e7d32; color: white; }"
        );
        connect(m_roiToggleBtn, &QPushButton::toggled,
                this, &MainWindow::onRoiToggled);

        ui->videoSourceComboBox->setParent(ui->controlsContainer);
        ui->overlayModeComboBox->setParent(ui->controlsContainer);
        ui->videoSourceComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->videoSourceComboBox->setFixedHeight(26);
        ui->overlayModeComboBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->overlayModeComboBox->setFixedHeight(26);
        ui->overlayModeComboBox->setMinimumWidth(120);

        srcRow->addWidget(m_roiToggleBtn, 0);
        srcRow->addWidget(ui->videoSourceComboBox, 1);
        srcRow->addWidget(ui->overlayModeComboBox, 0);
        mainVBox->addLayout(srcRow, 0);

        // Hide the standalone "Video Source:" label — the combo is self-explanatory
        ui->videoSourceLabel->setVisible(false);
    }

    // ── Row 3: Tab widget — gets all remaining space ──
    {
        ui->tabWidget->setParent(ui->controlsContainer);
        ui->tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        ui->tabWidget->setMinimumHeight(200);

        // Make tab bar use equal-width tabs that fit the panel
        ui->tabWidget->tabBar()->setExpanding(true);
        ui->tabWidget->tabBar()->setDocumentMode(true);
        ui->tabWidget->tabBar()->setUsesScrollButtons(false);
        ui->tabWidget->tabBar()->setElideMode(Qt::ElideNone);

        // Start on Joystick tab (index 0)
        ui->tabWidget->setCurrentIndex(0);

        mainVBox->addWidget(ui->tabWidget, 1);
    }

    // ── Row 4: Mode switch buttons — compact horizontal bar ──
    {
        auto *modeRow = new QHBoxLayout();
        modeRow->setSpacing(4);
        ui->switchtojoystick->setParent(ui->controlsContainer);
        ui->switchtokeyboard->setParent(ui->controlsContainer);
        ui->pushButtonConfiguration->setParent(ui->controlsContainer);

        // Shorter labels
        ui->switchtojoystick->setText("Joystick");
        ui->switchtokeyboard->setText("Keyboard");
        ui->pushButtonConfiguration->setText("Config");

        for (auto *btn : {ui->switchtojoystick, ui->switchtokeyboard, ui->pushButtonConfiguration}) {
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            btn->setFixedHeight(28);
            modeRow->addWidget(btn);
        }
        mainVBox->addLayout(modeRow, 0);
    }

    // Camera Status — removed from panel, hide it
    if (ui->groupBox_5) {
        ui->groupBox_5->setVisible(false);
    }

    // Hide the now-empty Designer wrapper widget
    if (ui->verticalLayoutWidget_9)
        ui->verticalLayoutWidget_9->setVisible(false);

    // ── 5. Fix tab page internals — unlock absolute-positioned children ──
    //    Each tab page has group boxes with hardcoded geometry from Designer.
    //    We apply layouts so they resize with the panel width.

    // Tab: Joystick (tabWidgetPage1)
    if (ui->tabWidgetPage1 && !ui->tabWidgetPage1->layout()) {
        auto *joyTabLayout = new QVBoxLayout(ui->tabWidgetPage1);
        joyTabLayout->setContentsMargins(4, 4, 4, 4);
        joyTabLayout->setSpacing(4);

        // Axes group (groupBox_2) — compact, not stretching
        if (ui->groupBox_2) {
            ui->groupBox_2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            ui->groupBox_2->setMinimumSize(0, 0);
            ui->groupBox_2->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            joyTabLayout->addWidget(ui->groupBox_2, 0);

            // Give groupBox_2 an internal layout so verticalLayoutWidget_2 fills width
            if (!ui->groupBox_2->layout()) {
                auto *axesLayout = new QVBoxLayout(ui->groupBox_2);
                axesLayout->setContentsMargins(4, 22, 4, 4);
                if (ui->verticalLayoutWidget_2) {
                    ui->verticalLayoutWidget_2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                    ui->verticalLayoutWidget_2->setMinimumSize(0, 0);
                    ui->verticalLayoutWidget_2->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    axesLayout->addWidget(ui->verticalLayoutWidget_2, 0);
                }
            }
        }

        // Buttons group (groupBox_3)
        if (ui->groupBox_3) {
            ui->groupBox_3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            ui->groupBox_3->setMinimumSize(0, 60);
            ui->groupBox_3->setMaximumSize(QWIDGETSIZE_MAX, 80);
            joyTabLayout->addWidget(ui->groupBox_3, 0);

            // Give groupBox_3 an internal layout so gridLayoutWidget fills width
            if (!ui->groupBox_3->layout()) {
                auto *btnLayout = new QVBoxLayout(ui->groupBox_3);
                btnLayout->setContentsMargins(4, 22, 4, 4);
                if (ui->gridLayoutWidget) {
                    ui->gridLayoutWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                    ui->gridLayoutWidget->setMinimumSize(0, 0);
                    ui->gridLayoutWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    btnLayout->addWidget(ui->gridLayoutWidget, 0);
                }
            }

            // Make the tool buttons expand horizontally only
            if (ui->toolButton_2)
                ui->toolButton_2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            if (ui->toolButton)
                ui->toolButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }

        // Push everything to the top
        joyTabLayout->addStretch(1);
    }

    // Tab: Keyboard (tabWidgetPage2)
    if (ui->tabWidgetPage2 && !ui->tabWidgetPage2->layout()) {
        auto *kbTabLayout = new QVBoxLayout(ui->tabWidgetPage2);
        kbTabLayout->setContentsMargins(4, 4, 4, 4);

        if (ui->gimbalControlGroupBox) {
            ui->gimbalControlGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            ui->gimbalControlGroupBox->setMinimumSize(0, 0);
            ui->gimbalControlGroupBox->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            kbTabLayout->addWidget(ui->gimbalControlGroupBox, 0);

            // Give gimbalControlGroupBox an internal layout
            if (!ui->gimbalControlGroupBox->layout()) {
                auto *gimbalLayout = new QVBoxLayout(ui->gimbalControlGroupBox);
                gimbalLayout->setContentsMargins(4, 24, 4, 4);
                gimbalLayout->setSpacing(8);

                // Direction pad grid — fixed height, not expanding
                if (ui->gridLayoutWidget_2) {
                    ui->gridLayoutWidget_2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                    ui->gridLayoutWidget_2->setMinimumSize(0, 120);
                    ui->gridLayoutWidget_2->setMaximumSize(QWIDGETSIZE_MAX, 160);
                    gimbalLayout->addWidget(ui->gridLayoutWidget_2, 0);
                }

                // Zoom row
                if (ui->gridLayoutWidget_3) {
                    ui->gridLayoutWidget_3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                    ui->gridLayoutWidget_3->setMinimumSize(0, 36);
                    ui->gridLayoutWidget_3->setMaximumSize(QWIDGETSIZE_MAX, 50);
                    gimbalLayout->addWidget(ui->gridLayoutWidget_3, 0);
                }
            }

            // Direction buttons: expand horizontally, fixed height
            for (auto *btn : {ui->toolButtonUp, ui->toolButtonDown,
                              ui->toolButtonLeft, ui->toolButtonRight,
                              ui->toolButtonStop}) {
                if (btn) {
                    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                    btn->setFixedHeight(40);
                }
            }
            // Zoom buttons: expand horizontally, fixed height
            for (auto *btn : {ui->toolButtonZoomPlus, ui->toolButtonZoomMinus}) {
                if (btn) {
                    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                    btn->setFixedHeight(30);
                }
            }
        }

        // Push everything to the top
        kbTabLayout->addStretch(1);
    }

    // Shorten tab titles so all 3 fit in the 340px panel
    ui->tabWidget->setTabText(0, "Joystick");
    ui->tabWidget->setTabText(1, "Keyboard");
    ui->tabWidget->setTabText(2, "Config");

    // Tab: Camera Configuration (tabWidgetPage3)
    if (ui->tabWidgetPage3 && !ui->tabWidgetPage3->layout()) {
        auto *cfgTabLayout = new QVBoxLayout(ui->tabWidgetPage3);
        cfgTabLayout->setContentsMargins(4, 4, 4, 4);

        if (ui->groupBox_4) {
            ui->groupBox_4->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            ui->groupBox_4->setMinimumSize(0, 0);
            ui->groupBox_4->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            cfgTabLayout->addWidget(ui->groupBox_4, 1);

            // Give groupBox_4 a proper internal layout for cameraTypeStack
            if (!ui->groupBox_4->layout()) {
                auto *gb4Layout = new QVBoxLayout(ui->groupBox_4);
                gb4Layout->setContentsMargins(4, 24, 4, 4); // 24 top for group title

                if (ui->cameraTypeStack) {
                    ui->cameraTypeStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                    ui->cameraTypeStack->setMinimumSize(0, 0);
                    ui->cameraTypeStack->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    gb4Layout->addWidget(ui->cameraTypeStack, 1);
                }
            }

            // Fix page_choose_type — unlock verticalLayoutWidget_3 and add top-alignment
            if (ui->page_choose_type && !ui->page_choose_type->layout()) {
                auto *chooserLayout = new QVBoxLayout(ui->page_choose_type);
                chooserLayout->setContentsMargins(4, 4, 4, 4);
                chooserLayout->setSpacing(0);

                if (ui->verticalLayoutWidget_3) {
                    ui->verticalLayoutWidget_3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                    ui->verticalLayoutWidget_3->setMinimumSize(0, 0);
                    ui->verticalLayoutWidget_3->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    chooserLayout->addWidget(ui->verticalLayoutWidget_3, 0);
                }
                chooserLayout->addStretch(1); // push buttons to top, fill bottom
            }

            // Fix page_siyi — unlock verticalLayoutWidget_4
            if (ui->page_siyi && !ui->page_siyi->layout()) {
                auto *siyiLayout = new QVBoxLayout(ui->page_siyi);
                siyiLayout->setContentsMargins(4, 4, 4, 4);

                if (ui->verticalLayoutWidget_4) {
                    ui->verticalLayoutWidget_4->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                    ui->verticalLayoutWidget_4->setMinimumSize(0, 0);
                    ui->verticalLayoutWidget_4->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    siyiLayout->addWidget(ui->verticalLayoutWidget_4, 0);
                }
                siyiLayout->addStretch(1);
            }

            // Fix page_servo — unlock formLayoutWidget
            if (ui->page_servo && !ui->page_servo->layout()) {
                auto *servoLayout = new QVBoxLayout(ui->page_servo);
                servoLayout->setContentsMargins(4, 4, 4, 4);

                if (ui->formLayoutWidget) {
                    ui->formLayoutWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                    ui->formLayoutWidget->setMinimumSize(0, 0);
                    ui->formLayoutWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    servoLayout->addWidget(ui->formLayoutWidget, 0);
                }
                servoLayout->addStretch(1);
            }

            // Fix page_ai — unlock verticalLayoutWidget_ai
            if (ui->page_ai && !ui->page_ai->layout()) {
                auto *aiLayout = new QVBoxLayout(ui->page_ai);
                aiLayout->setContentsMargins(4, 4, 4, 4);

                if (ui->verticalLayoutWidget_ai) {
                    ui->verticalLayoutWidget_ai->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                    ui->verticalLayoutWidget_ai->setMinimumSize(0, 0);
                    ui->verticalLayoutWidget_ai->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    aiLayout->addWidget(ui->verticalLayoutWidget_ai, 0);
                }
                aiLayout->addStretch(1);
            }
        }
    }

    // ── 6. Left dock widget — compact joystick panel ──
    if (ui->dockWidget) {
        ui->dockWidget->setMinimumWidth(140);
        ui->dockWidget->setMaximumWidth(200);
        ui->dockWidget->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        ui->dockWidget->setWindowTitle("Joysticks");

        QWidget *dockContents = ui->dockWidget->widget();
        if (dockContents) {
            // Remove any existing layout
            if (dockContents->layout())
                delete dockContents->layout();

            auto *dockLayout = new QVBoxLayout(dockContents);
            dockLayout->setContentsMargins(4, 4, 4, 4);
            dockLayout->setSpacing(4);

            // Joystick list group box
            if (ui->groupBox) {
                ui->groupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                ui->groupBox->setMinimumSize(0, 80);
                ui->groupBox->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                dockLayout->addWidget(ui->groupBox, 1);

                // Give groupBox a proper internal layout so listWidget fills it
                if (!ui->groupBox->layout()) {
                    auto *gbLayout = new QVBoxLayout(ui->groupBox);
                    gbLayout->setContentsMargins(4, 22, 4, 4); // 22 top for group title

                    // Unlock verticalLayoutWidget from its fixed 201x411 geometry
                    if (ui->verticalLayoutWidget) {
                        ui->verticalLayoutWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                        ui->verticalLayoutWidget->setMinimumSize(0, 0);
                        ui->verticalLayoutWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                        gbLayout->addWidget(ui->verticalLayoutWidget, 1);
                    }
                }

                // Configure the list widget for better display
                if (ui->listWidget) {
                    ui->listWidget->setWordWrap(true);
                    ui->listWidget->setTextElideMode(Qt::ElideNone);
                    ui->listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                    ui->listWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                }
            }

            // Rescan button — right below the list
            if (ui->Rescan) {
                ui->Rescan->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                ui->Rescan->setFixedHeight(28);
                dockLayout->addWidget(ui->Rescan, 0);
            }
        }
    }

    // ── 7. Controls container initial state — hidden, width 0 ──
    ui->controlsContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    ui->controlsContainer->setMinimumWidth(0);
    ui->controlsContainer->setMaximumWidth(0);
    ui->controlsContainer->hide();

    int fullW = 340;

    // ── 8. Panel slide animation ──
    m_panelAnimation = new QPropertyAnimation(ui->controlsContainer, "maximumWidth", this);
    m_panelAnimation->setDuration(300);
    m_panelAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    m_panelAnimation->setStartValue(0);
    m_panelAnimation->setEndValue(fullW);

    // Hover timer for auto-expand
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(300);

    this->installEventFilter(this);
    this->setMouseTracking(true);
}

// ---------------------------------------------------------------------------
// setupConnections — all signal/slot wiring
// ---------------------------------------------------------------------------
void MainWindow::setupConnections()
{
    // --- Camera type chooser buttons ---
    if (ui->cameraTypeStack) {
        ui->cameraTypeStack->setCurrentIndex(0);

        connect(ui->btnSelectSiyi,  &QPushButton::clicked, this, &MainWindow::onSelectSiyiClicked);
        connect(ui->btnSelectServo, &QPushButton::clicked, this, &MainWindow::onSelectServoClicked);
        connect(ui->btnSelectAi,    &QPushButton::clicked, this, &MainWindow::onSelectAiClicked);

        connect(ui->btnSiyiBack,  &QPushButton::clicked, this, &MainWindow::onCameraChooseBack);
        connect(ui->btnServoBack, &QPushButton::clicked, this, &MainWindow::onCameraChooseBack);
        connect(ui->btnAiBack,    &QPushButton::clicked, this, &MainWindow::onCameraChooseBack);

        if (ui->btnSiyiSave)
            connect(ui->btnSiyiSave,  &QPushButton::clicked, this, &MainWindow::saveConfig);
        if (ui->btnServoSave)
            connect(ui->btnServoSave, &QPushButton::clicked, this, &MainWindow::saveConfig);
        if (ui->btnAiSave)
            connect(ui->btnAiSave,    &QPushButton::clicked, this, &MainWindow::saveConfig);

        if (ui->btnSiyiDefault)
            connect(ui->btnSiyiDefault,  &QPushButton::clicked, this, &MainWindow::onSiyiDefaultClicked);
        if (ui->btnServoDefault)
            connect(ui->btnServoDefault, &QPushButton::clicked, this, &MainWindow::onServoDefaultClicked);
        if (ui->btnAiDefault)
            connect(ui->btnAiDefault,    &QPushButton::clicked, this, &MainWindow::onAiDefaultClicked);
    }

    // --- Overlay mode dropdown ---
    if (ui->overlayModeComboBox) {
        connect(ui->overlayModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onOverlayModeChanged);
    }

    // --- Video source dropdown ---
    if (ui->videoSourceComboBox) {
        connect(ui->videoSourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (isInitializing) {
                LOG_VIDEO_SOURCE() << "Dropdown changed during initialization - skipping";
                return;
            }
            LOG_VIDEO_SOURCE() << "Dropdown changed - index:" << index;
            updateVideoSourceInConfig();
            applyConfig();
        });
    }

    // --- Compressor mode checkbox (SIYI) ---
    if (ui->siyi_checkBoxCompressor) {
        // Toggle visibility of compressor fields and single IP field
        auto updateCompressorVisibility = [this](bool checked) {
            if (ui->siyi_lineEditVideoIP)   ui->siyi_lineEditVideoIP->setVisible(checked);
            if (ui->siyi_lineEditControlIP) ui->siyi_lineEditControlIP->setVisible(checked);
            if (ui->siyi_lineEditIP)        ui->siyi_lineEditIP->setVisible(!checked);
        };
        connect(ui->siyi_checkBoxCompressor, &QCheckBox::toggled, this, updateCompressorVisibility);
        // Apply initial state
        updateCompressorVisibility(ui->siyi_checkBoxCompressor->isChecked());
    }

    // --- Port validators ---
    auto setPortValidator = [this](QLineEdit* le){
        if (!le) return;
        le->setValidator(new QIntValidator(1, 65535, this));
    };
    setPortValidator(ui->siyi_lineEditPort);
    setPortValidator(ui->servo_lineEditPort);
    setPortValidator(ui->servo_lineEditServoPort);

    // --- Tab change: populate config fields when Camera Configuration tab is shown ---
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index){
        if (index == 2) {
            populateConfigFields();
        }
    });

    // --- Mode switch buttons (with tab switching) ---
    connect(ui->switchtojoystick, &QPushButton::clicked, this, [this]() {
        onSwitchToJoystick();
        ui->tabWidget->setCurrentIndex(0);
    });
    connect(ui->switchtokeyboard, &QPushButton::clicked, this, [this]() {
        onSwitchToKeyboard();
        ui->tabWidget->setCurrentIndex(1);
    });
    connect(ui->pushButtonConfiguration, &QPushButton::clicked, this, [this]() {
        onSwitchToConfiguration();
        ui->tabWidget->setCurrentIndex(2);
    });

    // --- Toggle button for controls panel ---
    connect(ui->toggleButton, &QToolButton::clicked, this, [this]() {
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
        auto *ctr = ui->controlsContainer;
        auto *lay = ui->centralwidget->layout();

        if (m_panelAnimation->direction() == QAbstractAnimation::Backward) {
            ctr->hide();
            m_panelExpanded = false;
        }

        if (lay) {
            lay->invalidate();
            lay->activate();
        }
        ui->centralwidget->updateGeometry();
    });

    // --- Joystick signals ---
    connect(QJoysticks::getInstance(), &QJoysticks::axisChanged,
            this, &MainWindow::updateAxisValues);
    connect(ui->Rescan, &QPushButton::clicked, this, &MainWindow::updateDeviceList);
    connect(QJoysticks::getInstance(), &QJoysticks::countChanged,
            this, &MainWindow::updateDeviceList);
    connect(QJoysticks::getInstance(), &QJoysticks::buttonChanged,
            this, &MainWindow::updateButtonState);
    connect(ui->listWidget, &QListWidget::itemClicked,
            this, &MainWindow::onJoystickItemClicked);

    // --- Gimbal direction / zoom buttons ---
    connect(ui->toolButtonUp,        &QToolButton::clicked, this, &MainWindow::onFullUp);
    connect(ui->toolButtonDown,      &QToolButton::clicked, this, &MainWindow::onFullDown);
    connect(ui->toolButtonLeft,      &QToolButton::clicked, this, &MainWindow::onFullLeft);
    connect(ui->toolButtonRight,     &QToolButton::clicked, this, &MainWindow::onFullRight);
    connect(ui->toolButtonStop,      &QToolButton::clicked, this, &MainWindow::onStop);
    connect(ui->toolButtonZoomPlus,  &QToolButton::clicked, this, &MainWindow::onZoomMaxIn);
    connect(ui->toolButtonZoomMinus, &QToolButton::clicked, this, &MainWindow::onZoomMaxOut);

    // --- Screenshot button ---
    connect(ui->ScreenshotButton, &QPushButton::clicked,
            this, &MainWindow::on_ScreenshotButton_clicked);

    // --- Servo position (wired once here, not in createCameraControllerFromConfig) ---
    connect(this, &MainWindow::servoPositionChanged, this, [this](int newPos) {
        if (cameraController && cameraController->supportsAbsolutePosition()) {
            cameraController->setGimbalPosition(0, newPos);
        }
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// setupVideoWidget — video widget creation, receiver wiring, camera poll
// ---------------------------------------------------------------------------
void MainWindow::setupVideoWidget()
{
    videoWidget = new VideoRecorderWidget(this);
    videoWidget->setFocusPolicy(Qt::NoFocus);
    videoWidget->getReceiver()->setWindowId(videoWidget->winId());

    mainStackedWidget = new QStackedWidget(this);

    QWidget* videoWrapper = new QWidget();
    QVBoxLayout* videoWrapperLayout = new QVBoxLayout(videoWrapper);
    videoWrapperLayout->setContentsMargins(0, 0, 0, 0);
    videoWrapperLayout->addWidget(videoWidget);

    QWidget* recordsWrapper = new QWidget();
    QVBoxLayout* recordsWrapperLayout = new QVBoxLayout(recordsWrapper);
    recordsWrapperLayout->setContentsMargins(0, 0, 0, 0);
    recordsListWidget = new QListWidget();
    recordsListWidget->setViewMode(QListWidget::IconMode);
    recordsListWidget->setIconSize(QSize(160, 120));
    recordsListWidget->setResizeMode(QListWidget::Adjust);
    recordsListWidget->setSpacing(10);
    connect(recordsListWidget, &QListWidget::itemClicked, this, &MainWindow::onRecordItemClicked);
    recordsWrapperLayout->addWidget(recordsListWidget);

    mainStackedWidget->addWidget(videoWrapper); // Index 0: Video
    mainStackedWidget->addWidget(recordsWrapper); // Index 1: Records

    QVBoxLayout *mainSectionLayout = new QVBoxLayout();
    mainSectionLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *headerWidget = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(10, 5, 10, 5);
    headerLayout->setAlignment(Qt::AlignLeft);

    btnVideoStream = new QPushButton("Video Stream");
    btnRecords = new QPushButton("Records");
    
    // Default active style for Video Stream
    btnVideoStream->setStyleSheet("background-color: #3d59a1; color: white; font-weight: bold; padding: 6px 15px; border-radius: 4px;");
    btnRecords->setStyleSheet("background-color: #24283b; color: #a9b1d6; font-weight: bold; padding: 6px 15px; border-radius: 4px;");

    headerLayout->addWidget(btnVideoStream);
    headerLayout->addWidget(btnRecords);
    headerLayout->addStretch();

    mainSectionLayout->addWidget(headerWidget);
    mainSectionLayout->addWidget(mainStackedWidget);

    if (ui->VideoRecorderSection) {
        ui->VideoRecorderSection->setLayout(mainSectionLayout);
        ui->VideoRecorderSection->setTitle(""); // Hide title since we have buttons now
    }

    connect(btnVideoStream, &QPushButton::clicked, this, &MainWindow::switchToVideoStream);
    connect(btnRecords, &QPushButton::clicked, this, &MainWindow::switchToRecords);

    recordsRefreshTimer = new QTimer(this);
    connect(recordsRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshRecordsList);


    rtspUri = videoWidget->getReceiver()->getRtspUriFromConfig();
    qDebug() << "[VideoReceiver] opening RTSP URI:" << rtspUri;

    // Initial camera status
    ui->lineEditCameraStatus->setText("Checking…");
    ui->lineEditCameraStatus->setStyleSheet("background-color: lightgray; color: black;");

    // Connect VideoReceiver signals
    auto *vr = videoWidget->getReceiver();
    vr->setWindowId(videoWidget->winId());
    connect(vr, &VideoReceiver::cameraStarted,
            this, &MainWindow::onCameraStarted);
    connect(vr, &VideoReceiver::cameraError,
            this, &MainWindow::onCameraError);
    connect(vr, &VideoReceiver::videoCharacteristicsUpdated,
            this, &MainWindow::setVideoCharacteristics);
    connect(vr, &VideoReceiver::bandwidthUpdated,
            this, &MainWindow::onBandwidthUpdated, Qt::QueuedConnection);
    connect(vr, &VideoReceiver::streamHealthUpdated,
            this, &MainWindow::onStreamHealthUpdated, Qt::QueuedConnection);

    // Initialize the ping watcher once — it runs continuously on its own
    // 3-second timer and does not need external polling.
    initializePingWatcher();
}

// ---------------------------------------------------------------------------
// setupRecordingOverlay — recording overlay label and timer
// ---------------------------------------------------------------------------
void MainWindow::setupRecordingOverlay()
{
    useLocalCamera = true;

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

    recordUiTimer = new QTimer(this);
    recordUiTimer->setInterval(500);
    connect(recordUiTimer, &QTimer::timeout, this, &MainWindow::updateRecordTime);

    recordState = RecordState::Idle;
    ui->RecordButton->setText("Start Recording");
}

// ---------------------------------------------------------------------------
// setupConfigOverlayLabel — creates the config display overlay label
// ---------------------------------------------------------------------------
void MainWindow::setupConfigOverlayLabel()
{
    if (configDisplayLabel) return;

    configDisplayLabel = new QLabel(ui->VideoRecorderSection);
    configDisplayLabel->setFixedWidth(280);
    configDisplayLabel->setMinimumHeight(100);
    configDisplayLabel->setMaximumHeight(800);
    configDisplayLabel->move(10, 30);
    configDisplayLabel->setAutoFillBackground(true);
    configDisplayLabel->setStyleSheet(
        "background-color: rgb(30, 30, 30); color: white; "
        "padding: 10px; border-radius: 6px; "
        "font-family: 'Courier New', monospace; font-size: 11px;");
    configDisplayLabel->setWordWrap(true);
    configDisplayLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    configDisplayLabel->setVisible(false);
}
