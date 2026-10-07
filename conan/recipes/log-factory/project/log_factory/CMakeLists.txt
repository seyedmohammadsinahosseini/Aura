add_library(MonitoringLogFactory INTERFACE)

target_include_directories(MonitoringLogFactory
        INTERFACE include
)

target_link_libraries(MonitoringLogFactory INTERFACE
        log_entry::log_entry
        fmt::fmt
)
