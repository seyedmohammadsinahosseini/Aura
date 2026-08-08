cmake_minimum_required(VERSION 3.26.6)
project(FaceAuthentication VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(tensorrt_runtime REQUIRED)
find_package(byte_track REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(faiss REQUIRED)
find_package(pugixml REQUIRED)
find_package(GStreamer REQUIRED COMPONENTS gstreamer-1.0 gstreamer-base-1.0)
find_package(Threads REQUIRED)

# Backend module sources
file(GLOB_RECURSE FACE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp
)

set(APP_SOURCES
    modules/camera/CameraManager.cpp
    modules/camera/CameraDiscovery.cpp
    modules/camera/CameraStreamer.cpp
    services/camera/CameraOrchestrator.cpp
    http/http_server.cpp
)

add_library(face_backend SHARED
    ffi/face_ffi.cpp
    ${APP_SOURCES}
    ${FACE_SOURCES}
)

set_target_properties(face_backend PROPERTIES
    OUTPUT_NAME "face_backend"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

target_include_directories(face_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ffi
    ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(face_backend
    opencv::opencv
    nlohmann_json::nlohmann_json
    pugixml::pugixml
    tensorrt_runtime::tensorrt_runtime
    byte_track::byte_track
    SQLite::SQLite3
    faiss
    CUDA::cudart
    nvinfer
    ${GSTREAMER_LIBRARIES}
    Threads::Threads
    "-Wl,-z,noexecstack"
)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME}
    face_backend
)

add_executable(admin_server
    main_admin_server.cpp
)

target_link_libraries(admin_server
    face_backend
)

install(TARGETS face_backend
    LIBRARY DESTINATION lib
)
