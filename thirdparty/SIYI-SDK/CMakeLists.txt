cmake_minimum_required(VERSION 2.8)
project(siyi-sdk)

set(SOURCE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(PROJECT_NAME_STATIC siyi-sdk-static)

set(SOURCE_FILES
        src/crc16.h
        src/crc16.cpp
        src/message.h
        src/message.cpp
        src/sdk.h
        src/sdk.cpp)

add_library(${PROJECT_NAME_STATIC} STATIC ${SOURCE_FILES})
target_include_directories(${PROJECT_NAME_STATIC} PUBLIC ${SOURCE_PATH}/src)

