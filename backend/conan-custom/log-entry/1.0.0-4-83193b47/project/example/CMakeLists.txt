file(GLOB EXAMPLE_SOURCES "src/*.cc")
foreach(SRC_FILE ${EXAMPLE_SOURCES})
    get_filename_component(EX_NAME ${SRC_FILE} NAME_WE)
    add_executable(${EX_NAME} ${SRC_FILE})
    include_directories(${OpenCV_INCLUDE_DIRS})
    target_link_libraries(${EX_NAME}
            MonitoringLogEntry
            gtest::gtest
            pthread
    )
endforeach()