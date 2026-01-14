cmake_minimum_required(VERSION 3.14)
project(QJoysticks)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt modules.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# --- Find SDL2 ---
find_package(SDL2 REQUIRED)
if(SDL2_FOUND)
    message(STATUS "SDL2 found: ${SDL2_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "SDL2 not found!")
endif()

# Gather source files from 'src' and 'src/QJoysticks'
file(GLOB QJOYSTICKS_SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/QJoysticks/*.cpp
)
file(GLOB QJOYSTICKS_HEADERS 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/QJoysticks/*.h
)

add_library(QJoysticks STATIC ${QJOYSTICKS_SOURCES} ${QJOYSTICKS_HEADERS})

# Add include directories: include SDL2 directories.
target_include_directories(QJoysticks PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SDL/include
    ${SDL2_INCLUDE_DIRS}  # Add SDL2 include directories.
)

# Link QJoysticks with Qt modules and SDL2 libraries.
target_link_libraries(QJoysticks PUBLIC Qt6::Core Qt6::Widgets ${SDL2_LIBRARIES})

# Define SDL_SUPPORTED so that SDL-dependent code is compiled.
target_compile_definitions(QJoysticks PUBLIC SDL_SUPPORTED)

