cmake_minimum_required(VERSION 3.10)
project(JTFlow)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

list(APPEND CMAKE_PREFIX_PATH "C:/dev/JackerTracker/lib/opencv/Build")
list(APPEND CMAKE_PREFIX_PATH "C:/dev/JackerTracker/build/_deps/ffmpeg_dev-src")
list(APPEND CMAKE_PREFIX_PATH "/usr/local/lib/cmake/opencv4")

find_package(OpenCV REQUIRED)
find_package(FFmpeg REQUIRED)
find_package(OpenCL REQUIRED)
find_package(Python3 COMPONENTS Development NumPy REQUIRED)

# OpenCV
include_directories(${OpenCV_INCLUDE_DIRS})
# set(OpenCV_LIBRARIES "-lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_imgcodecs")
set(OpenCV_LIBRARIES
    # opencv_calib3d
    opencv_core
    # opencv_cudaarithm
    # opencv_cudaimgproc
    opencv_cudaoptflow
    # opencv_cudawarping
    # opencv_cudev
    # opencv_features2d
    # opencv_flann
    opencv_imgcodecs
    # opencv_imgproc
    # opencv_optflow
    # opencv_video
    # opencv_videoio
    # opencv_ximgproc
)



# FFmpeg
include_directories(${FFMPEG_INCLUDE_DIRS})
if(LINUX OR MINGW)
    set(FFMPEG_LIBRARIES "-lavcodec -lavutil -lavformat")
endif()

# message("OpenCV: ${OpenCV_LIBRARIES}")
# message("FFmpeg: ${FFMPEG_LIBRARIES}")
# message("OpenCL ${OpenCL_INCLUDE_DIRS}")
# message("Python: ${Python3_LIBRARIES}")

if(MINGW)
    set(CMAKE_C_COMPILER "gcc")
    set(CMAKE_CXX_COMPILER "g++")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# -- JTFlowTpl --
SET(SRC_ADD
    src/FlowLibShared.hpp
    src/FlowLibShared.cpp
)
SET(INCLUDE_ADD
    ${Python3_INCLUDE_DIRS}
    ${Python3_NumPy_INCLUDE_DIRS}
)
SET(LIB_ADD
    ${Python3_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${OpenCV_LIBRARIES}
)

# -- JTFlowCuda --

add_library(JTFlowCuda SHARED
    cuda/FFmpegDemuxer.h
    cuda/NvDecoder.h
    cuda/nv12_to_rgb.h
    cuda/ReadFlow.h

    cuda/FlowLib.cpp
    cuda/NvDecoder.cpp
    cuda/nv12_to_rgb.cu
    cuda/ReadFlow.cu

    ${SRC_ADD}
)
# set_property(TARGET JTFlowCuda PROPERTY CXX_STANDARD 20)

enable_language(CUDA)
set_source_files_properties(cuda/nv12_to_rgb.cu PROPERTIES LANGUAGE CUDA)
set_source_files_properties(cuda/ReadFlow.cu PROPERTIES LANGUAGE CUDA)

SET(LIB_CUVID "nvcuvid")
if(DOCKER)
    SET(LIB_CUVID "/usr/lib/x86_64-linux-gnu/libnvcuvid.so.1")
endif()

target_link_libraries(JTFlowCuda PRIVATE
    cuda
    ${LIB_CUVID}
    ${LIB_ADD}
)
target_include_directories(JTFlowCuda PRIVATE
    ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
    ${INCLUDE_ADD}
)

install(TARGETS JTFlowCuda LIBRARY DESTINATION lib)

# -- JTFlowLav --

add_library(JTFlowLav SHARED
    lav/Reader.hpp
    
    lav/Reader.cpp
    lav/FlowLib.cpp

    ${SRC_ADD}
)

# set(OpenCV_LIBRARIES
#     opencv_core
#     opencv_imgcodecs
#     opencv_imgproc
# )

target_link_libraries(JTFlowLav PRIVATE
    ${OpenCL_LIBRARIES}
    ${LIB_ADD}
)

target_include_directories(JTFlowLav PRIVATE
    ${OpenCL_INCLUDE_DIRS}
    ${INCLUDE_ADD}
)

install(TARGETS JTFlowLav LIBRARY DESTINATION lib)

# -- JTFlowUtil --

# add_library(JTFlow SHARED FlowLibStub.c)
add_executable(JTFlowUtilLav src/Main.cpp)
target_compile_definitions(JTFlowUtilLav PRIVATE FLOWLIB_IMPORT)
target_link_libraries(JTFlowUtilLav PRIVATE JTFlowLav)

add_executable(JTFlowUtilCuda src/Main.cpp)
target_compile_definitions(JTFlowUtilCuda PRIVATE FLOWLIB_IMPORT)
target_link_libraries(JTFlowUtilCuda PRIVATE JTFlowCuda)

install(TARGETS JTFlowUtilLav JTFlowUtilCuda RUNTIME DESTINATION bin)