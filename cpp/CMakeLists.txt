cmake_minimum_required(VERSION 3.25)
project(PhoneCam VERSION 2.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGL OpenGLWidgets Concurrent)
find_package(FFMPEG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(libavcodec REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(libavutil REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(libswscale REQUIRED IMPORTED_TARGET libswscale)

# ═══════════════════════════════════════════════
# Main application (phonecam.exe)
# ═══════════════════════════════════════════════
qt_add_executable(phonecam WIN32
    src/main.cpp
    resources/phonecam.qrc
    src/core/pcp_protocol.cpp
    src/core/pcp_receiver.cpp
    src/core/hw_decoder.cpp
    src/core/connection_manager.cpp
    src/core/device_discovery.cpp
    src/core/final_frame_composer.cpp
    src/output/virtual_cam.cpp
    src/gui/main_window.cpp
    src/gui/preview_widget.cpp
    src/vcam/shared_memory.cpp  # SharedMemoryWriter for virtual camera output
)

target_include_directories(phonecam PRIVATE src)

target_link_libraries(phonecam PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    PkgConfig::libavcodec
    PkgConfig::libavutil
    PkgConfig::libswscale
)

if(WIN32)
    target_sources(phonecam PRIVATE resources/phonecam.rc)
    target_link_libraries(phonecam PRIVATE ws2_32 iphlpapi)
endif()

# ═══════════════════════════════════════════════
# Virtual Camera DLL (phonecam-virtualcam.dll)
# Separate target — NO Qt dependency
# ═══════════════════════════════════════════════
add_subdirectory(src/vcam)
