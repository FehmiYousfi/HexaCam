cmake_minimum_required(VERSION 3.16)

project(VideoReceiver LANGUAGES CXX)

# Create a static library for VideoReceiver.
add_library(VideoReceiver STATIC
    VideoReceiver.cpp
    VideoReceiver.h
)

# Find Qt6 components required for VideoReceiver.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Use pkg-config to find GStreamer.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-video-1.0)

# Add GStreamer include directories as PUBLIC so they are available
# to any target linking VideoReceiver.
target_include_directories(VideoReceiver PUBLIC
    ${GSTREAMER_INCLUDE_DIRS}
)

# Link VideoReceiver against Qt6 libraries and GStreamer libraries.
target_link_libraries(VideoReceiver PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${GSTREAMER_LIBRARIES}
)

