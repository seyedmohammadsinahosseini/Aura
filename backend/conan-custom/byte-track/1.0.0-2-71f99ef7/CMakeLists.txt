cmake_minimum_required(VERSION 3.26)
project(ByteTrack)

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE Release)
ENDIF()

MESSAGE("Build type: " ${CMAKE_BUILD_TYPE})

if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /O2")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O2")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
find_package(Eigen3 REQUIRED)

if(WIN32)
  add_library(${PROJECT_NAME} STATIC
    src/BYTETracker.cpp
    src/KalmanFilter.cpp
    src/lapjv.cpp
    files/ByteTrack/Object.h
    src/Rect.cpp
    src/STrack.cpp
    windows_compat/uuid/uuid.h
  )
  target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/windows_compat)
  target_link_libraries(${PROJECT_NAME} Eigen3::Eigen rpcrt4)
else()
  find_package(libuuid REQUIRED)
  add_library(${PROJECT_NAME} STATIC
    src/BYTETracker.cpp
    src/KalmanFilter.cpp
    src/lapjv.cpp
    files/ByteTrack/Object.h
    src/Rect.cpp
    src/STrack.cpp
  )
  target_link_libraries(${PROJECT_NAME} Eigen3::Eigen libuuid::libuuid)
endif()

install(DIRECTORY files/ByteTrack DESTINATION include)

install(TARGETS ${PROJECT_NAME}
        EXPORT bytetrackTargets
        DESTINATION lib)

install(EXPORT bytetrackTargets
        FILE bytetrackTargets.cmake
        DESTINATION lib/cmake/bytetrack/
        NAMESPACE bytetrack::
)

include(CMakePackageConfigHelpers)