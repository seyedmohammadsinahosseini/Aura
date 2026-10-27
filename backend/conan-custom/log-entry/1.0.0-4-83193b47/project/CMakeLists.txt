add_subdirectory(log_entry)

if(BUILD_EXAMPLE)
    add_subdirectory(example)
    message(STATUS "Example directory is used for checking interface!")
endif()