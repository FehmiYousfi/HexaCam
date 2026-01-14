cmake_minimum_required(VERSION 3.16)

project(JoystickIdentifier VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
add_link_options(   -fsanitize=address)


#set(CMAKE_EXE_LINKER_FLAGS "-static")
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
#set(BUILD_SHARED_LIBS OFF)
#set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Qt static configuration
set(QT6_USE_STATIC ON)
set(QT_NO_LINK_QTMAIN ON)

# Policy settings
if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()


# Set Qt6 prefix path (adjust if needed)
#set(CMAKE_PREFIX_PATH "/home/med/Qt/6.8.1/gcc_64/lib/cmake")

# Find Qt6 required components.
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Core Concurrent)
find_package(X11 REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -D_DEBUG")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")




# Add QJoysticks as a subdirectory.
add_subdirectory(QJoysticks ${CMAKE_BINARY_DIR}/QJoysticks)

# Set up project sources.
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    signalhandler.cpp
    signalhandler.h
    cJSON.c
    cJSON.h
    servo_client.cpp
    servo_client.hpp
    ServoWorker.h   
    ping.cpp
    ping.h
)

# Create executable using Qt6.
qt_add_executable(JoystickIdentifier
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    mainwindow.ui
    CameraController.h
    SiyiCameraController.h SiyiCameraController.cpp
    ServoCameraController.h ServoCameraController.cpp
    mainwindow.ui.bak



)

#--- bundle resources into the binary
qt_add_resources(RESOURCES_ADDED resources.qrc)
# add the generated list of qrc files to your target
target_sources(JoystickIdentifier PRIVATE ${RESOURCES_ADDED})


find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTAPP REQUIRED
    gstreamer-app-1.0
)
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-video-1.0
)


# Add subdirectory for VideoReceiver.
add_subdirectory(VideoReceiver)


add_library(siyi-sdk STATIC
    thirdparty/SIYI-SDK/src/sdk.cpp
    thirdparty/SIYI-SDK/src/message.cpp
    thirdparty/SIYI-SDK/src/crc16.cpp
)
target_include_directories(siyi-sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/SIYI-SDK/src
)

# Link libraries.
target_link_libraries(JoystickIdentifier PRIVATE
    ${X11_LIBRARIES}
    VideoReceiver
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    QJoysticks
    #${CMAKE_SOURCE_DIR}/thirdparty/SIYI-SDK/build/libsiyi-sdk-static.a
    siyi-sdk
    Qt6::Concurrent
    # Network libraries for ping functionality (Windows only)
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:iphlpapi>
)

target_link_libraries(VideoReceiver
  PRIVATE
    ${GSTAPP_LIBRARIES}
    ${GST_LIBRARIES}        # if you need core/video too
)
target_include_directories(VideoReceiver
  PRIVATE
    ${GSTAPP_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}     # if you pulled in core/video above
)

# Additional video recorder sources.
set(VIDEO_RECORDER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoRecorderApp/VideoRecorderWidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoReceiver/VideoReceiver.cpp
)

set(VIDEO_RECORDER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoRecorderApp/VideoRecorderWidget.h
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoReceiver/VideoReceiver.h
)



target_sources(JoystickIdentifier PRIVATE ${VIDEO_RECORDER_SOURCES} ${VIDEO_RECORDER_HEADERS})
target_include_directories(JoystickIdentifier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/SIYI-SDK/src
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoRecorderApp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoReceiver
)

include(GNUInstallDirs)
install(TARGETS JoystickIdentifier
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(JoystickIdentifier)

# after qt_finalize_executable(JoystickIdentifier)

# install the .desktop entry
install(FILES
    ${CMAKE_SOURCE_DIR}/JoystickIdentifier.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)

# install a PNG or SVG you put in deploy/icons/
install(FILES
    ${CMAKE_SOURCE_DIR}/deploy/icons/joystickidentifier.png
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/64x64/apps
)



