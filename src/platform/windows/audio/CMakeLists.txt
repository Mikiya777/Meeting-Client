find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)

add_library(WindowsAudio STATIC WasapiDevice.cpp)
target_include_directories(WindowsAudio PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(WindowsAudio SYSTEM PRIVATE "${MINIAUDIO_INCLUDE_DIR}")
target_compile_features(WindowsAudio PUBLIC cxx_std_17)
