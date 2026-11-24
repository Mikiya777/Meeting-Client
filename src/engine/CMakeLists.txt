add_library(VceEngine STATIC
   VceEngine.cpp
    private/VceEngineImpl.cpp
    private/audio/AudioProcessingPipeline.cpp
    private/audio/AudioProcessingPipeline.h
    private/capture/CaptureManager.cpp
    private/render/RenderManager.cpp
    private/render/RenderManager.h
    private/render/MockRenderManager.cpp
    private/transport/TransportManager.cpp
    private/transport/TransportManager.h
    private/transport/MockTransportManager.cpp
    private/service/MockServiceManager.cpp
    private/service/ClientIpResolver.cpp
    private/service/GrpcServiceManager.cpp
    private/service/MeetingEventSubscriber.cpp
)

find_path(SPEEXDSP_INCLUDE_DIR NAMES speex/speex_echo.h REQUIRED)
find_library(SPEEXDSP_LIBRARY NAMES speexdsp REQUIRED)

target_include_directories(VceEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${SPEEXDSP_INCLUDE_DIR}
)

target_link_libraries(VceEngine
    PUBLIC
        Capture
        RenderEngine
        TransportCore
        Network
        MeetingClientProto
    PRIVATE
        Logger
        OpenCvBeauty
        ${SPEEXDSP_LIBRARY}
)

target_compile_features(VceEngine PUBLIC cxx_std_17)
