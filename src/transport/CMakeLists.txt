add_library(TransportCore STATIC
    include/ITransportEngine.h
    include/TransportDefine.h
    src/local/LocalTransportEngine.cpp
    src/local/LocalTransportEngine.h
    src/rtc/RtcPullTransport.cpp
    src/rtc/RtcPullTransport.h
    src/rtc/RtcPushTransport.cpp
    src/rtc/RtcPushTransport.h
    src/codec/SrsMediaCodec.cpp
    src/codec/SrsMediaCodec.h
    src/srs/SrsTransportCommon.cpp
    src/srs/SrsTransportCommon.h
    src/srs/SrsTransportEngine.cpp
    src/srs/SrsTransportEngine.h
    src/srs/SrsWhipWhepClient.cpp
    src/srs/SrsWhipWhepClient.h
    src/TransportEngineFactory.cpp
)

target_include_directories(TransportCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(WIN32)
    find_package(Opus CONFIG REQUIRED)
    find_package(SampleRate CONFIG REQUIRED)
    find_path(OPENH264_INCLUDE_DIR NAMES wels/codec_api.h REQUIRED)
    find_library(OPENH264_LIBRARY NAMES openh264 REQUIRED)
    add_library(OpenH264::OpenH264 UNKNOWN IMPORTED)
    set_target_properties(OpenH264::OpenH264 PROPERTIES
        IMPORTED_LOCATION "${OPENH264_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENH264_INCLUDE_DIR}"
    )
    set(codec_targets Opus::opus OpenH264::OpenH264 SampleRate::samplerate)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    pkg_check_modules(OPENH264 REQUIRED IMPORTED_TARGET openh264)
    pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
    set(codec_targets PkgConfig::OPUS PkgConfig::OPENH264 PkgConfig::SAMPLERATE)
endif()

target_compile_definitions(TransportCore
    PRIVATE
        TRANSPORT_ENABLE_REAL_SRS=1
)

target_link_libraries(TransportCore
    PRIVATE
        LibDataChannel::LibDataChannel
        nlohmann_json::nlohmann_json
        ${codec_targets}
        Network
        Logger
)

target_compile_features(TransportCore PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(H264RecoveryTest ../../tests/h264_recovery_test.cpp)
    target_compile_definitions(H264RecoveryTest PRIVATE TRANSPORT_ENABLE_REAL_SRS=1)
    target_link_libraries(H264RecoveryTest PRIVATE
        TransportCore ${codec_targets})
    add_test(NAME H264RecoveryTest COMMAND H264RecoveryTest)
endif()
