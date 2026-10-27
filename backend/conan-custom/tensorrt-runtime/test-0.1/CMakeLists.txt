cmake_minimum_required(VERSION 3.28)

project(TensorRTRuntime)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(${CMAKE_SOURCE_DIR}/cmake/dependencies.cmake)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(project)

SET(SOURCES
        project/tensorrt_runtime/src/tensorrt_runtime.cc
        project/logger/src/logger.cc
        project/engine/src/engine.cc
)

IF (DEFINED With_OpenCV)
SET(SOURCES
    project/tensorrt_runtime/src/tensorrt_runtime.cc
    project/float_image_tensor/src/float_image_tensor.cc
    project/logger/src/logger.cc
    project/engine/src/engine.cc
)
ENDIF ()

IF (DEFINED Shared)
    add_library(TensorRTRuntime SHARED
            ${SOURCES}
    )
ELSE ()
    add_library(TensorRTRuntime STATIC
            ${SOURCES}
    )
ENDIF ()

set(TENSORRT_LIBS
    nvinfer
    nvinfer_plugin
    nvonnxparser
)

target_link_libraries(TensorRTRuntime
    CUDA::cudart
    ${TENSORRT_LIBS}
    log_factory::log_factory
    opencv::opencv
    nlohmann_json::nlohmann_json
)
target_include_directories(TensorRTRuntime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/project
)
