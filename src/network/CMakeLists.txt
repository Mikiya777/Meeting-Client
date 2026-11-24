add_library(Network STATIC
    include/HttpClient.h
    src/HttpClient.cpp
)

target_include_directories(Network
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(WIN32)
    find_package(CURL CONFIG REQUIRED)
    set(curl_target CURL::libcurl)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
    set(curl_target PkgConfig::LIBCURL)
endif()

target_link_libraries(Network
    PUBLIC
        ${curl_target}
    PRIVATE
        Logger
)

target_compile_features(Network PUBLIC cxx_std_17)
