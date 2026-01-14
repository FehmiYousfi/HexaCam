#ifndef LOGGING_CONFIG_H
#define LOGGING_CONFIG_H

// Logging Configuration - Enable/disable logging for different modules
// Set to 1 to enable, 0 to disable logging for each module

#define ENABLE_IP_WATCHDOG_LOGGING    0
#define ENABLE_UI_STATUS_LOGGING      0
#define ENABLE_VIDEO_SOURCE_LOGGING   1
#define ENABLE_PING_WATCHER_LOGGING   0
#define ENABLE_VIDEO_SHUTDOWN_LOGGING 1
#define ENABLE_VIDEO_RESTORE_LOGGING  1

// Global logging control - set to 0 to disable all logging
#define ENABLE_GLOBAL_LOGGING         1

// Convenience macros for conditional logging
#if ENABLE_GLOBAL_LOGGING

    #if ENABLE_IP_WATCHDOG_LOGGING
        #define LOG_IP_WATCHDOG() qDebug() << "[IP_WATCHDOG]"
    #else
        #define LOG_IP_WATCHDOG() if (false) qDebug()
    #endif

    #if ENABLE_UI_STATUS_LOGGING
        #define LOG_UI_STATUS() qDebug() << "[UI_STATUS]"
    #else
        #define LOG_UI_STATUS() if (false) qDebug()
    #endif

    #if ENABLE_VIDEO_SOURCE_LOGGING
        #define LOG_VIDEO_SOURCE() qDebug() << "[VIDEO_SOURCE]"
    #else
        #define LOG_VIDEO_SOURCE() if (false) qDebug()
    #endif

    #if ENABLE_PING_WATCHER_LOGGING
        #define LOG_PING_WATCHER() qDebug() << "[PING_WATCHER]"
    #else
        #define LOG_PING_WATCHER() if (false) qDebug()
    #endif

    #if ENABLE_VIDEO_SHUTDOWN_LOGGING
        #define LOG_VIDEO_SHUTDOWN() qDebug() << "[VIDEO_SHUTDOWN]"
    #else
        #define LOG_VIDEO_SHUTDOWN() if (false) qDebug()
    #endif

    #if ENABLE_VIDEO_RESTORE_LOGGING
        #define LOG_VIDEO_RESTORE() qDebug() << "[VIDEO_RESTORE]"
    #else
        #define LOG_VIDEO_RESTORE() if (false) qDebug()
    #endif

#else
    // Global logging disabled - all logging macros become no-ops
    #define LOG_IP_WATCHDOG() if (false) qDebug()
    #define LOG_UI_STATUS() if (false) qDebug()
    #define LOG_VIDEO_SOURCE() if (false) qDebug()
    #define LOG_PING_WATCHER() if (false) qDebug()
    #define LOG_VIDEO_SHUTDOWN() if (false) qDebug()
    #define LOG_VIDEO_RESTORE() if (false) qDebug()
#endif

#endif // LOGGING_CONFIG_H
