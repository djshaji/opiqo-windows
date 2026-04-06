cmake_minimum_required(VERSION 3.16)

# Default to Windows cross-compilation with MinGW on Linux hosts.
if(UNIX AND NOT APPLE)
    if(NOT DEFINED CMAKE_SYSTEM_NAME)
        set(CMAKE_SYSTEM_NAME Windows)
    endif()
    if(NOT DEFINED CMAKE_C_COMPILER)
        set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    endif()
endif()

project(opiqo LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Build number: git commit count + 1
# ---------------------------------------------------------------------------
find_package(Git QUIET)
if(Git_FOUND OR GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_COMMIT_COUNT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_COUNT_RESULT
    )
endif()
if(NOT GIT_COMMIT_COUNT OR NOT GIT_COUNT_RESULT EQUAL 0)
    set(GIT_COMMIT_COUNT 0)
endif()
math(EXPR GIT_BUILD_NUMBER "${GIT_COMMIT_COUNT} + 1")
message(STATUS "Build number: ${GIT_BUILD_NUMBER} (${GIT_COMMIT_COUNT} commits + 1)")

# Generate app.rc from template, substituting @GIT_BUILD_NUMBER@
configure_file(
    "${CMAKE_SOURCE_DIR}/src/win32/app.rc.in"
    "${CMAKE_BINARY_DIR}/app.rc"
    @ONLY
)

# Tell windres where to find resource.h (src/win32/) when preprocessing the
# generated app.rc which lives in the binary dir, not next to resource.h.
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I\"${CMAKE_SOURCE_DIR}/src/win32\"")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "This project must be configured for Windows. Use -DCMAKE_SYSTEM_NAME=Windows and a Windows toolchain.")
endif()

if(UNIX AND NOT CMAKE_CXX_COMPILER MATCHES "mingw|w64-mingw32")
    message(FATAL_ERROR "On Linux, configure with MinGW cross-compiler (for example x86_64-w64-mingw32-g++).")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(ENGINE_SOURCES
    src/LiveEffectEngine.cpp
    src/FileWriter.cpp
    src/LockFreeQueue.cpp
    src/resample.c
)

# speex resampler compiled standalone outside of the speexdsp library.
set_source_files_properties(src/resample.c PROPERTIES
    COMPILE_DEFINITIONS "OUTSIDE_SPEEX;RANDOM_PREFIX=opiqo"
)

set(WIN32_UI_SOURCES
    src/win32/AppSettings.cpp
    src/win32/MainWindow.cpp
    src/win32/ParameterPanel.cpp
    src/win32/PluginSlot.cpp
    src/win32/PluginDialog.cpp
    src/win32/ControlBar.cpp
    src/win32/SettingsDialog.cpp
    src/win32/UpgradeDialog.cpp
    src/win32/AudioEngine.cpp
    src/win32/WasapiDeviceEnum.cpp
    src/win32/win_logging.cpp
    src/main_win32.cpp
    "${CMAKE_BINARY_DIR}/app.rc"
)

add_executable(opiqo WIN32
    ${ENGINE_SOURCES}
    ${WIN32_UI_SOURCES}
)

# Convenience alias so users can run `cmake --build <dir> --target default`.
add_custom_target(default DEPENDS opiqo)

# Package bin/ directory into opiqo.zip.
add_custom_target(zip
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}/bin" zip -r "${CMAKE_SOURCE_DIR}/opiqo.zip" .
    COMMENT "Creating opiqo.zip from bin/"
)

target_include_directories(opiqo PRIVATE
    src
    include
    include/lilv-0
    include/serd-0
    include/sord-0
    include/sratom-0
    include/zix-0
)

# Derive MinGW sysroot from the active compiler triplet (e.g. x86_64-w64-mingw32-g++).
string(REGEX MATCH "[A-Za-z0-9_]+-w64-mingw32" MINGW_TRIPLET "${CMAKE_CXX_COMPILER}")
if(MINGW_TRIPLET)
    set(MINGW_SYSROOT "/usr/${MINGW_TRIPLET}/sys-root/mingw")
    target_include_directories(opiqo PRIVATE "${MINGW_SYSROOT}/include/opus")
    link_directories("${MINGW_SYSROOT}/lib")
else()
    message(FATAL_ERROR "Could not derive MinGW triplet from CMAKE_CXX_COMPILER='${CMAKE_CXX_COMPILER}'.")
endif()

# All audio libraries are in the MinGW sysroot; link them by name.
target_link_directories(opiqo PRIVATE
    "${MINGW_SYSROOT}/lib"
    "${CMAKE_SOURCE_DIR}/libs"
)

# Local bundled static archives (lilv, serd, sord, sratom, zix, mp3lame, opusenc, sndfile).
file(GLOB LOCAL_ARCHIVES
    "${CMAKE_SOURCE_DIR}/libs/*.a"
    "${CMAKE_SOURCE_DIR}/libs/*.dll.a"
)

target_link_libraries(opiqo PRIVATE
    # Bundled static archives
    ${LOCAL_ARCHIVES}
    # MinGW sysroot audio libraries (same order as Makefile)
    opus
    vorbisenc
    vorbis
    vorbisfile
    ogg
    FLAC
    fftw3
    m
    dl
    # Windows system libraries
    ole32
    uuid
    mmdevapi
    avrt
    comctl32
    comdlg32
    dbghelp
    bcrypt
)
