# ═══════════════════════════════════════════════
# DirectShow baseclasses — static library
# Source: microsoft/Windows-classic-samples
# ═══════════════════════════════════════════════

set(BASECLASSES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/baseclasses)

# Include ALL .cpp files from baseclasses
file(GLOB BASECLASSES_SOURCES ${BASECLASSES_DIR}/*.cpp)

add_library(dshow_baseclasses STATIC ${BASECLASSES_SOURCES})

target_include_directories(dshow_baseclasses PUBLIC ${BASECLASSES_DIR})
target_compile_definitions(dshow_baseclasses PUBLIC
    WIN32
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
    UNICODE
    _UNICODE
)

# Legacy code needs permissive mode
set_source_files_properties(${BASECLASSES_SOURCES}
    PROPERTIES COMPILE_OPTIONS "/Zc:strictStrings-;/permissive;/w"
)

# ═══════════════════════════════════════════════
# Virtual Camera DLL (phonecam-virtualcam.dll)
# ═══════════════════════════════════════════════

add_library(phonecam-virtualcam SHARED
    dll_main.cpp
    virtual_cam_filter.cpp
    shared_memory.cpp
    dll_register.cpp
    phonecam-virtualcam.def
)

target_include_directories(phonecam-virtualcam PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
# The vcam DLL includes streams.h (baseclasses) which needs permissive mode
target_compile_options(phonecam-virtualcam PRIVATE /permissive /Zc:strictStrings- /w)

target_link_libraries(phonecam-virtualcam PRIVATE
    dshow_baseclasses
    strmiids
    ole32
    uuid
    winmm
    kernel32
    user32
    advapi32
)

set_target_properties(phonecam-virtualcam PROPERTIES
    OUTPUT_NAME "phonecam-virtualcam"
    PREFIX ""
    AUTOMOC OFF
)
