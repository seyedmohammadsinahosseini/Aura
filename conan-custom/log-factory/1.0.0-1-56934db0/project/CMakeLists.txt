add_subdirectory(log_factory)

if(BUILD_EXAMPLE)
    add_subdirectory(example)
    messageWithColor(BOLD COLOR MAGENTA "Example directory is used for checking interface!")
endif()