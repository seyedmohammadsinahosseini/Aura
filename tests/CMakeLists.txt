# Unit tests for the dependency-free parts of the backend.
#
# These targets deliberately avoid OpenCV / TensorRT / FAISS so they can be
# configured, built and executed on any Linux machine (and in CI) without a GPU:
#
#   cmake -S tests -B build/tests
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure
#
# The top-level CMakeLists.txt also adds this directory, so the same tests run
# as part of a full build.

cmake_minimum_required(VERSION 3.26)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(faceauth_tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
endif()

if(NOT TARGET Threads::Threads)
    find_package(Threads REQUIRED)
endif()

get_filename_component(FACEAUTH_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

add_executable(test_sha1
    unit/test_sha1.cpp
    ${FACEAUTH_SOURCE_ROOT}/core/crypto/Sha1.cpp
)
target_include_directories(test_sha1 PRIVATE ${FACEAUTH_SOURCE_ROOT}/core/crypto)

add_executable(test_safe_circular_buffer
    unit/test_safe_circular_buffer.cpp
)
target_include_directories(test_safe_circular_buffer PRIVATE ${FACEAUTH_SOURCE_ROOT}/services/buffer)
target_link_libraries(test_safe_circular_buffer PRIVATE Threads::Threads)

add_test(NAME sha1_vectors COMMAND test_sha1)
add_test(NAME safe_circular_buffer COMMAND test_safe_circular_buffer)
