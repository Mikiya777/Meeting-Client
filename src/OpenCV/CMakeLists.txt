find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc)

add_library(OpenCvBeauty STATIC BeautyProcessor.cpp BeautyProcessor.h)
target_include_directories(OpenCvBeauty PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(OpenCvBeauty SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(OpenCvBeauty PRIVATE ${OpenCV_LIBS})
target_compile_features(OpenCvBeauty PUBLIC cxx_std_17)
