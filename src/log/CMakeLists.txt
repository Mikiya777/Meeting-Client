add_library(Logger STATIC
    include/LogLevel.h
    include/Logger.h
    src/Logger.cpp
)

target_include_directories(Logger
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

find_package(spdlog CONFIG REQUIRED)

target_link_libraries(Logger
    PUBLIC
        spdlog::spdlog
)

target_compile_features(Logger PUBLIC cxx_std_17)
