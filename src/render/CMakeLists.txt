add_library(RenderEngine STATIC
    src/core/RenderEngine.cpp
    src/core/RenderEngine.h
    src/audio/AudioMixer.cpp
    src/audio/AudioMixer.h
    src/audio/AudioRender.cpp
    src/audio/AudioRender.h
    src/audio/IAudioSource.h
    src/video/UserContext.cpp
    src/video/UserContext.h
    src/video/VideoRender.cpp
    src/video/VideoRender.h
    src/video/GLFrameWork/OpenGLFunctions.h
    src/video/GLFrameWork/ShaderSourceCodes.h
    src/video/GLFrameWork/VideoMesh.cpp
    src/video/GLFrameWork/VideoMesh.h
    src/video/GLFrameWork/VideoShader.cpp
    src/video/GLFrameWork/VideoShader.h
    src/video/GLFrameWork/YUVTexture.cpp
    src/video/GLFrameWork/YUVTexture.h
)

target_include_directories(RenderEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(MEETING_WINDOWS_7)
    target_link_libraries(RenderEngine PRIVATE Qt5::Gui)
else()
    target_link_libraries(RenderEngine PRIVATE Qt6::OpenGL)
endif()

if(WIN32)
    target_sources(RenderEngine PRIVATE src/audio/windows/WasapiPlayback.cpp)
    target_link_libraries(RenderEngine PRIVATE WindowsAudio)
elseif(APPLE)
    target_sources(RenderEngine PRIVATE src/audio/macos/AudioQueuePlayback.cpp)
    target_link_libraries(RenderEngine
        PUBLIC
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework CoreFoundation"
    )
endif()

target_compile_features(RenderEngine PUBLIC cxx_std_17)
