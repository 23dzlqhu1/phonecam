cmake_minimum_required(VERSION 3.25)
project(PhoneCam VERSION 2.0.3 LANGUAGES CXX)
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
    src/core/adb_locator.cpp
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

# ═══════════════════════════════════════════════════════════════
# ADB Setup Wizard (phonecam-adb-setup.exe)
# Standalone tool for downloading ADB from a mirror during install
# or later from the main application's settings.
# ═══════════════════════════════════════════════════════════════
qt_add_executable(phonecam-adb-setup WIN32
    src/adb_setup/main.cpp
    src/adb_setup/setup_window.cpp
    src/adb_setup/setup_window.h
    src/core/adb_command_runner.cpp
    src/core/adb_locator.cpp
    src/core/apk_installer.cpp
    src/core/apk_metadata.cpp
)

target_include_directories(phonecam-adb-setup PRIVATE src)

target_link_libraries(phonecam-adb-setup PRIVATE
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    if(NOT TARGET Qt6::QSchannelBackendPlugin)
        message(FATAL_ERROR "Qt Schannel TLS backend target is required for phonecam-adb-setup")
    endif()
    # Copy the mode-matched imported Qt plugin after every build. Generator
    # expressions select qschannelbackend.dll vs qschannelbackendd.dll without
    # hard-coding a vcpkg installation path or mixing configurations.
    add_custom_command(TARGET phonecam-adb-setup POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:phonecam-adb-setup>/tls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QSchannelBackendPlugin>"
            "$<TARGET_FILE_DIR:phonecam-adb-setup>/tls/$<TARGET_FILE_NAME:Qt6::QSchannelBackendPlugin>"
        COMMENT "Deploying the Qt Schannel TLS backend"
        VERBATIM
    )
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(phonecam-adb-setup-tests
        tests/test_adb_setup.cpp
        src/core/adb_command_runner.cpp
        src/core/apk_installer.cpp
        src/core/apk_metadata.cpp
    )
    target_include_directories(phonecam-adb-setup-tests PRIVATE src)
    target_link_libraries(phonecam-adb-setup-tests PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(phonecam-adb-setup-tests PRIVATE
        PHONECAM_TEST_APK_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../dist/PhoneCam-Android-v0.2.9.apk"
    )
    add_test(NAME adb_setup_unit COMMAND phonecam-adb-setup-tests)

    add_executable(phonecam-adb-network-test
        tests/test_adb_official_download.cpp
        src/core/adb_locator.cpp
    )
    target_include_directories(phonecam-adb-network-test PRIVATE src)
    target_link_libraries(phonecam-adb-network-test PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME adb_official_download COMMAND phonecam-adb-network-test)
    set_tests_properties(adb_official_download PROPERTIES TIMEOUT 180 LABELS "network;integration")
endif()

# ═══════════════════════════════════════════════════════════════
# Virtual Camera DLL (phonecam-virtualcam.dll)
# Separate target — NO Qt dependency
# ═══════════════════════════════════════════════════════════════
add_subdirectory(src/vcam)
