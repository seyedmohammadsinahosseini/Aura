cmake_minimum_required(VERSION 3.15)
project(LogEntry)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Load Color Format
include(cmake/colorFormatting.cmake)

# Load YAML config directly
include(cmake/load_yaml_config.cmake)

load_yaml_config("${CMAKE_SOURCE_DIR}/build_config.yaml")

# Version
if(DEFINED ENV{APP_VERSION})
    set(APP_VERSION $ENV{APP_VERSION})
else()
    set(APP_VERSION "unknown")
endif()

add_compile_definitions(APP_VERSION="${APP_VERSION}")

messageWithColor(BOLD COLOR MAGENTA "\nBUILD_EXAMPLE = ${BUILD_EXAMPLE}")

set(COLOR_FORMATTED_TEXT_COMBINED "\n")
colorFormatTextAppend(COLOR BLUE "${PROJECT_NAME} VERSION : ")
colorFormatTextAppend(BOLD COLOR GREEN "${APP_VERSION}\n")
message("${COLOR_FORMATTED_TEXT_COMBINED}")

# Add deps
include(cmake/dependencies.cmake)

# Add project directory
add_subdirectory(project)
