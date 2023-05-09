cmake_minimum_required(VERSION 3.10)
project(JTFlow)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")
# list(APPEND CMAKE_PREFIX_PATH "C:/dev/JackerTracker/lib/opencv/Build")
# list(APPEND CMAKE_PREFIX_PATH "C:/dev/JackerTracker/build/_deps/ffmpeg_dev-src")

list(APPEND CMAKE_PREFIX_PATH "/usr/local/lib/cmake/opencv4")

find_package(OpenCV REQUIRED)
find_package(FFmpeg REQUIRED)
find_package(OpenCL REQUIRED)

# OpenCV
include_directories(${OpenCV_INCLUDE_DIRS})
set(OpenCV_LIBRARIES "-lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_imgcodecs")
# set(OpenCV_LIBRARIES
#     opencv_calib3d
#     opencv_core
#     opencv_cudaarithm
#     opencv_cudaimgproc
#     opencv_cudaoptflow
#     opencv_cudawarping
#     opencv_cudev
#     opencv_features2d
#     opencv_flann
#     opencv_imgcodecs
#     opencv_imgproc
#     opencv_optflow
#     opencv_video
#     opencv_videoio
#     opencv_ximgproc
# )
message("OpenCV: ${OpenCV_LIBRARIES}")


# FFmpeg
include_directories(${FFMPEG_INCLUDE_DIRS})
set(FFMPEG_LIBRARIES "-lavcodec -lavutil -lavformat")
message("FFmpeg: ${FFMPEG_LIBRARIES}")
message("OpenCL ${OpenCL_INCLUDE_DIRS}")


# set(CMAKE_VERBOSE_MAKEFILE ON)
# set(CMAKE_C_COMPILER "gcc")
# set(CMAKE_CXX_COMPILER "g++")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

SET(MY_TARGETS "")

if(NOT ${SKIP_BUILD_CUDA})
    # -- JTFlowCuda --

    add_library(JTFlowCuda SHARED
        FlowLibShared.hpp
        FlowLibShared.cpp

        cuda/FFmpegDemuxer.h
        cuda/NvDecoder.h
        cuda/nv12_to_rgb.h
        cuda/ReadFlow.h

        cuda/FlowLib.cpp
        cuda/NvDecoder.cpp
        cuda/nv12_to_rgb.cu
        cuda/ReadFlow.cu
    )
    set_property(TARGET JTFlowCuda PROPERTY CXX_STANDARD 20)

    enable_language(CUDA)
    set_source_files_properties(cuda/nv12_to_rgb.cu PROPERTIES LANGUAGE CUDA)
    set_source_files_properties(cuda/ReadFlow.cu PROPERTIES LANGUAGE CUDA)

    target_link_libraries(JTFlowCuda PRIVATE
        ${FFMPEG_LIBRARIES}
        ${OpenCV_LIBRARIES}
        cuda
        nvcuvid
    )

    install(TARGETS JTFlowCuda LIBRARY DESTINATION lib)
endif()

# if (NOT ${SKIP_BUILD_LAV})
    # -- JTFlowLav --

    add_library(JTFlowLav SHARED
        FlowLibShared.hpp
        FlowLibShared.cpp

        lav/Reader.hpp
        
        lav/Reader.cpp
        lav/FlowLib.cpp
    )

    set_property(TARGET JTFlowLav PROPERTY CXX_STANDARD 17)

    set(OpenCV_LIBRARIES
        opencv_core
        opencv_imgcodecs
        opencv_imgproc
    )

    target_link_libraries(JTFlowLav PRIVATE
        ${FFMPEG_LIBRARIES}
        ${OpenCV_LIBRARIES}
        ${OpenCL_LIBRARIES}
    )

    # find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp" PATHS "/usr/local/src/vcpkg")

    target_include_directories(JTFlowLav PRIVATE
        "/usr/include/opencv4/"
        # ${OpenCL_INCLUDE_DIRS}
        # "/usr/local/src/vcpkg/installed/x64-linux/include"
        # ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}
    )

    install(TARGETS JTFlowLav LIBRARY DESTINATION lib)
# endif()


# if(NOT ${SKIP_BUILD_UTIL})
    # -- JTFlowUtil --

    # add_library(JTFlow SHARED FlowLibStub.c)
    add_executable(JTFlowUtil Main.cpp)
    target_compile_definitions(JTFlowUtil PRIVATE FLOWLIB_IMPORT)
    target_link_libraries(JTFlowUtil PRIVATE JTFlowLav)

    install(TARGETS JTFlowUtil RUNTIME DESTINATION bin)
# endif()