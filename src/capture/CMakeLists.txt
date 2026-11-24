add_library(Capture STATIC
    src/core/CaptureEngine.cpp
    src/source/BaseSource.cpp
    src/source/CameraSource.cpp
    src/source/CameraSource.h
    src/source/MicSource.cpp
    src/source/MicSource.h
    src/property/BaseSourceProperty.h
    src/property/CameraSourceProperty.cpp
    src/property/CameraSourceProperty.h
    src/property/MicSourceProperty.cpp
    src/property/MicSourceProperty.h
)

target_include_directories(Capture
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(Capture PUBLIC cxx_std_17)

if(WIN32)
    find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc)
    target_sources(Capture PRIVATE src/windows/QtCaptureRuntime.cpp src/windows/QtCaptureRuntime.h)
    target_include_directories(Capture SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(Capture PRIVATE ${MEETING_QT_PACKAGE}::Multimedia WindowsAudio ${OpenCV_LIBS})
    return()
endif()

if(NOT APPLE)
    message(FATAL_ERROR "Capture is supported on macOS and Windows.")
endif()

enable_language(OBJCXX)
target_sources(Capture PRIVATE
    src/macos/ObsRuntime.cpp
    src/macos/ObsRuntime.h
    src/macos/MacCaptureDevice.h
    src/macos/MacCaptureDevice.mm
)

set(CAPTURE_OBS_SDK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/obs/sdk/32.1.2/macos-arm64"
    CACHE PATH
    "Local fixed-version OBS SDK root used by the capture module."
)

set(CAPTURE_OBS_FRAMEWORK_PATH
    "${CAPTURE_OBS_SDK_DIR}/Frameworks/libobs.framework"
    CACHE PATH
    "Local SDK libobs.framework path."
)

set(CAPTURE_OBS_PLUGIN_PATH
    "${CAPTURE_OBS_SDK_DIR}/PlugIns"
    CACHE PATH
    "Local SDK OBS plugin directory."
)

set(CAPTURE_OBS_DATA_PATH
    "${CAPTURE_OBS_SDK_DIR}/Resources"
    CACHE PATH
    "Local SDK OBS data/resource directory."
)

option(CAPTURE_VALIDATE_OBS_DEPENDENCY
    "Validate local OBS SDK paths during CMake configure."
    OFF
)

if(CAPTURE_VALIDATE_OBS_DEPENDENCY)
    foreach(required_obs_path
            "${CAPTURE_OBS_SDK_DIR}/include/obs.h"
            "${CAPTURE_OBS_SDK_DIR}/include/obsconfig.h"
            "${CAPTURE_OBS_SDK_DIR}/include/simde/x86/sse2.h"
            "${CAPTURE_OBS_FRAMEWORK_PATH}"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libobs-metal.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libobs-opengl.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libavcodec.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libavformat.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libavutil.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libswresample.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libswscale.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libmbedcrypto.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libmbedtls.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libmbedx509.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/librist.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libsrt.dylib"
            "${CAPTURE_OBS_SDK_DIR}/Frameworks/libx264.dylib"
            "${CAPTURE_OBS_PLUGIN_PATH}/mac-avcapture.plugin"
            "${CAPTURE_OBS_PLUGIN_PATH}/mac-capture.plugin"
            "${CAPTURE_OBS_DATA_PATH}")
        if(NOT EXISTS "${required_obs_path}")
            message(FATAL_ERROR "Required OBS dependency path is missing: ${required_obs_path}")
        endif()
    endforeach()

    message(STATUS "Capture OBS dependency paths validated")
endif()

add_library(CaptureObsSdk INTERFACE)

target_include_directories(CaptureObsSdk
    INTERFACE
        "${CAPTURE_OBS_SDK_DIR}/include"
)

target_link_libraries(CaptureObsSdk INTERFACE
    "${CAPTURE_OBS_FRAMEWORK_PATH}"
    "-framework AVFoundation"
    "-framework Foundation"
)

target_compile_definitions(CaptureObsSdk
    INTERFACE
        CAPTURE_OBS_SDK_DIR="${CAPTURE_OBS_SDK_DIR}"
        CAPTURE_OBS_PLUGIN_PATH="${CAPTURE_OBS_PLUGIN_PATH}"
        CAPTURE_OBS_DATA_PATH="${CAPTURE_OBS_DATA_PATH}"
)

target_link_libraries(Capture
    PUBLIC
        CaptureObsSdk
)
