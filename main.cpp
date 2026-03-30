#include <QApplication>
#include <QFile>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QEvent>
#include <QUrl>
#include "signalhandler.h"
#include "mainwindow.h"
#include <QFileInfo>
#include <QCommandLineParser>


int main(int argc, char *argv[])
{
    // // 0) disable VAAPI hardware accel entirely:
    // qputenv("LIBVA_DRIVER_NAME", QByteArray("dummy"));
    // qputenv("GST_VAAPI_ALL_DRIVERS", QByteArray("false"));

    Q_INIT_RESOURCE(resources);
    // Add this before creating QApplication
    qputenv("QT_XCB_FORCE_SOFTWARE_OPENGL", "1");
    qputenv("QMLSCENE_DEVICE", "softwarecontext");
    QApplication app(argc, argv);
    
    // Set application info
    app.setApplicationName("HexaCam");
    app.setApplicationVersion("1.0");

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Hexa5 Camera Control Application");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Mode file option
    QCommandLineOption modeFileOption(
        QStringList() << "m" << "mode-file",
        "Path to the mode JSON file to watch for mode changes",
        "file"
    );
    parser.addOption(modeFileOption);
    
    parser.process(app);
    
    QString modeFilePath = parser.value(modeFileOption);
    
    // If no command line arg, use default path
    if (modeFilePath.isEmpty()) {
        modeFilePath = "/tmp/hexa-mode.json";
        qDebug() << "[MODE_WATCHER] Using default mode file path:" << modeFilePath;
    } else {
        qDebug() << "[MODE_WATCHER] Using mode file path from argument:" << modeFilePath;
    }

    // **1)** Keep the event loop alive even if splash is closed
    app.setQuitOnLastWindowClosed(false);

    //SignalHandler signalHandler;

    // Load your CSS (unchanged)
    QString css;
    QFile f(":/hexa5.css");
    if (f.open(QFile::ReadOnly | QFile::Text))
        css = QString::fromUtf8(f.readAll());

    // Determine video path based on environment
    QString videoPath;
    QFileInfo videoFile;

    // First try: AppImage path
    QString appImagePath = QCoreApplication::applicationDirPath() + "/../share/videos/hexa5camera.mp4";
    if (QFile::exists(appImagePath)) {
        videoPath = appImagePath;
        qDebug() << "Using AppImage video path:" << videoPath;
    }
    // Second try: Development resource path
    // else if (QFile::exists(":/intro.mp4")) {
    //     videoPath = "qrc:/intro.mp4";
    //     qDebug() << "Using resource video path";
    // }
    // Fallback: Absolute path
    else {
        videoPath = "/usr/share/videos/hexa5camera.mp4";
        qWarning() << "Using fallback video path";
    }

    MainWindow w;
    w.setWindowFlags(w.windowFlags()
                     | Qt::WindowMinimizeButtonHint
                     | Qt::WindowMaximizeButtonHint
                     | Qt::WindowCloseButtonHint
                     | Qt::WindowSystemMenuHint
                     );
    
    // Initialize mode file watcher
    w.initializeModeFileWatcher(modeFilePath);
    
    //w.show();
    w.showMaximized();
    w.setStyleSheet(css);

    // in–place splash → UI
    //w.playIntro(videoPath, css);

    return app.exec();
}
