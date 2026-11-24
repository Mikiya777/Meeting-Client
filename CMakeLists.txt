cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
elseif(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "/Users/mac/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "/Users/mac/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(MeetingClient VERSION 1.0.0 LANGUAGES CXX)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(CAPTURE_ENABLE_DEVICE_TESTS "Enable capture tests that require real camera/microphone devices" OFF)

option(MEETING_WINDOWS_7 "Build MeetingClient with Qt 5 for Windows 7" OFF)
if(MEETING_WINDOWS_7)
    if(NOT WIN32)
        message(FATAL_ERROR "MEETING_WINDOWS_7 requires a Windows target.")
    endif()
    add_compile_definitions(WINVER=0x0601 _WIN32_WINNT=0x0601)
    set(MEETING_QT_PACKAGE Qt5)
    find_package(Qt5 5.15.2 REQUIRED COMPONENTS Widgets Multimedia)
    find_package(Qt5 5.15.2 QUIET COMPONENTS LinguistTools)
else()
    set(MEETING_QT_PACKAGE Qt6)
    set(MEETING_OPENGL_WIDGETS_TARGET Qt6::OpenGLWidgets)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
    if(WIN32)
        find_package(Qt6 REQUIRED COMPONENTS Multimedia)
    endif()
    find_package(Qt6 QUIET COMPONENTS LinguistTools)
endif()
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protos)
set(GENERATED_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_PROTO_DIR})

set(PROTO_FILES
    ${PROTO_DIR}/user_service.proto
    ${PROTO_DIR}/meeting_service.proto
)

set(GENERATED_PROTO_SRCS)
set(GENERATED_PROTO_HDRS)
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    list(APPEND GENERATED_PROTO_SRCS
        ${GENERATED_PROTO_DIR}/${PROTO_NAME}.pb.cc
        ${GENERATED_PROTO_DIR}/${PROTO_NAME}.grpc.pb.cc
    )
    list(APPEND GENERATED_PROTO_HDRS
        ${GENERATED_PROTO_DIR}/${PROTO_NAME}.pb.h
        ${GENERATED_PROTO_DIR}/${PROTO_NAME}.grpc.pb.h
    )
endforeach()

add_custom_command(
    OUTPUT ${GENERATED_PROTO_SRCS} ${GENERATED_PROTO_HDRS}
    COMMAND protobuf::protoc
    ARGS
        --grpc_out=${GENERATED_PROTO_DIR}
        --cpp_out=${GENERATED_PROTO_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        -I ${PROTO_DIR}
        ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating meeting service gRPC sources"
    VERBATIM
)

add_library(MeetingClientProto STATIC
    ${GENERATED_PROTO_SRCS}
    ${GENERATED_PROTO_HDRS}
)

target_include_directories(MeetingClientProto
    PUBLIC
        ${GENERATED_PROTO_DIR}
)

target_link_libraries(MeetingClientProto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
)

set(MEETING_CLIENT_UI_SOURCES
    src/app/UiEventDispatcher.h
    src/ui/MeetingWindow.cpp
    src/ui/MeetingWindow.h
    src/ui/MeetingWindowObserver.cpp
    src/ui/MeetingWindowObserver.h
    src/ui/widgets/NotificationToast.cpp
    src/ui/widgets/NotificationToast.h
    src/ui/widgets/VideoTileWidget.cpp
    src/ui/widgets/VideoTileWidget.h
    src/ui/widgets/VideoGridWidget.cpp
    src/ui/widgets/VideoGridWidget.h
    src/ui/widgets/MeetingSidePanel.cpp
    src/ui/widgets/MeetingSidePanel.h
    src/ui/widgets/MeetingControlBar.cpp
    src/ui/widgets/MeetingControlBar.h
)

set(MEETING_CLIENT_SOURCES
    src/main.cpp
    src/app/AppLog.cpp
    src/app/AppLog.h
    src/app/SkinManager.cpp
    src/app/SkinManager.h
    resources/resources.qrc
    ${MEETING_CLIENT_UI_SOURCES}
)

include(CTest)

if(WIN32)
    add_subdirectory(src/platform/windows/audio)
endif()

add_subdirectory(src/capture)
add_subdirectory(src/render)
add_subdirectory(src/log)
add_subdirectory(src/network)
add_subdirectory(src/transport)
add_subdirectory("src/OpenCV-美颜")
add_subdirectory(src/engine)

if(MEETING_WINDOWS_7)
    add_executable(app ${MEETING_CLIENT_SOURCES})
else()
    qt_add_executable(app ${MEETING_CLIENT_SOURCES})
endif()

if(WIN32)
    set_target_properties(app PROPERTIES OUTPUT_NAME MeetingClient WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(app PRIVATE src)

target_link_libraries(app PRIVATE
    VceEngine
    ${MEETING_QT_PACKAGE}::Widgets
    ${MEETING_OPENGL_WIDGETS_TARGET}
)

if(BUILD_TESTING)
    add_executable(OpenCvBeautyTest "src/OpenCV-美颜/beauty_test.cpp")
    target_link_libraries(OpenCvBeautyTest PRIVATE VceEngine OpenCvBeauty)
    add_test(NAME OpenCvBeautyTest COMMAND OpenCvBeautyTest)

    add_executable(BeautyButtonTest
        "src/OpenCV-美颜/beauty_button_test.cpp"
        src/ui/widgets/MeetingControlBar.cpp
        src/ui/widgets/MeetingControlBar.h)
    target_include_directories(BeautyButtonTest PRIVATE src)
    target_link_libraries(BeautyButtonTest PRIVATE ${MEETING_QT_PACKAGE}::Widgets)
    add_test(NAME BeautyButtonTest COMMAND BeautyButtonTest)
    set_tests_properties(BeautyButtonTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(MeetingUiLifecycleTest
        tests/meeting_ui_lifecycle_test.cpp
        ${MEETING_CLIENT_UI_SOURCES})
    target_include_directories(MeetingUiLifecycleTest PRIVATE src)
    target_link_libraries(MeetingUiLifecycleTest PRIVATE VceEngine ${MEETING_QT_PACKAGE}::Widgets ${MEETING_OPENGL_WIDGETS_TARGET})
    add_test(NAME MeetingUiLifecycleTest COMMAND MeetingUiLifecycleTest)
    set_tests_properties(MeetingUiLifecycleTest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 15
    )

    add_executable(VceEngineServiceCallbackTest
        tests/engine_service_callback_test.cpp
    )

    target_link_libraries(VceEngineServiceCallbackTest PRIVATE
        VceEngine
    )

    add_test(
        NAME VceEngineServiceCallbackTest
        COMMAND VceEngineServiceCallbackTest
    )

    add_executable(LoggerSmokeTest
        tests/logger_smoke_test.cpp
    )

    target_link_libraries(LoggerSmokeTest PRIVATE
        Logger
    )

    add_test(
        NAME LoggerSmokeTest
        COMMAND LoggerSmokeTest
    )

    if(UNIX)
        add_executable(HttpClientSmokeTest
            tests/http_client_smoke_test.cpp
        )

        target_link_libraries(HttpClientSmokeTest PRIVATE
            Network
            Logger
        )

        add_test(
            NAME HttpClientSmokeTest
            COMMAND HttpClientSmokeTest
        )
    endif()

    add_executable(VceEngineServiceTransportIntegrationTest
        tests/engine_service_transport_integration_test.cpp
    )

    target_link_libraries(VceEngineServiceTransportIntegrationTest PRIVATE
        VceEngine
    )

    add_test(
        NAME VceEngineServiceTransportIntegrationTest
        COMMAND VceEngineServiceTransportIntegrationTest
    )

    add_executable(GrpcServiceManagerSmokeTest
        tests/grpc_service_manager_smoke_test.cpp
    )

    target_link_libraries(GrpcServiceManagerSmokeTest PRIVATE
        VceEngine
    )

    add_executable(VceEngineTransportManagerTest
        tests/engine_transport_manager_test.cpp
    )

    target_link_libraries(VceEngineTransportManagerTest PRIVATE
        VceEngine
    )

    add_test(
        NAME VceEngineTransportManagerTest
        COMMAND VceEngineTransportManagerTest
    )

    add_executable(TransportManagerQueueTest
        tests/transport_manager_queue_test.cpp
    )

    target_link_libraries(TransportManagerQueueTest PRIVATE
        VceEngine
    )

    add_test(
        NAME TransportManagerQueueTest
        COMMAND TransportManagerQueueTest
    )
    set_tests_properties(TransportManagerQueueTest PROPERTIES
        ENVIRONMENT "MEETING_TRANSPORT_ENGINE=local"
        TIMEOUT 15
    )

    add_executable(TransportCoreLocalEngineTest
        tests/transport_core_local_engine_test.cpp
    )

    target_link_libraries(TransportCoreLocalEngineTest PRIVATE
        TransportCore
    )

    add_test(
        NAME TransportCoreLocalEngineTest
        COMMAND TransportCoreLocalEngineTest
    )

    add_executable(TransportCoreSrsEngineTest
        tests/transport_core_srs_engine_test.cpp
    )

    target_link_libraries(TransportCoreSrsEngineTest PRIVATE
        TransportCore
    )

    add_test(
        NAME TransportCoreSrsEngineTest
        COMMAND TransportCoreSrsEngineTest
    )

    add_executable(RenderAudioMixerTest
        tests/render_audio_mixer_test.cpp
    )

    target_link_libraries(RenderAudioMixerTest PRIVATE
        RenderEngine
    )

    add_test(
        NAME RenderAudioMixerTest
        COMMAND RenderAudioMixerTest
    )

    add_executable(AudioProcessingPipelineTest
        tests/audio_processing_pipeline_test.cpp
    )

    target_link_libraries(AudioProcessingPipelineTest PRIVATE
        VceEngine
    )

    add_test(
        NAME AudioProcessingPipelineTest
        COMMAND AudioProcessingPipelineTest
    )

    add_executable(RenderValidationTest
        tests/render_validation_test.cpp
    )

    target_link_libraries(RenderValidationTest PRIVATE
        RenderEngine
    )

    add_test(
        NAME RenderValidationTest
        COMMAND RenderValidationTest
    )

    add_executable(RenderAVSmokeTest
        tests/render_av_smoke_test.cpp
    )

    target_link_libraries(RenderAVSmokeTest PRIVATE
        RenderEngine
        ${MEETING_QT_PACKAGE}::Widgets
        ${MEETING_OPENGL_WIDGETS_TARGET}
    )

    add_executable(CaptureRenderAVSmokeTest
        tests/capture_render_av_smoke_test.cpp
    )

    target_link_libraries(CaptureRenderAVSmokeTest PRIVATE
        Capture
        RenderEngine
        ${MEETING_QT_PACKAGE}::Widgets
        ${MEETING_OPENGL_WIDGETS_TARGET}
    )

    if(APPLE)
        enable_language(OBJCXX)

        add_executable(AVFoundationCameraRenderSmokeTest
            tests/macos/avfoundation_camera_render_smoke_test.mm
        )

        target_link_libraries(AVFoundationCameraRenderSmokeTest PRIVATE
            RenderEngine
            ${MEETING_QT_PACKAGE}::Widgets
            ${MEETING_OPENGL_WIDGETS_TARGET}
            "-framework AVFoundation"
            "-framework AudioToolbox"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework Foundation"
        )
    endif()

    add_executable(CaptureSourcePropertyTest
        tests/capture_source_property_test.cpp
    )

    target_link_libraries(CaptureSourcePropertyTest PRIVATE
        Capture
    )

    add_test(
        NAME CaptureSourcePropertyTest
        COMMAND CaptureSourcePropertyTest
    )

    if(APPLE AND CAPTURE_ENABLE_DEVICE_TESTS)
        add_executable(VceEngineLifecycleTest
            tests/engine_lifecycle_test.cpp
        )

        target_link_libraries(VceEngineLifecycleTest PRIVATE
            VceEngine
        )

        add_test(
            NAME VceEngineLifecycleTest
            COMMAND VceEngineLifecycleTest
        )

        add_executable(VceEngineObserverTest
            tests/engine_observer_test.cpp
        )

        target_link_libraries(VceEngineObserverTest PRIVATE
            VceEngine
        )

        add_test(
            NAME VceEngineObserverTest
            COMMAND VceEngineObserverTest
        )

        add_executable(CaptureObsRuntimeTest
            tests/macos/capture_obs_runtime_test.cpp
        )

        target_link_libraries(CaptureObsRuntimeTest PRIVATE
            Capture
        )

        add_test(
            NAME CaptureObsRuntimeTest
            COMMAND CaptureObsRuntimeTest
        )
        set_tests_properties(CaptureObsRuntimeTest PROPERTIES TIMEOUT 60)

        add_executable(CaptureEngineObsIntegrationTest
            tests/macos/capture_engine_obs_integration_test.cpp
        )

        target_link_libraries(CaptureEngineObsIntegrationTest PRIVATE
            Capture
        )

        add_test(
            NAME CaptureEngineObsIntegrationTest
            COMMAND CaptureEngineObsIntegrationTest
        )

        add_executable(CaptureCallbackSmokeTest
            tests/macos/capture_callback_smoke_test.cpp
        )

        target_link_libraries(CaptureCallbackSmokeTest PRIVATE
            Capture
        )

        add_test(
            NAME CaptureCallbackSmokeTest
            COMMAND CaptureCallbackSmokeTest
        )
    endif()
endif()

set(MEETING_TRANSLATIONS
    translations/zh_CN.ts
    translations/en_US.ts
)
if(MEETING_WINDOWS_7 AND Qt5LinguistTools_FOUND)
    qt5_add_translation(meeting_qm_files ${MEETING_TRANSLATIONS})
    set(translation_qrc "${CMAKE_CURRENT_BINARY_DIR}/meeting_translations.qrc")
    set(translation_resources "<RCC><qresource prefix=\"/i18n\">")
    foreach(qm_file IN LISTS meeting_qm_files)
        get_filename_component(qm_name "${qm_file}" NAME)
        string(APPEND translation_resources "<file>${qm_name}</file>")
    endforeach()
    string(APPEND translation_resources "</qresource></RCC>\n")
    file(GENERATE OUTPUT "${translation_qrc}" CONTENT "${translation_resources}")
    set(translation_cpp "${CMAKE_CURRENT_BINARY_DIR}/qrc_meeting_translations.cpp")
    add_custom_command(
        OUTPUT "${translation_cpp}"
        COMMAND Qt5::rcc --name meeting_translations --output "${translation_cpp}" "${translation_qrc}"
        DEPENDS "${translation_qrc}" ${meeting_qm_files} Qt5::rcc
        VERBATIM
    )
    target_sources(app PRIVATE "${translation_cpp}")
elseif(Qt6LinguistTools_FOUND)
    qt_add_translations(app TS_FILES ${MEETING_TRANSLATIONS} RESOURCE_PREFIX "/i18n")
endif()

# Device-free regression for lifecycle notifications and initialization rollback.
if(BUILD_TESTING)
    add_executable(EngineLifecycleRegressionTest
        tests/engine_lifecycle_regression_test.cpp
        src/engine/private/capture/MockCaptureManager.cpp)
    target_link_libraries(EngineLifecycleRegressionTest PRIVATE VceEngine)
    add_test(NAME EngineLifecycleRegressionTest COMMAND EngineLifecycleRegressionTest)
    set_tests_properties(EngineLifecycleRegressionTest PROPERTIES TIMEOUT 15)
endif()

if(BUILD_TESTING)
    add_executable(GrpcServiceDeadlineTest tests/grpc_service_deadline_test.cpp)
    target_link_libraries(GrpcServiceDeadlineTest PRIVATE VceEngine)
    add_test(NAME GrpcServiceDeadlineTest COMMAND GrpcServiceDeadlineTest)
    set_tests_properties(GrpcServiceDeadlineTest PROPERTIES TIMEOUT 15)
endif()
