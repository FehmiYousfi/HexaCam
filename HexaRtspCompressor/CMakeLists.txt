cmake_minimum_required(VERSION 3.16)
project(HexaRtspCompressor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find GStreamer via pkg-config
find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
    gstreamer-pbutils-1.0
    gstreamer-sdp-1.0
    gstreamer-rtsp-1.0
    gstreamer-rtsp-server-1.0
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/RtspAnalyzer.cpp
    src/RtspBridge.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GST_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GST_LIBRARIES}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${GST_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)
