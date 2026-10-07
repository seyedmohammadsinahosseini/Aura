cmake_minimum_required(VERSION 3.26.6)
project(FaceAuthAttendance
    VERSION 1.0.0
    DESCRIPTION "Real-time multi-camera face authentication and attendance backend"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global build settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Werror=return-type)

# ---------------------------------------------------------------------------
# Dependencies (resolved by Conan — see conanfile.txt and docs/BUILDING.md)
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE FACE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/*.cpp
)

set(APP_SOURCES
    modules/camera/CameraManager.cpp
    modules/camera/CameraDiscovery.cpp
    modules/camera/CameraStreamer.cpp
    services/camera/CameraOrchestrator.cpp
    http/http_server.cpp
)

# ---------------------------------------------------------------------------
# Shared recognition backend (consumed through the C FFI boundary in ffi/)
# ---------------------------------------------------------------------------
add_library(face_backend SHARED
    ffi/face_ffi.cpp
    ${APP_SOURCES}
    ${FACE_SOURCES}
)

set_target_properties(face_backend PROPERTIES
    OUTPUT_NAME "face_backend"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(face_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ffi
    # Vendored, header-only dependencies (see third_party/README.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib/include
    ${GSTREAMER_INCLUDE_DIRS}
)

target_compile_definitions(face_backend PUBLIC
    FACEAUTH_VERSION_STRING="${PROJECT_VERSION}"
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

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
add_executable(faceauth main.cpp)
target_link_libraries(faceauth PRIVATE face_backend)

add_executable(faceauth-admin main_admin_server.cpp)
target_link_libraries(faceauth-admin PRIVATE face_backend)

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS face_backend
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS faceauth faceauth-admin
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ffi/face_ffi.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/faceauth
)

# ---------------------------------------------------------------------------
# Unit tests (dependency-free; see tests/CMakeLists.txt)
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
